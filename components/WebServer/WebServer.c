/* Web Server Component
 *
 * This component provides HTTP server functionality including:
 * - MJPG video streaming
 * - Static image capture
 * - Web interface
 * - Camera configuration
 */
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_system.h"
#include "freertos/event_groups.h"

#include "Camera.h"
#include "vCenter.h"
#include "utilsFS.h"
#include "storage.h"
#include "WebServer.h"
#include "Utils.h"

static const char *TAG = "WebServer";

#define PART_BOUNDARY "123abc321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

#define MAX_STREAMS 3

typedef struct _httpd_sustain_req_t
{
    httpd_req_t *req;
    uint8_t taskNum;
    char activity[16];
    bool inUse;
} httpd_sustain_req_t;

static TaskHandle_t sustainHandle[MAX_STREAMS];
static httpd_sustain_req_t sustainReq[MAX_STREAMS] = {0};
#define AUX_STRUCT_SIZE 2048 // size of http request aux data - sizeof(struct httpd_req_aux) = 1108 in esp_http
static SemaphoreHandle_t mutex;

// OTA状态
static bool ota_in_progress = false;
static int ota_progress = 0;
static char ota_message[256] = {0};

#define MIN(a, b) ((a) < (b) ? (a) : (b))

/**
 * @brief MJPG流HTTP处理函数
 */
static esp_err_t jpg_stream_httpd_handler(httpd_req_t *req)
{
    video_node *node = NULL;
    unsigned int last_timestamp = 0;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t *_jpg_buf = NULL;
    char *part_buf[64] = {0};
    int64_t tm1 = 0, tm2 = 0;
    float avg_fps = 0;
    float avg_bandwidth = 0;
    int64_t count = 0;
    int width = 0, height = 0;
    get_camera_frame_dimension(&width, &height);

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK)
    {
        return res;
    }

    tm1 = esp_timer_get_time();
    while (true)
    {
        node = get_video_frame(last_timestamp);
        if (!node)
        {
            ESP_LOGW(TAG, "Camera capture failed");
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }
        last_timestamp = node->timestamp + 1;
        if (node->format != PIXFORMAT_JPEG)
        {
            bool jpeg_converted = fmt2jpg(node->data, node->size, width, height, node->format, 80, &_jpg_buf, &_jpg_buf_len);
            if (!jpeg_converted)
            {
                ESP_LOGE(TAG, "JPEG compression failed");
                put_video_frame(node);
                res = ESP_FAIL;
            }
        }
        else
        {
            _jpg_buf_len = node->size;
            _jpg_buf = node->data;
        }

        if (res == ESP_OK)
        {
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        }
        if (res == ESP_OK)
        {
            size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);

            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        }
        if (res == ESP_OK)
        {
            res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
        }
        if (node->format != PIXFORMAT_JPEG)
        {
            free(_jpg_buf);
        }
        put_video_frame(node);
        if (res != ESP_OK)
        {
            break;
        }
        tm2 = esp_timer_get_time();
        int64_t frame_time = tm2 - tm1;
        tm1 = tm2;

        // Calculate average FPS and bandwidth
        avg_fps = 0.7 * avg_fps + 0.3 * (1000000.0 / (float)frame_time);
        avg_bandwidth = 0.7 * avg_bandwidth + 0.3 * (_jpg_buf_len * 8.0 / (float)frame_time * 1000.0); // in Kbps
        count++;
        if (count % 10 == 0)
        {
            ESP_LOGI(TAG, "MJPG: %.2ffps %.2fKbps", avg_fps, avg_bandwidth);
        }
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }

    return res;
}

/**
 * @brief 静态图片HTTP处理函数
 */
