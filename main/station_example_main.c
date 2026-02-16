/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_timer.h"

#include "ChipInfo.h"
#include "Camera.h"
#include "vCenter.h"

#include "ha_mqtt_client.h"

/* The examples use WiFi configuration that you can set via project configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_ESP_WIFI_SSID CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_MAXIMUM_RETRY CONFIG_ESP_MAXIMUM_RETRY

#if CONFIG_ESP_STATION_EXAMPLE_WPA3_SAE_PWE_HUNT_AND_PECK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#define EXAMPLE_H2E_IDENTIFIER ""
#elif CONFIG_ESP_STATION_EXAMPLE_WPA3_SAE_PWE_HASH_TO_ELEMENT
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#elif CONFIG_ESP_STATION_EXAMPLE_WPA3_SAE_PWE_BOTH
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#endif
#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static const char *TAG = "wifi station";

static int s_retry_num = 0;

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        }
        else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "connect to the AP fail");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (password len => 8).
             * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
             * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
             * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
             */
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
            .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    }
    else
    {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

#include "esp_http_server.h"

#define PART_BOUNDARY "123abc321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

esp_err_t jpg_stream_httpd_handler(httpd_req_t *req)
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

esp_err_t picture_httpd_handler(httpd_req_t *req)
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

#define MAX_STREAMS 3
TaskHandle_t sustainHandle[MAX_STREAMS];
typedef struct _httpd_sustain_req_t
{
    httpd_req_t *req;
    uint8_t taskNum;
    char activity[16];
    bool inUse;
} httpd_sustain_req_t;

httpd_sustain_req_t sustainReq[MAX_STREAMS] = {0};
#define AUX_STRUCT_SIZE 2048 // size of http request aux data - sizeof(struct httpd_req_aux) = 1108 in esp_http
SemaphoreHandle_t mutex;

esp_err_t index_html_handler(httpd_req_t *req)
{
    extern const char index_html_start[] asm("_binary_index_html_start");
    extern const char index_html_end[] asm("_binary_index_html_end");
    size_t index_html_len = index_html_end - index_html_start;

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, index_html_start, index_html_len);
    return ESP_OK;
}

esp_err_t httpd_handler(httpd_req_t *req)
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

httpd_handle_t setup_server(void)
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

    if (httpd_start(&stream_httpd, &config) == ESP_OK)
    {
        httpd_register_uri_handler(stream_httpd, &uri_get);
        httpd_register_uri_handler(stream_httpd, &picture_get);
        httpd_register_uri_handler(stream_httpd, &index_get);
    }

    return stream_httpd;
}

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



void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    if (CONFIG_LOG_MAXIMUM_LEVEL > CONFIG_LOG_DEFAULT_LEVEL)
    {
        /* If you only want to open more logs in the wifi module, you need to make the max level greater than the default level,
         * and call esp_log_level_set() before esp_wifi_init() to improve the log level of the wifi module. */
        esp_log_level_set("wifi", CONFIG_LOG_MAXIMUM_LEVEL);
    }

    chip_info();

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();

    if (ESP_OK != init_camera())
    {
        ESP_LOGE(TAG, "Camera Init Failed");
        return;
    }

    if (ESP_OK != test_camera())
    {
        ESP_LOGE(TAG, "Camera Test Failed");
        return;
    }

    if (!init_video_center())
    {
        ESP_LOGE(TAG, "Video Center Init Failed");
        return;
    }

    setup_server();
    start_sustainTasks();

    checkMemory("init complete");

#ifdef CONFIG_ENABLE_MQTT
    mqtt_app_start();
    int count = 0;
    while (1)
    {
        mqtt_publish_data();
        vTaskDelay(60000 / portTICK_PERIOD_MS);
        if (count % 6 == 0)
        {
            mqtt_broadcast_discovery();
        }
        count++;
    }
#endif
}
