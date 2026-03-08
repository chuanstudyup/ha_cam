/* Web Server Component
 *
 * This component provides HTTP server functionality including:
 * - MJPG video streaming
 * - Static image capture
 * - Web interface
 * - Camera configuration
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "cJSON.h"
#include "esp_http_server.h"

#include "Camera.h"
#include "vCenter.h"
#include "WebServer.h"

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
 * @brief 获取配置HTTP处理函数
 * /config?cfg=sensor - 获取传感器配置
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
            if (strcmp(param, "sensor") == 0)
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

    if (httpd_start(&stream_httpd, &config) == ESP_OK)
    {
        httpd_register_uri_handler(stream_httpd, &uri_get);
        httpd_register_uri_handler(stream_httpd, &picture_get);
        httpd_register_uri_handler(stream_httpd, &index_get);
        httpd_register_uri_handler(stream_httpd, &config_get);
        httpd_register_uri_handler(stream_httpd, &config_set);

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
