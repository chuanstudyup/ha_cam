#include "EasyRTSPServer.h"
#include "mbedtls/base64.h"
#include "esp_log.h"
#include "esp_timer.h"
#ifdef ENABL_AUDIO_STREAM
#include "Mic.h"
#define AUDIO_BUFFER_SIZE (MIC_SMPLING_RATE / AUDIO_FRAME_FPS * MIC_DATA_BIT_WIDTH / 8) // Number of bytes to send in one packet
#endif
static const char *TAG = "RSTPServer";

char const *DateHeader()
{
  static char buf[128] = {0};
  time_t tt = time(NULL);
  strftime(buf, sizeof(buf), "Date: %a, %b %d %Y %H:%M:%S GMT", gmtime(&tt));
  return buf;
}

static bool isDigit(char c)
{
  return (c >= '0' && c <= '9');
}

static void setRtpHeader(char *m_rtpBuf, uint32_t seq, uint32_t timestamp)
{
  m_rtpBuf[7] = seq & 0x0FF; // each packet is counted with a sequence counter
  m_rtpBuf[6] = seq >> 8;

  m_rtpBuf[8] = (timestamp & 0xFF000000) >> 24; // each image gets a timestamp
  m_rtpBuf[9] = (timestamp & 0x00FF0000) >> 16;
  m_rtpBuf[10] = (timestamp & 0x0000FF00) >> 8;
  m_rtpBuf[11] = (timestamp & 0x000000FF);
}

static int packJpegRtpPack(RTPPacket *rtpPacket, unsigned const char *jpeg, uint32_t jpegLen, int fragmentOffset, BufPtr quant0tbl, BufPtr quant1tbl, StreamInfo *streamInfo)
{
  int fragmentLen = MAX_FRAGMENT_SIZE;
  char *m_rtpBuf = rtpPacket->rtpBuf;

  if (fragmentLen + fragmentOffset > jpegLen) // Shrink last fragment if needed
    fragmentLen = jpegLen - fragmentOffset;

  rtpPacket->isLastFragment = (fragmentOffset + fragmentLen) == jpegLen;

  // Do we have custom quant tables? If so include them per RFC

  bool includeQuantTbl = quant0tbl && quant1tbl && fragmentOffset == 0;
  uint8_t q = includeQuantTbl ? 128 : 0x5e;

  rtpPacket->RtpPacketSize = fragmentLen + KRtpHeaderSize + KJpegHeaderSize + (includeQuantTbl ? (4 + 64 * 2) : 0);

  memset(m_rtpBuf, 0x00, RTP_BUF_SIZE);
  // Prepare the first 4 byte of the packet. This is the Rtp over Rtsp header in case of TCP based transport
  m_rtpBuf[0] = '$'; // magic number
  m_rtpBuf[1] = 0;   // number of multiplexed subchannel on RTPS connection - here the RTP channel
  m_rtpBuf[2] = (rtpPacket->RtpPacketSize & 0x0000FF00) >> 8;
  m_rtpBuf[3] = (rtpPacket->RtpPacketSize & 0x000000FF);
  // Prepare the 12 byte RTP header
  m_rtpBuf[4] = 0x80;                                             // RTP version
  m_rtpBuf[5] = 0x1a | (rtpPacket->isLastFragment ? 0x80 : 0x00); // JPEG payload (26) and marker bit
  m_rtpBuf[12] = 0x13;                                            // 4 byte SSRC (sychronization source identifier)
  m_rtpBuf[13] = 0xf9;                                            // we just an arbitrary number here to keep it simple
  m_rtpBuf[14] = 0x7e;
  m_rtpBuf[15] = 0x67;

  // Prepare the 8 byte payload JPEG header
  m_rtpBuf[16] = 0x00;                                // type specific
  m_rtpBuf[17] = (fragmentOffset & 0x00FF0000) >> 16; // 3 byte fragmentation offset for fragmented images
  m_rtpBuf[18] = (fragmentOffset & 0x0000FF00) >> 8;
  m_rtpBuf[19] = (fragmentOffset & 0x000000FF);

  /*    These sampling factors indicate that the chrominance components of
       type 0 video is downsampled horizontally by 2 (often called 4:2:2)
       while the chrominance components of type 1 video are downsampled both
       horizontally and vertically by 2 (often called 4:2:0). */
  m_rtpBuf[20] = 0x00;                   // type (fixme might be wrong for camera data) https://tools.ietf.org/html/rfc2435
  m_rtpBuf[21] = q;                      // quality scale factor was 0x5e
  m_rtpBuf[22] = streamInfo->width / 8;  // width  / 8
  m_rtpBuf[23] = streamInfo->height / 8; // height / 8

  int headerLen = 24; // Inlcuding jpeg header but not qant table header
  if (includeQuantTbl)
  { // we need a quant header - but only in first packet of the frame
    // if ( debug ) printf("inserting quanttbl\n");
    m_rtpBuf[24] = 0; // MBZ
    m_rtpBuf[25] = 0; // 8 bit precision
    m_rtpBuf[26] = 0; // MSB of lentgh

    int numQantBytes = 64;           // Two 64 byte tables
    m_rtpBuf[27] = 2 * numQantBytes; // LSB of length

    headerLen += 4;

    memcpy(m_rtpBuf + headerLen, quant0tbl, numQantBytes);
    headerLen += numQantBytes;

    memcpy(m_rtpBuf + headerLen, quant1tbl, numQantBytes);
    headerLen += numQantBytes;
  }
  // if ( debug ) printf("Sending timestamp %d, seq %d, fragoff %d, fraglen %d, jpegLen %d\n", m_Timestamp, m_SequenceNumber, fragmentOffset, fragmentLen, jpegLen);

  // append the JPEG scan data to the RTP buffer
  memcpy(m_rtpBuf + headerLen, jpeg + fragmentOffset, fragmentLen);
  fragmentOffset += fragmentLen;

  return rtpPacket->isLastFragment ? 0 : fragmentOffset;
}

