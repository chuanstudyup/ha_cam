#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>

#include "esp_timer.h"
#include "esp_log.h"
#include "esp_mac.h"

#include "Utils.h"

#define TAG "UTILS"

/**
 * URL解码函数，将URL编码的字符串转换为原始字符串
 *
 * @param variable 指向URL编码字符串的指针，解码结果也会存储在此缓冲区中
 *
 * 功能说明：
 * 1. 将'+'字符转换为空格
 * 2. 将'%'后跟两个十六进制数字的序列转换为对应的ASCII字符
 * 3. 原地修改字符串，不需要额外分配内存
 *
 * 注意：
 * - 输入缓冲区必须足够大以容纳解码后的字符串
 * - 函数会直接修改输入缓冲区的内容
 */
void urlDecode(char *variable)
{
  char *decoded = variable;
  while (*variable)
  {
    if (*variable == '+')
      *decoded = ' ';
    else if (*variable == '%')
    {
      if (variable[1] && variable[2])
      {
        int partA = toupper(variable[1]) - '0';
        int partB = toupper(variable[2]) - '0';
        if (isdigit((uint8_t)variable[1]))
          partA = variable[1] - '0';
        else
          partA = variable[1] - 'A' + 10;
        if (isdigit((uint8_t)variable[2]))
          partB = variable[2] - '0';
        else
          partB = variable[2] - 'A' + 10;
        *decoded = (partA << 4) | (partB & 0x0F);
        variable += 2;
      }
    }
    else
      *decoded = *variable;
    variable++;
    decoded++;
  }
  *decoded = '\0';
}

/**
 * 从HTTP请求的URL查询字符串中提取键值对
 *
 * @param req HTTP请求对象指针
 * @param variable 用于存储查询字符串的缓冲区，同时用于返回键名
 * @param value 用于存储键值的缓冲区
 *
 * @return
 * - ESP_OK: 成功提取键值对
 * - ESP_FAIL: 查询字符串无效
 *
 * 功能说明：
 * 1. 获取URL查询字符串
 * 2. 对查询字符串进行URL解码
 * 3. 解析键值对格式（key=value）
 * 4. 返回解析结果
 *
 * 注意：
 * - variable缓冲区需要足够大以容纳查询字符串
 * - 函数会修改variable缓冲区内容
 * - 无效查询格式会返回400错误响应
 */
esp_err_t extractQueryKeyVal(httpd_req_t *req, char *variable, char *value)
{
  // get variable and value pair from URL query
  size_t queryLen = httpd_req_get_url_query_len(req) + 1;
  httpd_req_get_url_query_str(req, variable, queryLen);
  urlDecode(variable);
  // extract key
  char *endPtr = strchr(variable, '=');
  if (endPtr != NULL)
  {
    *endPtr = 0;                                    // split variable into 2 strings, first is key name
    strcpy(value, variable + strlen(variable) + 1); // value is now second part of string
  }
  else
  {
    ESP_LOGE(TAG, "Invalid query string %s", variable);
    httpd_resp_set_status(req, "400 Invalid query string");
    httpd_resp_sendstr(req, NULL);
    return ESP_FAIL;
  }
  return ESP_OK;
}

/**
 * @brief 替换字符串中的指定字符
 *
 * 将字符串中所有出现的指定字符替换为另一个字符
 *
 * @param s 目标字符串，将被原地修改
 * @param c 需要被替换的字符
 * @param r 替换后的字符
 */
void replaceChar(char *s, char c, char r)
{
  // replace specified character in string
  int reader = 0;
  while (s[reader])
  {
    if (s[reader] == c)
      s[reader] = r;
    reader++;
  }
}

/**
 * @brief 将ESP错误代码转换为可读字符串
 *
 * @param errCode ESP错误代码，类型为esp_err_t
 * @return const char* 指向错误描述字符串的指针
 *
 * @note 使用静态缓冲区存储错误字符串，非线程安全
 * @see https://github.com/espressif/esp-idf/blob/master/components/esp_common/include/esp_err.h
 */
const char *espErrMsg(esp_err_t errCode)
{
  static char errText[100];
  esp_err_to_name_r(errCode, errText, 100);
  return errText;
}

/**
 * @brief 显示本地时间信息
 *
 * 获取当前epoch时间，格式化为本地时间字符串并输出日志信息
 *
 * @param timeSrc 时间来源描述字符串，用于标识时间来源
 */
static void showLocalTime(const char *timeSrc)
{
  time_t currEpoch = time(NULL);
  char timeFormat[20];
  strftime(timeFormat, sizeof(timeFormat), "%d/%m/%Y %H:%M:%S", localtime(&currEpoch));
  // LOG_INF("Got current time from %s: %s with tz: %s", timeSrc, timeFormat, timezone);
  ESP_LOGI(TAG, "Got current time from %s: %s", timeSrc, timeFormat);
}