static esp_err_t picture_httpd_handler(httpd_req_t *req)
{
    size_t _jpg_buf_len = 0;
    uint8_t *_jpg_buf = NULL;
    int64_t t1 = esp_timer_get_time();
    video_node *node = get_latest_video_frame();
    int width = 0, height = 0;
    get_camera_frame_dimension(&width, &height);

    if (!node)
    {
        ESP_LOGE(TAG, "Camera capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    if (node->format != PIXFORMAT_JPEG)
    {
        bool jpeg_converted = fmt2jpg(node->data, node->size, width, height, node->format, 80, &_jpg_buf, &_jpg_buf_len);
        if (!jpeg_converted)
        {
            ESP_LOGE(TAG, "JPEG compression failed");
            httpd_resp_send_500(req);
            put_video_frame(node);
            return ESP_FAIL;
        }
    }
    else
    {
        _jpg_buf_len = node->size;
        _jpg_buf = node->data;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=\"picture.jpg\"");
    httpd_resp_send(req, (const char *)_jpg_buf, _jpg_buf_len);
    put_video_frame(node);
    if (node->format != PIXFORMAT_JPEG)
    {
        free(_jpg_buf);
    }
    int64_t t2 = esp_timer_get_time();
    ESP_LOGI(TAG, "JPG: %uKB %ums", (uint32_t)(_jpg_buf_len / 1024), (uint32_t)((t2 - t1) / 1000));

    return ESP_OK;
}

/**
 * @brief 首页HTML处理函数
 */
static esp_err_t index_html_handler(httpd_req_t *req)
{
    extern const char index_html_start[] asm("_binary_index_html_start");
    extern const char index_html_end[] asm("_binary_index_html_end");
    size_t index_html_len = index_html_end - index_html_start;

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, index_html_start, index_html_len);
    return ESP_OK;
}

/**
 * @brief OTA页面HTTP处理函数
 */
static esp_err_t ota_html_handler(httpd_req_t *req)
{
    extern const char ota_html_start[] asm("_binary_ota_html_start");
    extern const char ota_html_end[] asm("_binary_ota_html_end");
    size_t ota_html_len = ota_html_end - ota_html_start;

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, ota_html_start, ota_html_len);
    return ESP_OK;
}

/**
 * @brief 获取配置HTTP处理函数
 * /config?cfg=image - 获取图像配置
 * /config?cap=framesizes - 获取支持的帧大小
 */
static esp_err_t get_config_html_handler(httpd_req_t *req)
{
    // 处理GET请求
    char cfg[32] = {0};
    if (httpd_req_get_url_query_str(req, cfg, sizeof(cfg)) == ESP_OK)
    {
        char param[16] = {0};
        if (httpd_query_key_value(cfg, "cfg", param, sizeof(param)) == ESP_OK)
        {
            if (strcmp(param, "image") == 0)
            {
                char *json_str = get_camera_sensor_settings_json();
                if (json_str)
                {
                    httpd_resp_set_type(req, "application/json");
                    httpd_resp_send(req, json_str, strlen(json_str));
                    free(json_str);
                    return ESP_OK;
                }
            }
        }
        if (httpd_query_key_value(cfg, "cap", param, sizeof(param)) == ESP_OK)
        {
            if (strcmp(param, "framesizes") == 0)
            {
                char *json_str = get_camera_supported_framesizes_json();
                if (json_str)
                {
                    httpd_resp_set_type(req, "application/json");
                    httpd_resp_send(req, json_str, strlen(json_str));
                    free(json_str);
                    return ESP_OK;
                }
            }
        }
    }
    httpd_resp_set_status(req, "500 Query data error");
    httpd_resp_sendstr(req, NULL);

    return ESP_OK;
}

/**
 * @brief 设置配置HTTP处理函数
 * POST请求，接收JSON配置并应用
 */
static esp_err_t set_config_html_handler(httpd_req_t *req)
{
    // 处理POST请求（配置保存）
    if (req->method == HTTP_POST)
    {
        char buf[256];
        int ret, remaining = req->content_len;

        // 读取请求体
        ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf) - 1));
        if (ret <= 0)
        {
            httpd_resp_set_status(req, "500 Read error");
            httpd_resp_sendstr(req, NULL);
            return ESP_OK;
        }

        buf[ret] = '\0';
        ESP_LOGI(TAG, "Received config: %s", buf);

        // 解析JSON配置
        cJSON *config_json = cJSON_Parse(buf);
        if (config_json)
        {
            esp_err_t result = apply_camera_config(config_json);
            cJSON_Delete(config_json);

            if (result == ESP_OK)
            {
                httpd_resp_set_status(req, "200 OK");
                httpd_resp_sendstr(req, "Configuration applied successfully");
            }
            else
            {
                httpd_resp_set_status(req, "500 Apply error");
                httpd_resp_sendstr(req, "Failed to apply configuration");
            }
        }
        else
        {
            httpd_resp_set_status(req, "400 Parse error");
            httpd_resp_sendstr(req, "Invalid JSON format");
        }
        return ESP_OK;
    }

    return ESP_OK;
}