#ifdef ENABLE_ADUIO_STREAM
static int packPcmRtpPack(RTPPacket *rtpPacket, unsigned const char *pcm, int pcmLen, int fragmentOffset)
{
  int fragmentLen = MAX_FRAGMENT_SIZE;
  char *m_rtpBuf = rtpPacket->rtpBuf;

  if (fragmentLen + fragmentOffset > pcmLen) // Shrink last fragment if needed
    fragmentLen = pcmLen - fragmentOffset;

  rtpPacket->isLastFragment = (fragmentOffset + fragmentLen) == pcmLen;

  rtpPacket->RtpPacketSize = fragmentLen + KRtpHeaderSize;

  memset(m_rtpBuf, 0x00, RTP_BUF_SIZE);
  // Prepare the first 4 byte of the packet. This is the Rtp over Rtsp header in case of TCP based transport
  m_rtpBuf[0] = '$'; // magic number
  m_rtpBuf[1] = 2;   // number of multiplexed subchannel on RTPS connection - here the RTP channel
  m_rtpBuf[2] = (rtpPacket->RtpPacketSize & 0x0000FF00) >> 8;
  m_rtpBuf[3] = (rtpPacket->RtpPacketSize & 0x000000FF);

  // Prepare the RTP header
  m_rtpBuf[4] = 0x80;                                             // RTP version
  m_rtpBuf[5] = 0x00 | (rtpPacket->isLastFragment ? 0x80 : 0x00); // PCMA payload (8) ; PCMU playload(0) and marker bit
  m_rtpBuf[12] = 0x31;                                            // SSRC (sychronization source identifier)
  m_rtpBuf[13] = 0x9f;                                            // we just an arbitrary number here to keep it simple
  m_rtpBuf[14] = 0xe7;
  m_rtpBuf[15] = 0x76;

  // append the PCM data to the RTP buffer
  memcpy(m_rtpBuf + 16, pcm + fragmentOffset, fragmentLen);
  fragmentOffset += fragmentLen;

  return rtpPacket->isLastFragment ? 0 : fragmentOffset;
}
#endif
static RTSPSession *RTSPSession_Create(RTSPServer *rtspServer, int fd, struct sockaddr_in *addr, StreamInfo *streamInfo, int index)
{
  RTSPSession *session = (RTSPSession *)malloc(sizeof(RTSPSession));
  if (session == NULL)
  {
    ESP_LOGE(TAG, "RTSPSession_Create malloc failed");
    return NULL;
  }
  memset(session, 0, sizeof(RTSPSession));
  session->rtspServer = rtspServer;
  session->tcpClient = fd;
  session->streamInfo = streamInfo;
  snprintf(session->clientIP, sizeof(session->clientIP), "%s", inet_ntoa(addr->sin_addr));
  if (strlen(streamInfo->authStr) == 0)
  {
    session->authed = true;
  }
  else
  {
    session->authed = false;
  }
  session->index = index;
  return session;
}

static void RTSPSession_Destroy(RTSPSession *session)
{
  if (session == NULL)
  {
    return;
  }

  if (session->rtpSocket >= 0)
  {
    close(session->rtpSocket);
  }
#ifdef ENABL_AUDIO_STREAM
  if (session->rtpAudioSocket >= 0)
  {
    close(session->rtpAudioSocket);
  }
#endif
  if (session->tcpClient >= 0)
  {
    close(session->tcpClient);
  }
  free(session);
}

static void Handle_RtspNotFound(RTSPSession *session, int client)
{
  int l = snprintf(session->buf, RTSP_RECV_BUFFER_SIZE,
                   "RTSP/1.0 404 Stream Not Found\r\nCSeq: %u\r\n%s\r\n\r\n",
                   session->CSeq,
                   DateHeader());
  send(client, session->buf, l, MSG_DONTWAIT);
}

static void Handle_RtspBadRequest(RTSPSession *session, int client)
{
  int l = snprintf(session->buf, RTSP_RECV_BUFFER_SIZE, "RTSP/1.0 400 Bad Request\r\nCSeq: %u\r\n\r\n", session->CSeq);
  send(client, session->buf, l, MSG_DONTWAIT);
}

static void Handle_RtspInternalError(RTSPSession *session, int client)
{
  int l = snprintf(session->buf, RTSP_RECV_BUFFER_SIZE, "RTSP/1.0 500 Internal Server Error\r\nCSeq: %u\r\n\r\n", session->CSeq);
  send(client, session->buf, l, MSG_DONTWAIT);
}

static bool ParseOptionRequest(RTSPSession *session, char *aRequest)
{
  /*
  OPTIONS rtsp://192.168.1.102:8554/mjpeg/1 RTSP/1.0\r\n
  CSeq: 0\r\n
  User-Agent: LibVLC/2.2.8 (LIVE555 Streaming Media v2016.02.22)\r\n
  \r\n
  */
  return true;
}

static bool Handle_RtspOPTION(RTSPSession *session, int client)
{
  int l = snprintf(session->buf, RTSP_RECV_BUFFER_SIZE,
                   "RTSP/1.0 200 OK\r\nCSeq: %u\r\n"
                   "Public: DESCRIBE, SETUP, TEARDOWN, PLAY, PAUSE\r\n\r\n",
                   session->CSeq);
  send(client, session->buf, l, MSG_DONTWAIT);
  return true;
}

