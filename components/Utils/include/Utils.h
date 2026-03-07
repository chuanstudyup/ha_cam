#ifndef __UTILS_H__
#define __UTILS_H__

#include "esp_http_server.h"
#include "esp_wifi.h"

#define SF_LEN (256)
#define CHUNKSIZE (4096)

// onepage 1MB，1048576字节
#define ONEMEG (1048576)

#define IN_FILE_NAME_LEN (128)
#ifndef FILE_NAME_LEN
#define FILE_NAME_LEN (288)
#endif

extern char startupFailure[];

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
void urlDecode(char *variable);

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
esp_err_t extractQueryKeyVal(httpd_req_t *req, char *variable, char *value);


/**
 * @brief 将ESP错误代码转换为可读字符串
 *
 * @param errCode ESP错误代码，类型为esp_err_t
 * @return const char* 指向错误描述字符串的指针
 *
 * @note 使用静态缓冲区存储错误字符串，非线程安全
 * @see https://github.com/espressif/esp-idf/blob/master/components/esp_common/include/esp_err.h
 */
const char *espErrMsg(esp_err_t errCode);

/**
 * @brief 替换字符串中的指定字符
 *
 * 将字符串中所有出现的指定字符替换为另一个字符
 *
 * @param s 目标字符串，将被原地修改
 * @param c 需要被替换的字符
 * @param r 替换后的字符
 */
void replaceChar(char* s, char c, char r);

/**
 * @brief 将系统时间同步到浏览器时间
 * 
 * 如果系统时间与浏览器时间不同步，则使用浏览器提供的UTC时间戳同步系统时间。
 * 同步后会调用showLocalTime函数显示本地时间。
 * 
 * @param browserUTC 浏览器提供的UTC时间戳（秒数）
 */
void syncToBrowser(uint32_t browserUTC);

/**
 * 格式化文件大小为人类可读的字符串
 *
 * 根据数值大小自动选择适当的单位(bytes/KB/MB/GB)进行格式化
 *
 * @param sizeVal 要格式化的字节大小值
 * @return 返回格式化后的字符串指针(静态缓冲区，非线程安全)
 *         格式示例: "123 bytes", "456KB", "7.8MB", "9.1GB"
 */
char *fmtSize(uint64_t sizeVal);

esp_err_t fileHandler(httpd_req_t *req, const char *fileName, bool download);

/**
 * @brief 获取当前网络接口的MAC地址
 *
 * 该函数通过ESP32的WiFi接口获取当前STA模式的MAC地址，
 * 并将其格式化为标准的XX:XX:XX:XX:XX:XX字符串形式。
 *
 * @return char* 指向静态字符串缓冲区的指针，包含格式化后的MAC地址字符串
 */
char *netMacAddress();

/**
 * @brief 获取当前WiFi连接的RSSI信号强度值
 *
 * 该函数用于获取设备当前连接的WiFi接入点的接收信号强度指示值(RSSI)。
 * RSSI值表示信号强度，数值越大表示信号越强（越接近0表示信号越好）。
 *
 * @return int 返回当前的RSSI值，单位为dBm。如果获取失败则返回0。
 *             正常情况下，RSSI值为负整数（如：-30, -50, -70等）
 */
int netRSSI();

/**
 * @brief 获取当前网络模式
 *
 * 获取ESP32设备当前的WiFi工作模式
 *
 * @return wifi_mode_t 返回当前的WiFi模式，如果获取失败返回WIFI_MODE_NULL
 */
wifi_mode_t netMode();

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
uint64_t getEfuseMac();

/**
 * 获取本地网络的IP地址、子网掩码和默认网关
 *
 * @param ipAddr 存储IP地址的缓冲区，长度至少为16字节
 * @param ipMask 存储子网掩码的缓冲区，长度至少为16字节  
 * @param gwIp 存储默认网关的缓冲区，长度至少为16字节
 */
void netLocalIP(char ipAddr[16], char ipMask[16], char gwIp[16]);
#endif