/**
 * @brief 将系统时间同步到浏览器时间
 *
 * 如果系统时间与浏览器时间不同步，则使用浏览器提供的UTC时间戳同步系统时间。
 * 同步后会调用showLocalTime函数显示本地时间。
 *
 * @param browserUTC 浏览器提供的UTC时间戳（秒数）
 */
void syncToBrowser(uint32_t browserUTC)
{
  // Synchronize to browser clock if out of sync
  struct timeval tv;
  tv.tv_sec = browserUTC;
  settimeofday(&tv, NULL);
  // setenv("TZ", timezone, 1);
  // tzset();
  showLocalTime("browser");
}

/**
 * 格式化文件大小为人类可读的字符串
 *
 * 根据数值大小自动选择适当的单位(bytes/KB/MB/GB)进行格式化
 *
 * @param sizeVal 要格式化的字节大小值
 * @return 返回格式化后的字符串指针(静态缓冲区，非线程安全)
 *         格式示例: "123 bytes", "456KB", "7.8MB", "9.1GB"
 */
char *fmtSize(uint64_t sizeVal)
{
  // format size according to magnitude
  // only one call per format string
  static char returnStr[20];
  if (sizeVal < 50 * 1024)
    sprintf(returnStr, "%llu bytes", sizeVal);
  else if (sizeVal < ONEMEG)
    sprintf(returnStr, "%lluKB", sizeVal / 1024);
  else if (sizeVal < ONEMEG * 1024)
    sprintf(returnStr, "%0.1fMB", (double)(sizeVal) / ONEMEG);
  else
    sprintf(returnStr, "%0.1fGB", (double)(sizeVal) / (ONEMEG * 1024));
  return returnStr;
}

static char *chunk = NULL;
char startupFailure[SF_LEN] = {0};

#define ps_malloc(size) heap_caps_malloc((size), MALLOC_CAP_SPIRAM)

esp_err_t sendChunks(const char *fname, FILE *df, httpd_req_t *req, bool endChunking)
{
  // use chunked encoding to send large content to browser
  size_t chunksize = 0;
  esp_err_t res = ESP_OK;
  if (!chunk)
  {
    chunk = ps_malloc(CHUNKSIZE);
  }

  chunksize = fread(chunk, 1, CHUNKSIZE, df);
  while (chunksize > 0)
  {
    res = httpd_resp_send_chunk(req, (char *)chunk, chunksize);
    if (res != ESP_OK)
      break;
    chunksize = fread(chunk, 1, CHUNKSIZE, df);
  }
  if (endChunking)
  {
    fclose(df);
    httpd_resp_sendstr_chunk(req, NULL);
  }
  if (res != ESP_OK)
  {
    snprintf(startupFailure, SF_LEN, "Failed to send to browser: %s, err %s", fname, espErrMsg(res));
    ESP_LOGW(TAG, "%s", startupFailure);
    // OTAprereq(); //todo: free up memory
  }
  return res;
}

esp_err_t downloadFile(const char *filePath, FILE *df, httpd_req_t *req)
{
  esp_err_t res = ESP_OK;
  char downloadName[IN_FILE_NAME_LEN];
  char *ptr = strrchr(filePath, '/');
  ptr++;
  strcpy(downloadName, ptr);

  struct stat df_stat;
  fstat(fileno(df), &df_stat);
  size_t downloadSize = df_stat.st_size;

  // create http header
  ESP_LOGI(TAG, "Begin download file: %s, size: %s", downloadName, fmtSize(downloadSize));
  httpd_resp_set_type(req, "application/octet-stream");
  // header field values must remain valid until first send
  char contentDisp[FILE_NAME_LEN];
  snprintf(contentDisp, sizeof(contentDisp) - 1, "attachment; filename=%s", downloadName);
  httpd_resp_set_hdr(req, "Content-Disposition", contentDisp);
  char contentLength[10];
  snprintf(contentLength, sizeof(contentLength) - 1, "%i", downloadSize);
  httpd_resp_set_hdr(req, "Content-Length", contentLength);

  uint32_t start_time = esp_timer_get_time() / 1000;
  res = sendChunks(filePath, df, req, true); // send AVI
  uint32_t cost = esp_timer_get_time() / 1000 - start_time;
  uint32_t speed = downloadSize / cost;
  ESP_LOGI(TAG, "Finished download, cost time: %ums, speed %u kB/s", cost, speed);
  return res;
}