static bool Handle_RtspDESCRIBE(RTSPSession *session, int client)
{
  char SDPBuf[256] = {0};
  int l;

  if (!session->authed)
  {
    l = snprintf(session->buf, RTSP_RECV_BUFFER_SIZE,
                 "RTSP/1.0 401 Unauthorized\r\nCSeq: %u\r\n"
                 "WWW-Authenticate: Basic realm=\"EasyRTSPServer\"\r\n"
                 "%s\r\n\r\n",
                 session->CSeq,
                 DateHeader());
  }
  else
  {
    snprintf(SDPBuf, sizeof(SDPBuf),
             "v=0\r\n"
             "o=- %d 1 IN IP4 %s\r\n"
             "s=\r\n"
             "t=0 0\r\n"                // start / stop - 0 -> unbounded and permanent session
             "m=video 0 RTP/AVP 26\r\n" // currently we just handle UDP sessions
             "a=x-control:trackID=1\r\n"
             "c=IN IP4 0.0.0.0\r\n"
#ifdef ENABLE_AUDIO_STREAM
             "m=audio 0 RTP/AVP 0\r\n"
             "a=rtpmap:0 PCMU/8000/1\r\n"
             "c=x-control:trackID=2\r\n"
             "c=IN IP4 0.0.0.0\r\n"
#endif
             ,
             rand(),
             session->streamInfo->serverIP);

    l = snprintf(session->buf, RTSP_RECV_BUFFER_SIZE,
                 "RTSP/1.0 200 OK\r\nCSeq: %u\r\n"
                 "Content-Base: %s\r\n"
                 "Content-Type: application/sdp\r\n"
                 "Content-Length: %d\r\n\r\n"
                 "%s",
                 session->CSeq,
                 session->streamInfo->rtspURL,
                 (int)strlen(SDPBuf),
                 SDPBuf);
  }

  send(client, session->buf, l, MSG_DONTWAIT);

  return true;
}

static int creatUdpSocket(int localport)
{
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (sock < 0)
  {
    ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
    return -1;
  }

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(localport);
  addr.sin_addr.s_addr = htonl(INADDR_ANY); // Any address is OK

  int err = bind(sock, (struct sockaddr *)&addr, sizeof(addr));
  if (err < 0)
  {
    ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
    close(sock);
    return -1;
  }
  // Set socket to non-blocking mode
  int flags = fcntl(sock, F_GETFL, 0);
  if (flags == -1)
  {
    ESP_LOGE(TAG, "Unable to get socket flags: errno %d", errno);
    close(sock);
    return -1;
  }
  flags |= O_NONBLOCK;
  if (fcntl(sock, F_SETFL, flags) == -1)
  {
    ESP_LOGE(TAG, "Unable to set socket flags: errno %d", errno);
    close(sock);
    return -1;
  }

  return sock;
}

static void setUdpDestAddr(struct sockaddr_in *dest_addr, char *dest_ip, int destPort)
{
  dest_addr->sin_addr.s_addr = inet_addr(dest_ip);
  dest_addr->sin_family = AF_INET;
  dest_addr->sin_port = htons(destPort);
}

static bool Handle_RtspSETUP(RTSPSession *session, int client, bool isVideo)
{
  char Transport[256] = {0};

  if (session->RtspSessionID == 0)
  {
    session->RtspSessionID = abs(rand()); // create a session ID
  }

  // simulate SETUP server response
  if (session->TcpTransport)
  {
    if (isVideo)
    {
      snprintf(Transport, sizeof(Transport), "RTP/AVP/TCP;unicast;interleaved=0-1");
    }
#ifdef ENABLE_AUDIO_STREAM
    else
    {
      snprintf(Transport, sizeof(Transport), "RTP/AVP/TCP;unicast;interleaved=2-3");
    }
#endif
  }
  else
  {
    if (isVideo)
    {
      session->RtpServerPort = SERVER_RTP_PORT_BASE + session->index * 4 + 0;
      session->RtcpServerPort = SERVER_RTP_PORT_BASE + session->index * 4 + 1;
      snprintf(Transport, sizeof(Transport),
               "RTP/AVP;unicast;destination=%s;source=%s;client_port=%i-%i;server_port=%i-%i",
               session->clientIP,
               session->streamInfo->serverIP,
               session->RtpClientPort,
               session->RtcpClientPort,
               session->RtpServerPort,
               session->RtcpServerPort);
      session->rtpSocket = creatUdpSocket(session->RtpServerPort);
      if (session->rtpSocket < 0)
      {
        ESP_LOGE(TAG, "Failed to create RTP socket");
        Handle_RtspInternalError(session, client);
        return false;
      }
      setUdpDestAddr(&session->dest_addr, session->clientIP, session->RtpClientPort);
    }
    else
    {
#ifdef ENABLE_AUDIO_STREAM
      session->RtpAudioServerPort = SERVER_RTP_PORT_BASE + session->index * 4 + 2;
      session->RtcpAudioServerPort = SERVER_RTP_PORT_BASE + session->index * 4 + 3;
      snprintf(Transport, sizeof(Transport),
               "RTP/AVP;unicast;destination=%s;source=%s;client_port=%i-%i;server_port=%i-%i",
               session->clientIP,
               session->streamInfo->serverIP,
               session->RtpAudioClientPort,
               session->RtcpAudioClientPort,
               session->RtpAudioServerPort,
               session->RtcpAudioServerPort);
      session->rtpAudioSocket = creatUdpSocket(session->RtpAudioServerPort);
      if (session->rtpAudioSocket < 0)
      {
        ESP_LOGE(TAG, "Failed to create RTP socket");
        Handle_RtspInternalError(session, client);
        return false;
      }
      setUdpDestAddr(&session->audio_dest_addr, session->clientIP, session->RtpAudioClientPort);
#else
      ESP_LOGE(TAG, "Don't support audio");
      return false;
#endif
    }
  }

  int l = snprintf(session->buf, RTSP_RECV_BUFFER_SIZE,
                   "RTSP/1.0 200 OK\r\nCSeq: %u\r\n"
                   "Session: %lu;timeout=60\r\n"
                   "Transport: %s\r\n"
                   "%s\r\n\r\n",
                   session->CSeq,
                   session->RtspSessionID,
                   Transport,
                   DateHeader());

  send(client, session->buf, l, MSG_DONTWAIT);
  return true;
}