/**
 * @brief 获取OTA状态处理函数
 * GET /ota - 返回OTA状态和版本信息
 */
static esp_err_t ota_status_handler(httpd_req_t *req)
{
    const esp_app_desc_t *app_desc = esp_ota_get_app_description();
    const esp_partition_t *running_partition = esp_ota_get_running_partition();

    cJSON *root = cJSON_CreateObject();
    cJSON *version = cJSON_CreateString(app_desc->version);
    cJSON *project_name = cJSON_CreateString(app_desc->project_name);
    cJSON *time = cJSON_CreateString(app_desc->time);
    cJSON *date = cJSON_CreateString(app_desc->date);
    cJSON *partition = cJSON_CreateString(running_partition->label);
    cJSON *ota_in_progress_json = cJSON_CreateBool(ota_in_progress);

    cJSON_AddItemToObject(root, "version", version);
    cJSON_AddItemToObject(root, "project_name", project_name);
    cJSON_AddItemToObject(root, "time", time);
    cJSON_AddItemToObject(root, "date", date);
    cJSON_AddItemToObject(root, "partition", partition);
    cJSON_AddItemToObject(root, "ota_in_progress", ota_in_progress_json);

    if (ota_in_progress)
    {
        cJSON *progress = cJSON_CreateNumber(ota_progress);
        cJSON *message = cJSON_CreateString(ota_message);
        cJSON_AddItemToObject(root, "progress", progress);
        cJSON_AddItemToObject(root, "message", message);
    }

    char *json_str = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));

    cJSON_Delete(root);
    free(json_str);

    return ESP_OK;
}

/**
 * @brief OTA更新处理函数
 * POST /update - 接收固件文件并执行更新
 */
static esp_err_t ota_update_handler(httpd_req_t *req)
{
    if (ota_in_progress)
    {
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_sendstr(req, "OTA update already in progress");
        return ESP_OK;
    }

    // 查找OTA分区
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition)
    {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "Failed to find OTA partition");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting OTA update to partition: %s", update_partition->label);

    // 初始化OTA句柄
    esp_ota_handle_t ota_handle = {0};
    esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "OTA initialization failed");
        return ESP_OK;
    }

    // 停止其他任务
    pause_video_center();

    // 此后 无论成功失败都重启
    ota_in_progress = true;
    ota_progress = 0;
    strncpy(ota_message, "Receiving firmware...", sizeof(ota_message) - 1);

    // 接收固件数据
    int total_len = req->content_len;
    int received_len = 0;
    char *ota_buffer = (char *)malloc(4096);
    if (!ota_buffer)
    {
        esp_ota_end(ota_handle);
        ota_in_progress = false;
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "Memory allocation failed");
        
        goto restart;
    }

    int remaining = total_len;
    while (remaining > 0)
    {
        int recv_len = httpd_req_recv(req, ota_buffer, MIN(remaining, 4096));
        if (recv_len < 0)
        {
            ESP_LOGE(TAG, "Error receiving data");
            free(ota_buffer);
            esp_ota_end(ota_handle);
            ota_in_progress = false;
            strncpy(ota_message, "Data receive error", sizeof(ota_message) - 1);
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_sendstr(req, "Error receiving data");
            goto restart;
        }
        else if (recv_len == 0)
        {
            // 连接关闭
            break;
        }

        // 写入OTA分区
        err = esp_ota_write(ota_handle, (const void *)ota_buffer, recv_len);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            free(ota_buffer);
            esp_ota_end(ota_handle);
            ota_in_progress = false;
            strncpy(ota_message, "OTA write failed", sizeof(ota_message) - 1);
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_sendstr(req, "OTA write failed");
            goto restart;
        }

        received_len += recv_len;
        remaining -= recv_len;
        ota_progress = (received_len * 100) / total_len;

        if (received_len % 10 == 0)
        {
            ESP_LOGI(TAG, "OTA progress: %d%%", ota_progress);
        }
    }

    free(ota_buffer);

    // 完成OTA更新
    err = esp_ota_end(ota_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        ota_in_progress = false;
        strncpy(ota_message, "OTA end failed", sizeof(ota_message) - 1);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "OTA end failed");
        goto restart;
    }

    // 设置启动分区
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        ota_in_progress = false;
        strncpy(ota_message, "Set boot partition failed", sizeof(ota_message) - 1);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "Set boot partition failed");
        goto restart;
    }

    ota_progress = 100;
    strncpy(ota_message, "OTA update completed successfully", sizeof(ota_message) - 1);

    ESP_LOGI(TAG, "OTA update completed successfully. Rebooting...");

    // 返回成功响应
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_sendstr(req, "OTA update completed successfully. Rebooting...");

