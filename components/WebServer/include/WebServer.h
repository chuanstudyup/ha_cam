#ifndef _WEB_SERVER_H_
#define _WEB_SERVER_H_

#include "esp_err.h"
#include "esp_http_server.h"

/**
 * @brief 启动Web服务器
 *
 * 初始化并启动HTTP服务器，支持以下功能：
 * - MJPG视频流 (/stream)
 * - 静态图片获取 (/picture)
 * - Web界面首页 (index.html)
 * - 相机配置获取和设置 (/config)
 *
 * @return httpd_handle_t 成功返回服务器句柄，失败返回NULL
 */
httpd_handle_t web_server_start(void);

/**
 * @brief 停止Web服务器
 *
 * 停止并清理HTTP服务器资源
 *
 * @param server 服务器句柄
 */
void web_server_stop(httpd_handle_t server);

#endif /* _WEB_SERVER_H_ */