static bool Handle_RtspPLAY(RTSPSession *session, int client)
{
  int l = snprintf(session->buf, RTSP_RECV_BUFFER_SIZE,
                   "RTSP/1.0 200 OK\r\nCSeq: %u\r\n"
                   "%s\r\n"
                   "Range: npt=0.000-\r\n"
                   "Session: %lu;timeout=60\r\n"
                   "RTP-Info: url=%s/trackID=1;seq=0;rtptime=0\r\n\r\n", // FIXME
                   session->CSeq,
                   session->streamInfo->rtspURL,
                   session->RtspSessionID,
                   DateHeader());

  send(client, session->buf, l, MSG_DONTWAIT);
  return true;
}

static bool Handle_RtspTEARDOWN(RTSPSession *session, int client)
{
  int l = snprintf(session->buf, RTSP_RECV_BUFFER_SIZE,
                   "RTSP/1.0 200 OK\r\nCSeq: %u\r\n\r\n",
                   session->CSeq);

  send(client, session->buf, l, MSG_DONTWAIT);
  return true;
}

static bool checkURL(RTSPSession *session, char *aRequest)
{
  if (strstr(aRequest, session->streamInfo->rtspURL))
  {
    return true;
  }
  else
  {
    return false;
  }
}

static bool parseCSeq(char *aRequest, int *seq)
{
  char *ptr = strstr(aRequest, "CSeq:");
  if (!ptr)
  {
    return false;
  }
  ptr += 5;
  while (!isDigit(*ptr))
  {
    ptr++;
    if (*ptr == '\r')
    {
      return false;
    }
  }

  *seq = atoi(ptr);
  return true;
}

static bool ParseDescribeRequest(RTSPSession *session, char *aRequest)
{
  /*
  DESCRIBE rtsp://192.168.1.102:8554/mjpeg/1 RTSP/1.0\r\n
  CSeq: 1\r\n
  User-Agent: LibVLC/2.2.8 (LIVE555 Streaming Media v2016.02.22)\r\n
  Accept: application/sdp\r\n
  \r\n
  */
  if (!strstr(aRequest, "application/sdp"))
  {
    return false;
  }

  if (strstr(aRequest, session->streamInfo->authStr))
  {
    session->authed = true;
  }
  else
  {
    session->authed = false;
  }
  return true;
}

static bool ParseSetupRequest(RTSPSession *session, char *aRequest, bool *isVideo)
{
  /*
  SETUP rtsp://192.168.1.102:8554/mjpeg/1 RTSP/1.0\r\n
  CSeq: 3\r\n
  User-Agent: LibVLC/2.2.8 (LIVE555 Streaming Media v2016.02.22)\r\n
  Transport: RTP/AVP;unicast;client_port=57844-57845\r\n
  \r\n

  Or

  SETUP rtsp://192.168.1.102:8554/mjpeg/1 RTSP/1.0\r\n
  CSeq: 3\r\n
  User-Agent: LibVLC/2.2.8 (LIVE555 Streaming Media v2016.02.22)\r\n
  Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n
  \r\n
  */

  char *ptr = strstr(aRequest, "Transport:");
  if (!ptr)
  {
    return false;
  }
  ptr += 10;

  if (strstr(ptr, "RTP/AVP/TCP"))
  {
    session->TcpTransport = true;
  }
  else
  {
    session->TcpTransport = false;
    ptr = strstr(ptr, "client_port=");
    if (!ptr)
    {
      return false;
    }
    ptr += 12;
    while (!isDigit(*ptr))
    {
      ptr++;
      if (*ptr == '\r')
      {
        return false;
      }
    }
#ifdef ENABLE_AUDIO_STREAM
    if (strstr(aRequest, "trackID=2") || strstr(aRequest, "Session:"))
    {
      *isVideo = false; // Audio track
      session->RtpAudioClientPort = atoi(ptr);
      session->RtcpAudioClientPort = session->RtpAudioClientPort + 1;
    }
    else
#endif
    {
      session->RtpClientPort = atoi(ptr);
      session->RtcpClientPort = session->RtpClientPort + 1;
    }
  }

  return true;
}

static bool ParsePlayRequest(RTSPSession *session, char *aRequest)
{
  /*
  PLAY rtsp://192.168.1.102:8554/mjpeg/1 RTSP/1.0\r\n
  CSeq: 4\r\n
  Session: 66334873\r\n
  Range: npt=0.000-\r\n
  \r\n
  */
  return true;
}

static bool ParseTeardownRequest(RTSPSession *session, char *aRequest)
{
  /*
  TEARDOWN rtsp://192.168.1.102:8554/mjpeg/1 RTSP/1.0\r\n
  CSeq: 5\r\n
  Session: 66334873\r\n
  \r\n
  */
  return true;
}

enum RecvResult recv_RTSPRequest(RTSPSession *session)
{

  int len = 0;