restart:
    // 无论成功失败，都延迟后重启
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    esp_restart();

    return ESP_OK;
}

/**
 * @brief 文件管理页面处理函数
 */
static esp_err_t files_html_handler(httpd_req_t *req)
{
    extern const char files_html_start[] asm("_binary_files_html_start");
    extern const char files_html_end[] asm("_binary_files_html_end");
    size_t files_html_len = files_html_end - files_html_start;

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, files_html_start, files_html_len);
    return ESP_OK;
}

/**
 * @brief 验证路径是否安全（只允许访问/sdcard目录）
 */
static bool is_path_safe(const char *path)
{
    // 只允许访问/sdcard开头的路径
    if (strncmp(path, "/sdcard", 7) != 0)
    {
        return false;
    }
    // 不允许访问系统目录
    if (strstr(path, "/sdcard/System") != NULL || strstr(path, "/sdcard/SYSTEM") != NULL)
    {
        return false;
    }
    return true;
}

/**
 * @brief URL解码函数
 */
static void url_decode(char *str)
{
    char *src = str;
    char *dst = str;
    while (*src)
    {
        if (*src == '%' && *(src + 1) && *(src + 2))
        {
            // URL编码: %XX
            char hex[3] = {*(src + 1), *(src + 2), 0};
            *dst = (char)strtol(hex, NULL, 16);
            src += 3;
        }
        else if (*src == '+')
        {
            // URL编码空格
            *dst = ' ';
            src++;
        }
        else
        {
            *dst = *src;
            src++;
        }
        dst++;
    }
    *dst = '\0';
}

/**
 * @brief 获取文件修改时间
 */
static void get_file_time(const char *path, char *time_str, size_t len)
{
    struct stat st;
    if (stat(path, &st) == 0)
    {
        struct tm *tm = localtime(&st.st_mtime);
        strftime(time_str, len, "%Y-%m-%d %H:%M", tm);
    }
    else
    {
        snprintf(time_str, len, "Unknown");
    }
}

/**
 * @brief 文件列表API处理函数
 * GET /api/files/list?path=/sdcard/xxx
 */
