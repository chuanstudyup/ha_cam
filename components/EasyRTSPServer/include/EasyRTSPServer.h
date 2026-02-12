#ifndef _EASYRTSPServer_H_
#define _EASYRTSPServer_H_

#include "esp_camera.h"
#include "lwip/sockets.h"
#include "rjpeg.h"

//#define ENABLE_AUDIO_STREAM

#define LEN_MAX_SUFFIX 16
#define LEN_MAX_IP 16
#define LEN_MAX_URL 64
#define LEN_MAX_AUTH 64
#define MAX_CLIENTS_NUM 3

#define SERVER_RTP_PORT_BASE 57000

#define RTSP_RECV_BUFFER_SIZE 384  // for incoming requests, and outgoing responses
#define RTSP_PARAM_STRING_MAX 200

#define KRtpHeaderSize 12       // size of the RTP header
#define KJpegHeaderSize 8       // size of the special JPEG payload header
#define MAX_FRAGMENT_SIZE 1300  // FIXME, pick more carefully

#define AUDIO_FRAME_FPS 10 // audio frame rate in fps

#define RTP_BUF_SIZE 1536

enum AudioFormat {
  AUDIO_FORMAT_PCMU = 0, // PCMU (G711u)
  AUDIO_FORMAT_PCMA,     // PCMA (G711a)
};

enum SessionStatus {
  STATUS_UNINIT = 0,
  STATUS_CONNECTING,
  STATUS_STREAMING,
  STATUS_PAUSED,
  STATUS_CLOSED,
  STATUS_ERROR
};

enum RecvStatus {
  hdrStateUnknown,
  hdrStateGotMethod,
  hdrStateInvalid
};

enum RecvResult {
  RECV_BAD_REQUEST,
  RECV_CONTINUE,
  RECV_FULL_REQUEST
};

enum RTSP_CMD_TYPES {
  RTSP_OPTIONS,
  RTSP_DESCRIBE,
  RTSP_SETUP,
  RTSP_PLAY,
  RTSP_TEARDOWN,
  RTSP_UNKNOWN,
  RTSP_INTERNAL_ERROR,
};

enum RTSP_FRAMERATE{
  FRAMERATE_5HZ = 5,
  FRAMERATE_10HZ = 10,
  FRAMERATE_20HZ = 20,
};

typedef struct _StreamInfo {
  char suffix[LEN_MAX_SUFFIX];
  char rtspURL[LEN_MAX_URL];
  char serverIP[LEN_MAX_IP];
  char authStr[LEN_MAX_AUTH];
  int width;
  int height;
}StreamInfo;


typedef struct _RTPPacket {
  char rtpBuf[RTP_BUF_SIZE];  // RTP packet buffer
  bool isLastFragment;
  int RtpPacketSize;
}RTPPacket;

typedef struct _RTSPSession{
  void* rtspServer; /* pointer to RTSP server */
  int tcpClient; /* tcp client fd */
  StreamInfo* streamInfo;
  int index;
  enum SessionStatus status;
  enum RecvStatus recvStatus;
  enum RTSP_CMD_TYPES RtspCmdType;
  int CSeq;
  char clientIP[LEN_MAX_IP];
  int rtpSocket;
  struct sockaddr_in dest_addr; // RTP destination address
#ifdef ENABLE_AUDIO_STREAM
  int rtpAudioSocket; // RTP audio socket
  struct sockaddr_in audio_dest_addr; // RTP destination address
#endif
  uint32_t RtspSessionID;  // create a session ID
  bool authed;

  bool TcpTransport;        /// if Tcp based streaming was activated
  /* Video rtp */
  uint16_t RtpClientPort;   // RTP receiver port on client (in host byte order!)
  uint16_t RtcpClientPort;  // RTCP receiver port on client (in host byte order!)
  uint16_t RtpServerPort;   // RTP sender port on server
  uint16_t RtcpServerPort;  // RTCP sender port on server

#ifdef ENABLE_AUDIO_STREAM
  /* Audio rtp */
  uint16_t RtpAudioClientPort;   // RTP receiver port on client (in host byte order!)
  uint16_t RtcpAudioClientPort;  // RTCP receiver port on client (in host byte order!)
  uint16_t RtpAudioServerPort;   // RTP sender port on server
  uint16_t RtcpAudioServerPort;  // RTCP sender port on server
#endif

  char buf[RTSP_RECV_BUFFER_SIZE];
  uint32_t bufPos;

  /* Video */
  uint32_t prevMsec;
  uint32_t SequenceNumber;
  uint32_t Timestamp;
  uint32_t SendIdx;

  /* Audio */
  uint32_t AudioSequenceNumber;
  uint32_t AudioTimestamp; 
  uint32_t AudioSendIdx;
}RTSPSession;

typedef struct _RTSPServer{
  uint16_t ServerPort; /* port of rtsp server */
  StreamInfo streamInfo;
  int tcpServer; /* server socket fd */
  RTPPacket rtpPacket;
  enum RTSP_FRAMERATE frameRate;
  uint32_t msecPerFrame; /* msec per frame: 1000ms/framerate */
  uint32_t msecPerAudioFrame; /* msec per audio frame: 1000ms/framerate */
  RTSPSession* session[MAX_CLIENTS_NUM];
  TaskHandle_t taskHandle;
  int owb; /* Kbps */
}RTSPServer;


/*****************  RTSPservr **********************/
RTSPServer* RTSPServer_Create(int width, int height);
void RTSPServer_Destory(RTSPServer* rtspServer);
bool RTSPServer_Start(RTSPServer* rtspServer,  char* serverip, int port);
void RTSPServer_Stop(RTSPServer* rtspServer);
bool RTSPServer_SetStreamSuffix(RTSPServer* rtspServer, char* suffix);
bool RTSPServer_SetFrameRate(RTSPServer* rtspServer, enum RTSP_FRAMERATE frameRate);
bool RTSPServer_SetAuthAccount(RTSPServer* rtspServer, char* username, char* pwd);
int RTSPServer_GetStreamingSessionCounts(RTSPServer* rtspServer);
int RTSPServer_GetSessionCounts(RTSPServer* rtspServer);
#endif