  len = recv(session->tcpClient, session->buf + session->bufPos, RTSP_RECV_BUFFER_SIZE - session->bufPos - 1, MSG_DONTWAIT); // read the data from the socket
  if (len > 0)
  {
    ESP_LOGI(TAG, "recv %d bytes\n", len);
    session->bufPos += len;
  }
  else if (len < 0)
  {
    if (errno == EAGAIN || errno == EWOULDBLOCK)
    {
      return RECV_CONTINUE;
    }
    else
    {
      return RECV_BAD_REQUEST;
    }
  }
  else if (len == 0)
  {
    ESP_LOGI(TAG, "client disconnected\n");
    return RECV_BAD_REQUEST;
  }

  if (session->bufPos > 0)
  {
    ESP_LOGI(TAG, "Read %lu bytes: %s\n", session->bufPos, session->buf);
    if (session->recvStatus == hdrStateUnknown && session->bufPos >= 6) // we need at least 4-letter at the line start with optional heading CRLF
    {
      if (NULL != strstr(session->buf, "\r\n")) // got a full line
      {
        char *s = session->buf;
        if (*s == '\r' && *(s + 1) == '\n') // skip allowed empty line at front
          s += 2;

        // find out the command type
        session->RtspCmdType = RTSP_UNKNOWN;

        if (strncmp(s, "OPTIONS ", 8) == 0)
          session->RtspCmdType = RTSP_OPTIONS;
        else if (strncmp(s, "DESCRIBE ", 9) == 0)
          session->RtspCmdType = RTSP_DESCRIBE;
        else if (strncmp(s, "SETUP ", 6) == 0)
          session->RtspCmdType = RTSP_SETUP;
        else if (strncmp(s, "PLAY ", 5) == 0)
          session->RtspCmdType = RTSP_PLAY;
        else if (strncmp(s, "TEARDOWN ", 9) == 0)
          session->RtspCmdType = RTSP_TEARDOWN;

        if (session->RtspCmdType != RTSP_UNKNOWN) // got some
          session->recvStatus = hdrStateGotMethod;
        else
          session->recvStatus = hdrStateInvalid;
      }
    }

    if (session->recvStatus == hdrStateUnknown)
    { // if state == hdrStateUnknown when bufPos<6 or find on \r\n, need continue read
      return RECV_CONTINUE;
    }

    if (session->recvStatus == hdrStateInvalid) // read a line but find no RTSP method, bad request
    {
      session->bufPos = 0;
      return RECV_BAD_REQUEST;
    }

    // per https://tools.ietf.org/html/rfc2326 we need to look for an empty line
    // to be sure that we got the correctly formed header. Also starting CRLF should be ignored.
    char *s = strstr(session->bufPos > 4 ? session->buf + session->bufPos - 4 : session->buf, "\r\n\r\n"); // try to save cycles by searching in the new data only
    if (s == NULL)
    { // no end of header seen yet, need continue read
      if (session->bufPos > 380)
      {
        ESP_LOGE(TAG, "RTSP header too long\n");
        session->bufPos = 0;
        return RECV_BAD_REQUEST;
      }
      return RECV_CONTINUE;
    }
    else
    {
      return RECV_FULL_REQUEST;
    }
  }
  else
  {
    return RECV_CONTINUE;
  }
}

enum RTSP_CMD_TYPES Handle_RtspRequest(RTSPSession *session, char *aRequest, int client)
{
  bool isVideo = true; // default to video stream
  ESP_LOGI(TAG, "do  %d method\n", session->RtspCmdType);
  /* check URL */
  if (!checkURL(session, aRequest))
  {
    Handle_RtspNotFound(session, client);
    ESP_LOGI(TAG, "parseCSeq error, bad request\n");
    return RTSP_UNKNOWN;
  }
  if (!parseCSeq(aRequest, &session->CSeq))
  {
    Handle_RtspBadRequest(session, client);
    ESP_LOGI(TAG, "parseCSeq error, bad request\n");
    return RTSP_UNKNOWN;
  }

  switch (session->RtspCmdType)
  {
  case RTSP_OPTIONS:
    Handle_RtspOPTION(session, client);
    break;
  case RTSP_DESCRIBE:
    if (ParseDescribeRequest(session, aRequest))
    {
      Handle_RtspDESCRIBE(session, client);
    }
    else
    {
      Handle_RtspBadRequest(session, client);
      return RTSP_UNKNOWN;
    }
    break;
  case RTSP_SETUP:
    if (ParseSetupRequest(session, aRequest, &isVideo))
    {
      if (!Handle_RtspSETUP(session, client, isVideo))
        return RTSP_INTERNAL_ERROR;
    }
    else
    {
      Handle_RtspBadRequest(session, client);
      return RTSP_UNKNOWN;
    }
    break;
  case RTSP_PLAY:
    Handle_RtspPLAY(session, client);
    break;
  case RTSP_TEARDOWN:
    Handle_RtspTEARDOWN(session, client);
    break;
  default:
    Handle_RtspBadRequest(session, client);
    return RTSP_UNKNOWN;
    break;
  }

  return session->RtspCmdType;
}

static int SendRtpPacket(RTSPSession *session, RTPPacket *rtpPcaket, bool isVideo)
{
  char *rtpBuf = rtpPcaket->rtpBuf; // RTP packet buffer
  int rtpButLen = rtpPcaket->RtpPacketSize;
  int sendlen = 0;
  if (session->TcpTransport)
  {
    sendlen = send(session->tcpClient, rtpBuf, rtpButLen + 4, MSG_DONTWAIT); // send the RTP packet over TCP
  }
  else
  {
    if (isVideo)
    {
      sendlen = sendto(session->rtpSocket, &rtpBuf[4], rtpButLen, 0, (struct sockaddr *)&session->dest_addr, sizeof(struct sockaddr));
    }
#ifdef ENABLE_AUDIO_STREAM
    else if (session->rtpAudioSocket > 0)
    {
      sendlen = sendto(session->rtpAudioSocket, &rtpBuf[4], rtpButLen, 0, (struct sockaddr *)&session->audio_dest_addr, sizeof(struct sockaddr));
    }
#endif
  }
  return sendlen;
}