static esp_err_t file_list_handler(httpd_req_t *req)
{
    char path[256] = {0};
    char temp_path[300] = {0};
    char path_param[256] = {0};

    // 获取路径参数
    if (httpd_req_get_url_query_str(req, path, sizeof(path)) != ESP_OK)
    {
        strcpy(path, "/sdcard");
    }
    else
    {
        if (httpd_query_key_value(path, "path", path_param, sizeof(path_param)) == ESP_OK)
        {
            // URL解码
            url_decode(path_param);
            strncpy(path, path_param, sizeof(path) - 1);
        }
        else
        {
            strcpy(path, "/sdcard");
        }
    }

    // 如果路径不以 /sdcard 开头，添加前缀
    if (strncmp(path, "/sdcard", 7) != 0)
    {
        snprintf(temp_path, sizeof(temp_path), "/sdcard%s", path);
        strncpy(path, temp_path, sizeof(path) - 1);
    }

    // 验证路径安全性
    if (!is_path_safe(path))
    {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_sendstr(req, "Access denied");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Listing files in: %s", path);

    // 打开目录
    DIR *dir = opendir(path);
    if (!dir)
    {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_sendstr(req, "Directory not found");
        return ESP_OK;
    }

    // 构建父路径
    char parent_path[256] = {0};
    strncpy(parent_path, path, sizeof(parent_path) - 1);
    char *last_slash = strrchr(parent_path, '/');
    if (last_slash && last_slash != parent_path)
    {
        *last_slash = '\0';
        if (strlen(parent_path) == 7) // /sdcard
        {
            strcpy(parent_path, "/sdcard");
        }
    }
    else
    {
        strcpy(parent_path, "/sdcard");
    }

    // 构建JSON
    cJSON *root = cJSON_CreateObject();
    cJSON *items = cJSON_CreateArray();

    struct dirent *entry;
    struct stat st;
    char full_path[512];

    // 添加父目录
    if (strcmp(path, "/sdcard") != 0)
    {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", "..");
        cJSON_AddStringToObject(item, "type", "dir");
        cJSON_AddStringToObject(item, "path", parent_path);
        cJSON_AddNumberToObject(item, "size", 0);
        cJSON_AddStringToObject(item, "modified", "");
        cJSON_AddItemToArray(items, item);
    }

    while ((entry = readdir(dir)) != NULL)
    {
        // 跳过隐藏文件和系统目录
        if (entry->d_name[0] == '.')
            continue;
        if (strcmp(entry->d_name, "System") == 0 || strcmp(entry->d_name, "SYSTEM") == 0)
            continue;
        if (strcmp(entry->d_name, "data") == 0)
            continue;

        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

        if (stat(full_path, &st) != 0)
            continue;

        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", entry->d_name);
        cJSON_AddStringToObject(item, "type", S_ISDIR(st.st_mode) ? "dir" : "file");
        cJSON_AddStringToObject(item, "path", full_path);
        cJSON_AddNumberToObject(item, "size", st.st_size);

        char time_str[64];
        get_file_time(full_path, time_str, sizeof(time_str));
        cJSON_AddStringToObject(item, "modified", time_str);

        cJSON_AddItemToArray(items, item);

        // ESP_LOGI(TAG, "Found %s: %s (%s) size: %u modified: %s", S_ISDIR(st.st_mode) ? "dir" : "file", entry->d_name, full_path, (uint32_t)st.st_size, time_str);
    }

    closedir(dir);

    cJSON_AddStringToObject(root, "path", path);
    cJSON_AddStringToObject(root, "parent", parent_path);
    cJSON_AddItemToObject(root, "items", items);

    char *json_str = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));

    cJSON_Delete(root);
    free(json_str);

    return ESP_OK;
}

/**
 * @brief 文件下载处理函数
 * GET /api/files/download?path=/sdcard/xxx/xxx.jpg
 */
static esp_err_t file_download_handler(httpd_req_t *req)
{
    char path[512] = {0};
    char path_param[256] = {0};
    char temp_path[128] = {0};

    // 获取路径参数
    if (httpd_req_get_url_query_str(req, path, sizeof(path)) != ESP_OK)
    {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "Missing path parameter");
        return ESP_OK;
    }

    if (httpd_query_key_value(path, "path", temp_path, sizeof(temp_path)) != ESP_OK)
    {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "Missing path parameter");
        return ESP_OK;
    }

    // URL解码
    url_decode(temp_path);

    // 如果路径不以 /sdcard 开头，添加前缀
    if (strncmp(temp_path, "/sdcard", 7) != 0)
    {
        snprintf(path_param, sizeof(path_param), "/sdcard%s", temp_path);
    }
    else
    {
        strncpy(path_param, temp_path, sizeof(path_param) - 1);
    }

    // 验证路径安全性
    if (!is_path_safe(path_param))
    {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_sendstr(req, "Access denied");
        return ESP_OK;
    }

    // 检查文件是否存在
    FILE *df = fopen(path_param, "rb");
    if (!df)
    {
        ESP_LOGW(TAG, "File does not exist or cannot be opened: %s", path_param);
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_sendstr(req, "File not found");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "Downloading file: %s", path_param);

    return downloadFile(path_param, df, req);
}