esp_err_t fileHandler(httpd_req_t *req, const char *fileName, bool download)
{
  // send file contents to browser
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  // if (!strcmp(inFileName, LOG_FILE_PATH)) flush_log(false);
  FILE *df = fopen(fileName, "rb");
  if (!df)
  {
    ESP_LOGW(TAG, "File does not exist or cannot be opened: %s", fileName);
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }
  // get df size use posix fstat
  struct stat df_stat;
  fstat(fileno(df), &df_stat);
  size_t df_size = df_stat.st_size;
  if (!df_size)
  {
    // file is empty
    fclose(df);
    httpd_resp_sendstr(req, NULL);
    return ESP_OK;
  }
  return (download) ? downloadFile(fileName, df, req) : sendChunks(fileName, df, req, true);
}

/**
 * @brief 获取当前网络模式
 *
 * 获取ESP32设备当前的WiFi工作模式
 *
 * @return wifi_mode_t 返回当前的WiFi模式，如果获取失败返回WIFI_MODE_NULL
 */
wifi_mode_t netMode()
{
  // return current network mode
  wifi_mode_t wifiMode;
  if (ESP_OK != esp_wifi_get_mode(&wifiMode))
  {
    ESP_LOGE(TAG, "Failed to get WiFi mode");
    return WIFI_MODE_NULL;
  }
  return wifiMode;
}

/**
 * @brief 获取当前WiFi连接的RSSI信号强度值
 *
 * 该函数用于获取设备当前连接的WiFi接入点的接收信号强度指示值(RSSI)。
 * RSSI值表示信号强度，数值越大表示信号越强（越接近0表示信号越好）。
 *
 * @return int 返回当前的RSSI值，单位为dBm。如果获取失败则返回0。
 *             正常情况下，RSSI值为负整数（如：-30, -50, -70等）
 */
int netRSSI()
{
  // return current RSSI value
  int rssi = 0;
  if (ESP_OK != esp_wifi_sta_get_rssi(&rssi))
  {
    ESP_LOGE(TAG, "Failed to get AP info");
    return 0;
  }
  return rssi;
}

/**
 * 获取本地网络的IP地址、子网掩码和默认网关
 *
 * @param ipAddr 存储IP地址的缓冲区，长度至少为16字节
 * @param ipMask 存储子网掩码的缓冲区，长度至少为16字节
 * @param gwIp 存储默认网关的缓冲区，长度至少为16字节
 */
void netLocalIP(char ipAddr[16], char ipMask[16], char gwIp[16])
{
  esp_netif_t *esp_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  esp_netif_ip_info_t ip_info;
  if (!esp_netif)
    return;
  esp_netif_get_ip_info(esp_netif, &ip_info);
  if (ipAddr)
    snprintf(ipAddr, 16, "%u.%u.%u.%u", IP2STR(&ip_info.ip));
  if (ipMask)
    snprintf(ipMask, 16, "%u.%u.%u.%u", IP2STR(&ip_info.netmask));
  if (gwIp)
    snprintf(gwIp, 16, "%u.%u.%u.%u", IP2STR(&ip_info.gw));
}

/**
 * @brief 获取当前网络接口的MAC地址
 *
 * 该函数通过ESP32的WiFi接口获取当前STA模式的MAC地址，
 * 并将其格式化为标准的XX:XX:XX:XX:XX:XX字符串形式。
 *
 * @return char* 指向静态字符串缓冲区的指针，包含格式化后的MAC地址字符串
 */
char *netMacAddress()
{
  uint8_t mac[6];
  static char macStr[18] = {0};
  esp_wifi_get_mac(WIFI_IF_STA, mac);
  sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return macStr;
}

/**
 * @brief 从eFuse获取设备MAC地址并转换为64位整数
 *
 * 该函数从ESP32芯片的eFuse存储器中读取默认的MAC地址，
 * 并将其转换为64位整数值格式返回。MAC地址通常以字节数组形式存储，
 * 通过位移操作将6个字节的MAC地址组合成一个64位整数。
 *
 * @return uint64_t 返回转换后的64位MAC地址整数
 *                  - 高8位对应MAC地址的第一个字节
 *                  - 低8位对应MAC地址的最后一个字节
 */
uint64_t getEfuseMac()
{
  uint8_t mac[6];
  esp_efuse_mac_get_default((uint8_t *)mac);
  uint64_t mac64 = ((uint64_t)mac[0] << 40) |
                   ((uint64_t)mac[1] << 32) |
                   ((uint64_t)mac[2] << 24) |
                   ((uint64_t)mac[3] << 16) |
                   ((uint64_t)mac[4] << 8) |
                   (uint64_t)mac[5];
  return mac64;
}