static void streamRTP(RTSPSession *session, RTPPacket *rtpPcaket, uint32_t curMsec)
{
  RTSPServer *rtspServer = (RTSPServer *)session->rtspServer;
  setRtpHeader(rtpPcaket->rtpBuf, session->SequenceNumber, session->Timestamp);

  int len = SendRtpPacket(session, rtpPcaket, true);
  if (len < 0)
  {
    ESP_LOGE(TAG, "Send RTP packet failed: %d(%s)", errno, strerror(errno));
    if (errno == ENOMEM)
    {
      ESP_LOGE(TAG, "ship the packet, no memory");
      return;
    }
    session->status = STATUS_ERROR;
    return;
  }

  session->SequenceNumber++;
  // Increment ONLY after a full frame
  // Serial.printf("deltams = %d, m_Timestamp = %d\n", deltams, m_Timestamp);
  if (rtpPcaket->isLastFragment)
  {
    // compute deltat (being careful to handle clock rollover with a little lie)
    uint32_t deltams = (curMsec >= session->prevMsec) ? curMsec - session->prevMsec : rtspServer->msecPerFrame;
    if (deltams > 1000)
    {
      ESP_LOGW(TAG, "deltams is too large: %lu", deltams);
      deltams = rtspServer->msecPerFrame; // reset to a default value
    }
    session->prevMsec = curMsec;
    session->Timestamp += (90000 * deltams / 1000); // fixed timestamp increment for a frame rate of 25fps
  }

  session->SendIdx++;
  if (session->SendIdx > 1)
    session->SendIdx = 0;
}
#ifdef ENABL_AUDIO_STREAM
static void streamAudioRTP(RTSPSession *session, RTPPacket *rtpPcaket, uint32_t curMsec)
{
  RTSPServer *rtspServer = (RTSPServer *)session->rtspServer;
  setRtpHeader(rtpPcaket->rtpBuf, session->AudioSequenceNumber, session->AudioTimestamp);

  int len = SendRtpPacket(session, rtpPcaket, false);
  if (len < 0)
  {
    ESP_LOGE(TAG, "Send RTP packet failed: %d(%s)", errno, strerror(errno));
    if (errno == ENOMEM)
    {
      ESP_LOGE(TAG, "ship the packet, no memory");
      return;
    }
    session->status = STATUS_ERROR;
    return;
  }

  session->AudioSequenceNumber++;
  // Increment ONLY after a full frame
  if (rtpPcaket->isLastFragment)
  {
    // compute deltat (being careful to handle clock rollover with a little lie)
    session->AudioTimestamp += (8000 * rtspServer->msecPerAudioFrame / 1000); // fixed timestamp increment for a frame rate of 20fps
  }
}
#endif
void RTSPSession_run(RTSPSession *session)
{
  enum RecvResult result = recv_RTSPRequest(session);
  if (result == RECV_FULL_REQUEST)
  {
    // got full header, parse
    enum RTSP_CMD_TYPES C = Handle_RtspRequest(session, session->buf, session->tcpClient);

    if (C == RTSP_PLAY)
      session->status = STATUS_STREAMING;

    else if (C == RTSP_TEARDOWN)
      session->status = STATUS_CLOSED;

    else if (C == RTSP_INTERNAL_ERROR)
      session->status = STATUS_ERROR;

    // cleaning up
    session->recvStatus = hdrStateUnknown;
    session->bufPos = 0;
    memset(session->buf, 0, RTSP_RECV_BUFFER_SIZE);
  }
  else if (result == RECV_BAD_REQUEST)
  {
    Handle_RtspBadRequest(session, session->tcpClient);
    session->status = STATUS_ERROR;
  }
}

bool RTSPServer_SetStreamSuffix(RTSPServer *rtspServer, char *suffix)
{
  if (strlen(suffix) < LEN_MAX_SUFFIX)
  {
    memset(rtspServer->streamInfo.suffix, 0, LEN_MAX_SUFFIX);
    strcpy(rtspServer->streamInfo.suffix, suffix);
    return true;
  }
  else
  {
    return false;
  }
}

bool RTSPServer_SetFrameRate(RTSPServer *rtspServer, enum RTSP_FRAMERATE frameRate)
{
  switch (frameRate)
  {
  case FRAMERATE_5HZ:
    rtspServer->msecPerFrame = 200;
    break;
  case FRAMERATE_10HZ:
    rtspServer->msecPerFrame = 100;
    break;
  case FRAMERATE_20HZ:
    rtspServer->msecPerFrame = 50;
    break;
  default:
    rtspServer->msecPerFrame = 100;
    break;
  }
  rtspServer->frameRate = frameRate;
  return true;
}

bool RTSPServer_SetAuthAccount(RTSPServer *rtspServer, char *username, char *pwd)
{
  char ori_str[32] = {0};
  char encode_str[128] = {0};
  size_t len = 0;
  if (strlen(username) + strlen(pwd) < 32)
  {
    int l = sprintf(ori_str, "%s:%s", username, pwd);
    if (0 == mbedtls_base64_encode((uint8_t *)encode_str, sizeof(encode_str), &len, (uint8_t *)ori_str, l))
    {
      memcpy(rtspServer->streamInfo.authStr, encode_str, len);
      ESP_LOGI(TAG, "Auth string: %s\n", rtspServer->streamInfo.authStr);
      return true;
    }
    else
    {
      ESP_LOGE(TAG, "Base64 encode failed.");
      return false;
    }
  }
  else
  {
    return false;
  }
}