/**
 * @brief 文件删除处理函数
 * POST /api/files/delete
 * Body: {"paths": ["/sdcard/xxx/1.jpg", "/sdcard/xxx/folder"]}
 */
static esp_err_t file_delete_handler(httpd_req_t *req)
{
    char buf[1024];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0)
    {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "Failed to read request body");
        return ESP_OK;
    }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root)
    {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "Invalid JSON");
        return ESP_OK;
    }

    cJSON *paths = cJSON_GetObjectItem(root, "paths");
    if (!paths || !cJSON_IsArray(paths))
    {
        cJSON_Delete(root);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "Missing paths array");
        return ESP_OK;
    }

    int deleted_count = 0;
    int failed_count = 0;

    cJSON *path_item;
    cJSON_ArrayForEach(path_item, paths)
    {
        const char *path = cJSON_GetStringValue(path_item);
        if (!path)
            continue;

        // 如果路径不以 /sdcard 开头，添加前缀
        char full_path[256];
        if (strncmp(path, "/sdcard", 7) != 0)
        {
            snprintf(full_path, sizeof(full_path), "/sdcard%s", path);
        }
        else
        {
            strncpy(full_path, path, sizeof(full_path) - 1);
        }

        // 验证路径安全性
        if (!is_path_safe(full_path))
        {
            failed_count++;
            continue;
        }

        // 检查是否是系统目录
        if (strcmp(full_path, "/sdcard") == 0 || strcmp(full_path, "/sdcard/System") == 0 || strcmp(full_path, "/sdcard/SYSTEM") == 0)
        {
            failed_count++;
            continue;
        }

        // 调用 deleteFolderOrFile 进行删除
        // 注意：deleteFolderOrFile 内部会使用 setFolderName 处理路径
        deleteFolderOrFile(full_path);
        deleted_count++;
    }

    cJSON_Delete(root);

    // 构建响应
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", failed_count == 0);
    char message[128];
    snprintf(message, sizeof(message), "Deleted %d item(s)", deleted_count);
    if (failed_count > 0)
    {
        snprintf(message, sizeof(message), "Deleted %d item(s), failed %d", deleted_count, failed_count);
    }
    cJSON_AddStringToObject(response, "message", message);

    char *json_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));

    cJSON_Delete(response);
    free(json_str);

    return ESP_OK;
}

/**
 * @brief 创建文件夹处理函数
 * POST /api/files/mkdir
 * Body: {"path": "/sdcard/xxx/newfolder"}
 */
static esp_err_t file_mkdir_handler(httpd_req_t *req)
{
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0)
    {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "Failed to read request body");
        return ESP_OK;
    }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root)
    {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "Invalid JSON");
        return ESP_OK;
    }

    cJSON *path_item = cJSON_GetObjectItem(root, "path");
    if (!path_item || !cJSON_IsString(path_item))
    {
        cJSON_Delete(root);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "Missing path");
        return ESP_OK;
    }

    const char *path = cJSON_GetStringValue(path_item);
    cJSON_Delete(root);

    // 如果路径不以 /sdcard 开头，添加前缀
    char full_path[256];
    if (strncmp(path, "/sdcard", 7) != 0)
    {
        snprintf(full_path, sizeof(full_path), "/sdcard%s", path);
    }
    else
    {
        strncpy(full_path, path, sizeof(full_path) - 1);
    }

    // 验证路径安全性
    if (!is_path_safe(full_path))
    {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_sendstr(req, "Access denied");
        return ESP_OK;
    }

    // 创建目录
    int result = mkdir(full_path, 0777);

    cJSON *response = cJSON_CreateObject();
    if (result == 0 || errno == EEXIST)
    {
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddStringToObject(response, "message", "Folder created successfully");
        ESP_LOGI(TAG, "Created folder: %s", full_path);
    }
    else
    {
        cJSON_AddBoolToObject(response, "success", false);
        cJSON_AddStringToObject(response, "message", "Failed to create folder");
        ESP_LOGE(TAG, "Failed to create folder: %s", full_path);
    }

    char *json_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));

    cJSON_Delete(response);
    free(json_str);

    return ESP_OK;
}

/**
 * @brief 获取存储空间信息
 * GET /api/storage/info
 */
static esp_err_t storage_info_handler(httpd_req_t *req)
{
    uint32_t total_kb = getSDTotalSpace();
    uint32_t free_kb = getSDFreeSpace();
    uint32_t used_kb = total_kb - free_kb;
    uint32_t used_percent = (total_kb > 0) ? (used_kb * 100 / total_kb) : 0;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "total_kb", total_kb);
    cJSON_AddNumberToObject(root, "free_kb", free_kb);
    cJSON_AddNumberToObject(root, "used_kb", used_kb);
    cJSON_AddNumberToObject(root, "used_percent", used_percent);

    char *json_str = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));

    cJSON_Delete(root);
    free(json_str);

    return ESP_OK;
}

/**
 * @brief HTTP通用处理函数
 * 负责分发请求到对应的sustain任务
 */
static esp_err_t httpd_handler(httpd_req_t *req)
{
    int i = 0;
    xSemaphoreTake(mutex, portMAX_DELAY);
    for (i = 0; i < MAX_STREAMS; i++)
    {
        if (!sustainReq[i].inUse)
        {
            sustainReq[i].inUse = true;
            sustainReq[i].req = (httpd_req_t *)malloc(sizeof(httpd_req_t));
            memcpy(sustainReq[i].req, req, sizeof(httpd_req_t));
            sustainReq[i].req->aux = malloc(AUX_STRUCT_SIZE);
            memcpy(sustainReq[i].req->aux, req->aux, AUX_STRUCT_SIZE);

            strncpy(sustainReq[i].activity, (char *)req->user_ctx, sizeof(sustainReq[i].activity) - 1);
            xSemaphoreGive(mutex);
            xTaskNotifyGive(sustainHandle[i]);
            return ESP_OK;
        }
    }
    xSemaphoreGive(mutex);

    if (i == MAX_STREAMS)
    {
        ESP_LOGE(TAG, "No available sustain task for %s", (char *)req->user_ctx);
        httpd_resp_set_status(req, "500 No free task");
    }
    httpd_resp_sendstr(req, NULL);
    return ESP_FAIL;
}

/**
 * @brief 持久化任务，处理长时间HTTP请求
 */
static void sustainTask(void *p)
{
    // process sustained http(s) requests as a separate task
    while (true)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        uint8_t i = *(uint8_t *)p; // identify task number

        if (!strcmp(sustainReq[i].activity, "stream"))
        {
            jpg_stream_httpd_handler(sustainReq[i].req);
        }
        else if (!strcmp(sustainReq[i].activity, "picture"))
        {
            picture_httpd_handler(sustainReq[i].req);
        }

        free(sustainReq[i].req->aux);
        free(sustainReq[i].req);
        sustainReq[i].req = NULL;

        xSemaphoreTake(mutex, portMAX_DELAY);
        sustainReq[i].inUse = false;
        xSemaphoreGive(mutex);
    }
    vTaskDelete(NULL);
}

/**
 * @brief 启动持久化任务
 */
static void start_sustainTasks(void)
{
    mutex = xSemaphoreCreateMutex();
    for (uint8_t i = 0; i < MAX_STREAMS; i++)
    {
        sustainHandle[i] = NULL;
        sustainReq[i].inUse = false;
        sustainReq[i].taskNum = i;
        char taskName[16];
        snprintf(taskName, sizeof(taskName), "sustainTask%d", i);
        xTaskCreate(sustainTask, taskName, 4096, &sustainReq[i].taskNum, 5, &sustainHandle[i]);
    }
}