RTSPServer *RTSPServer_Create(int width, int height)
{
  RTSPServer *server = (RTSPServer *)malloc(sizeof(RTSPServer));
  if (server == NULL)
  {
    ESP_LOGE(TAG, "Failed to allocate memory for RTSPServer.");
    return NULL;
  }
  memset(server, 0, sizeof(RTSPServer));

  server->msecPerFrame = 100; // default 10 fps
  server->msecPerAudioFrame = 1000 / AUDIO_FRAME_FPS;

  snprintf(server->streamInfo.suffix, LEN_MAX_SUFFIX, "mjpeg/1");
  server->streamInfo.width = width;
  server->streamInfo.height = height;

  return server;
}

extern uint8_t *waitRTSPFrame(size_t *frameSize);
extern void releaseRTSPFrame();

static void RTSPServer_Stream(RTSPServer *rtspServer)
{
  int i = 0;
  static int64_t lastimage = 0;
#ifdef ENABLE_AUDIO_STREAM
  static int64_t lastAudio = 0;
  static int audioBufLen = AUDIO_BUFFER_SIZE;
  static uint8_t audioBuf[AUDIO_BUFFER_SIZE] = {0}; // buffer for audio data
#endif
  int64_t now = esp_timer_get_time() / 1000; // get current time in ms

  int streamingCounts = RTSPServer_GetStreamingSessionCounts(rtspServer);
  if (streamingCounts > 0)
  {
    if (now > lastimage + rtspServer->msecPerFrame || now < lastimage)
    { // handle clock rollover
      // streaming video frame
      lastimage = now;
      size_t bytesSize = 0;
      BufPtr bytes = NULL;
      bytes = (BufPtr)waitRTSPFrame(&bytesSize);
      if (!bytes)
      {
        ESP_LOGE(TAG, "waitRTSPFrame failed\n");
        return;
      }
      int64_t waittime = esp_timer_get_time() / 1000 - now;

      uint32_t frameSize = (uint32_t)bytesSize;
      // locate quant tables if possible
      BufPtr qtable0 = NULL;
      BufPtr qtable1 = NULL;

      if (!decodeJPEGfile(&bytes, &frameSize, &qtable0, &qtable1))
      {
        ESP_LOGE(TAG, "can't decode jpeg data\n");
        return;
      }
      int offset = 0;
      do
      {
        offset = packJpegRtpPack(&rtspServer->rtpPacket, bytes, frameSize, offset, qtable0, qtable1, &rtspServer->streamInfo);
        for (i = 0; i < MAX_CLIENTS_NUM; i++)
        {
          if (rtspServer->session[i] && rtspServer->session[i]->status == STATUS_STREAMING)
          {
            streamRTP(rtspServer->session[i], &rtspServer->rtpPacket, now);
          }
        }
      } while (offset != 0);

      releaseRTSPFrame();

      int streamingClients = RTSPServer_GetStreamingSessionCounts(rtspServer);
      int costTime = esp_timer_get_time() / 1000 - now;
      if (costTime <= 0)
      {
        costTime = 1; // avoid division by zero
      }
      rtspServer->owb = frameSize * 8 * streamingClients / costTime; // in kbps

      now = esp_timer_get_time() / 1000; // check if we are overrunning our max frame rate
      if (now > lastimage + rtspServer->msecPerFrame)
      {
        ESP_LOGE(TAG, "streaming a frame with %lu bytes to %d clients cost %d ms, wait frame %d ms. occupied wifi bandwidth %d Kbps\n",
                 frameSize,
                 streamingClients,
                 costTime,
                 waittime,
                 rtspServer->owb);
      }
    }
#ifdef ENABL_AUDIO_STREAM
    if (now > lastAudio + rtspServer->msecPerAudioFrame || now < lastAudio)
    {
      // streaming audio frame
      lastAudio = now;

      int pcmLen = mic_read_pcmu(audioBuf, audioBufLen); // read audio data from microphone
      if (pcmLen < 0)
      {
        ESP_LOGE(TAG, "mic_read failed, read %d bytes", pcmLen);
        return;
      }

      int offset = 0;
      do
      {
        offset = packPcmRtpPack(&rtspServer->rtpPacket, audioBuf, pcmLen, offset);
        for (i = 0; i < MAX_CLIENTS_NUM; i++)
        {
          if (rtspServer->session[i] && rtspServer->session[i]->status == STATUS_STREAMING)
          {
            streamAudioRTP(rtspServer->session[i], &rtspServer->rtpPacket, now);
          }
        }
      } while (offset != 0);
    }
#endif
  }
}

extern void changeRTSPStreaming(bool enable);

static void rtspServerTask(void *arg)
{
  RTSPServer *server = (RTSPServer *)arg;
  struct sockaddr addr = {0};
  socklen_t addr_len = sizeof(addr);
  int i = 0;
  int client = 0;
  int sleepTime = 0;

  while (true)
  {
    sleepTime = 5; // if has client connected, sleep for a short while

    client = accept(server->tcpServer, &addr, &addr_len);
    if (client > 0)
    {
      ESP_LOGI(TAG, "Client connected: %s:%d",
               inet_ntoa(((struct sockaddr_in *)&addr)->sin_addr),
               ntohs(((struct sockaddr_in *)&addr)->sin_port));

      changeRTSPStreaming(true);

      // Set the client to non-blocking mode
      int flags = fcntl(client, F_GETFL, 0);
      if (flags == -1)
      {
        ESP_LOGE(TAG, "fcntl() failed: %s", strerror(errno));
        close(client);
        goto runSession;
      }
      if (fcntl(client, F_SETFL, flags | O_NONBLOCK) == -1)
      {
        ESP_LOGE(TAG, "fcntl() failed: %s", strerror(errno));
        close(client);
        goto runSession;
      }

      for (i = 0; i < MAX_CLIENTS_NUM; i++)
      {
        if (server->session[i] == NULL)
        {
          RTSPSession *session = RTSPSession_Create(server, client, (struct sockaddr_in *)&addr, &server->streamInfo, i);
          if (session == NULL)
          {
            ESP_LOGE(TAG, "RTSPSession_Create failed");
            close(client);
            goto runSession;
          }
          server->session[i] = session;
          break;
        }
      }

      if (i == MAX_CLIENTS_NUM)
      {
        close(client);
        ESP_LOGE(TAG, "Max clients reached, closing connection.");
      }
      else
      {
        ESP_LOGI(TAG, "Accepted client %d", i);
      }
    }
    else
    {
      if (0 == RTSPServer_GetSessionCounts(server))
      {
        sleepTime = 100; // no client connected, sleep for a long while
        changeRTSPStreaming(false);
      }
    }

  runSession:
    for (i = 0; i < MAX_CLIENTS_NUM; i++)
    {
      if (server->session[i])
      {
        RTSPSession_run(server->session[i]);
      }
      if (server->session[i] && server->session[i]->status >= STATUS_CLOSED)
      {
        RTSPSession_Destroy(server->session[i]);
        server->session[i] = NULL;
      }
    }

    RTSPServer_Stream(server); // stream to all clients

    vTaskDelay(sleepTime / portTICK_PERIOD_MS);
  }
}

bool RTSPServer_Start(RTSPServer *rtspServer, char *serverip, int port)
{
  rtspServer->ServerPort = port;
  snprintf(rtspServer->streamInfo.serverIP, LEN_MAX_IP, "%s", serverip);
  snprintf(rtspServer->streamInfo.rtspURL, LEN_MAX_URL, "rtsp://%s:%u/%s", rtspServer->streamInfo.serverIP, rtspServer->ServerPort, rtspServer->streamInfo.suffix);

  rtspServer->tcpServer = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (rtspServer->tcpServer == -1)
  {
    ESP_LOGE(TAG, "socket() failed: %s", strerror(errno));
    return false;
  }
  struct sockaddr_in serverAddr;
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_addr.s_addr = INADDR_ANY;
  serverAddr.sin_port = htons(rtspServer->ServerPort);
  if (bind(rtspServer->tcpServer, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
  {
    ESP_LOGE(TAG, "bind() failed: %s", strerror(errno));
    close(rtspServer->tcpServer);
    return false;
  }
  // Set the socket to non-blocking mode
  int flags = fcntl(rtspServer->tcpServer, F_GETFL, 0);
  if (flags == -1)
  {
    ESP_LOGE(TAG, "fcntl() failed: %s", strerror(errno));
    close(rtspServer->tcpServer);
    return false;
  }
  if (fcntl(rtspServer->tcpServer, F_SETFL, flags | O_NONBLOCK) == -1)
  {
    ESP_LOGE(TAG, "fcntl() failed: %s", strerror(errno));
    close(rtspServer->tcpServer);
    return false;
  }
  if (listen(rtspServer->tcpServer, MAX_CLIENTS_NUM) < 0)
  {
    ESP_LOGE(TAG, "listen() failed: %s", strerror(errno));
    close(rtspServer->tcpServer);
    return false;
  }
  ESP_LOGI(TAG, "RTSP Server start. URL: %s\n", rtspServer->streamInfo.rtspURL);
  ESP_LOGI(TAG, "Resolution: %dx%d\n", rtspServer->streamInfo.width, rtspServer->streamInfo.height);

  // Create a task to handle incoming connections
  xTaskCreate(rtspServerTask, "RTSPServer", 4096, rtspServer, 5, &rtspServer->taskHandle);

  return true;
}

void RTSPServer_Stop(RTSPServer *rtspServer)
{
  for (int i = 0; i < MAX_CLIENTS_NUM; i++)
  {
    if (rtspServer->session[i])
    {
      RTSPSession_Destroy(rtspServer->session[i]);
      rtspServer->session[i] = NULL;
    }
  }

  if (rtspServer->tcpServer >= 0)
  {
    close(rtspServer->tcpServer);
    rtspServer->tcpServer = -1;
  }

  if (rtspServer->taskHandle)
  {
    vTaskDelete(rtspServer->taskHandle);
    rtspServer->taskHandle = NULL;
  }
  ESP_LOGI(TAG, "RTSP Server stopped.");
}

void RTSPServer_Destory(RTSPServer *rtspServer)
{
  if (rtspServer)
  {
    RTSPServer_Stop(rtspServer);
    free(rtspServer);
  }
}

int RTSPServer_GetStreamingSessionCounts(RTSPServer *rtspServer)
{
  int count = 0;
  for (int i = 0; i < MAX_CLIENTS_NUM; i++)
  {
    if (rtspServer->session[i] && rtspServer->session[i]->status == STATUS_STREAMING)
    {
      count++;
    }
  }
  return count;
}

int RTSPServer_GetSessionCounts(RTSPServer *rtspServer)
{
  int count = 0;
  for (int i = 0; i < MAX_CLIENTS_NUM; i++)
  {
    if (rtspServer->session[i])
    {
      count++;
    }
  }
  return count;
}