httpd_handle_t web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t stream_httpd = NULL;
    config.max_uri_handlers = 16; // 增加URI处理器数量以支持更多功能
    config.stack_size = 8192;     // 增加堆栈大小以处理更复杂的请求

    httpd_uri_t uri_get = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = httpd_handler,
        .user_ctx = "stream"};

    httpd_uri_t picture_get = {
        .uri = "/picture",
        .method = HTTP_GET,
        .handler = httpd_handler,
        .user_ctx = "picture"};

    httpd_uri_t index_get = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_html_handler,
        .user_ctx = NULL};

    httpd_uri_t config_get = {
        .uri = "/config",
        .method = HTTP_GET,
        .handler = get_config_html_handler,
        .user_ctx = NULL};

    httpd_uri_t config_set = {
        .uri = "/config",
        .method = HTTP_POST,
        .handler = set_config_html_handler,
        .user_ctx = NULL};

    httpd_uri_t ota_html_get = {
        .uri = "/ota.html",
        .method = HTTP_GET,
        .handler = ota_html_handler,
        .user_ctx = NULL};

    httpd_uri_t ota_status_get = {
        .uri = "/ota",
        .method = HTTP_GET,
        .handler = ota_status_handler,
        .user_ctx = NULL};

    httpd_uri_t ota_update_post = {
        .uri = "/update",
        .method = HTTP_POST,
        .handler = ota_update_handler,
        .user_ctx = NULL};

    httpd_uri_t files_html_get = {
        .uri = "/files.html",
        .method = HTTP_GET,
        .handler = files_html_handler,
        .user_ctx = NULL};

    httpd_uri_t api_files_list = {
        .uri = "/api/files/list",
        .method = HTTP_GET,
        .handler = file_list_handler,
        .user_ctx = NULL};

    httpd_uri_t api_files_download = {
        .uri = "/api/files/download",
        .method = HTTP_GET,
        .handler = file_download_handler,
        .user_ctx = NULL};

    httpd_uri_t api_files_delete = {
        .uri = "/api/files/delete",
        .method = HTTP_POST,
        .handler = file_delete_handler,
        .user_ctx = NULL};

    httpd_uri_t api_files_mkdir = {
        .uri = "/api/files/mkdir",
        .method = HTTP_POST,
        .handler = file_mkdir_handler,
        .user_ctx = NULL};

    httpd_uri_t api_storage_info = {
        .uri = "/api/storage/info",
        .method = HTTP_GET,
        .handler = storage_info_handler,
        .user_ctx = NULL};

    if (httpd_start(&stream_httpd, &config) == ESP_OK)
    {
        httpd_register_uri_handler(stream_httpd, &uri_get);
        httpd_register_uri_handler(stream_httpd, &picture_get);
        httpd_register_uri_handler(stream_httpd, &index_get);
        httpd_register_uri_handler(stream_httpd, &config_get);
        httpd_register_uri_handler(stream_httpd, &config_set);

        httpd_register_uri_handler(stream_httpd, &ota_html_get);
        httpd_register_uri_handler(stream_httpd, &ota_status_get);
        httpd_register_uri_handler(stream_httpd, &ota_update_post);

        httpd_register_uri_handler(stream_httpd, &files_html_get);
        httpd_register_uri_handler(stream_httpd, &api_files_list);
        httpd_register_uri_handler(stream_httpd, &api_files_download);
        httpd_register_uri_handler(stream_httpd, &api_files_delete);
        httpd_register_uri_handler(stream_httpd, &api_files_mkdir);
        httpd_register_uri_handler(stream_httpd, &api_storage_info);

        start_sustainTasks();

        ESP_LOGI(TAG, "WebServer started successfully");
    }
    else
    {
        ESP_LOGE(TAG, "Failed to start WebServer");
    }

    return stream_httpd;
}

void web_server_stop(httpd_handle_t server)
{
    if (server)
    {
        httpd_stop(server);
        ESP_LOGI(TAG, "WebServer stopped");
    }
}
