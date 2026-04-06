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
#include "esp_netif_sntp.h"

#include "ChipInfo.h"
#include "Camera.h"
#include "vCenter.h"
#include "EasyRTSPServer.h"
#include "Utils.h"
#include "storage.h"
#include "ha_mqtt_client.h"
#include "WebServer.h"
#include "paramCenter.h"
#include "MotionDetect.h"

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

static bool time_sntp_init(void)
{
    // Initialize SNTP
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&config);
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000)) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to update system time within 10s timeout");
        return false;
    }
    else
    {
        // set timezones
        setenv("TZ", "CST-8", 1); // Set timezone to China Standard Time (CST) which is UTC+8
        tzset();                  // Apply the timezone setting
        // Get the current time and print it
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        ESP_LOGI(TAG, "System time updated successfully. Current time: %s", asctime(&timeinfo));
        return true;
    }
}

#include "utilsFS.h"

static TaskHandle_t save_picture_to_sdcardHandle = NULL;

// 创建一个任务，定周期保存图片到SD卡
void save_picture_to_sdcard(void *pvParameters)
{
    // 首先判断文件夹YYYY-MM-DD是否存在，不存在则创建
    char folder_name[64] = {0};
    char file_name[128] = {0};
    int64_t tm1 = 0, tm2 = 0;

    // 从参数中获取周期
    int period = *(int *)pvParameters;

    while (1)
    {
        tm1 = esp_timer_get_time(); // us

        // 判断文件夹是否存在，不存在则创建
        dateFormat(folder_name, sizeof(folder_name), true); // true表示创建当天的文件夹
        if (access(folder_name, F_OK) != 0)
        {
            if (mkdir(folder_name, 0777) != 0)
            {
                ESP_LOGE(TAG, "Failed to create date folder");
                return;
            }
            else
            {
                ESP_LOGI(TAG, "Created folder: %s", folder_name);
            }
        }
        

        // 获取当前时间，生成文件名
        dateFormat(file_name, sizeof(file_name), false); // true表示生成带有时间戳的文件名
        strcat(file_name, ".jpg");                       // 添加.jpg后缀

        // 从摄像头获取一帧图像数据
        video_node *node = get_latest_video_frame();
        if (!node)
        {
            ESP_LOGE(TAG, "Failed to get video frame");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        // 将图像数据保存到SD卡
        FILE *f = fopen(file_name, "wb");
        if (f == NULL)
        {
            ESP_LOGE(TAG, "Failed to open file for writing: %s", file_name);
            put_video_frame(node);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        fwrite(node->data, 1, node->size, f);
        fclose(f);
        put_video_frame(node);
        tm2 = esp_timer_get_time(); // us

        ESP_LOGI(TAG, "Saved picture to SD card: %s, time: %lld ms", file_name, (tm2 - tm1) / 1000);
        // 等待下一个周期
        vTaskDelay(period / portTICK_PERIOD_MS);
    }
}

/**
 * @brief 启动RTSP服务器
 */
void start_rtsp_server(void)
{
    if (get_param_bool(CONFIG_RTSP_SERVER, RTSP_SERVER_ENABLE))
    {
        RTSPServer* server = RTSPServer_Create();
        RTSPServer_Start(server, get_param_int32(CONFIG_RTSP_SERVER, RTSP_SERVER_PORT));
        char *user = get_param_string(CONFIG_RTSP_SERVER, RTSP_SERVER_USER);
        char *password = get_param_string(CONFIG_RTSP_SERVER, RTSP_SERVER_PASSWORD);
        if (strlen(user) > 0 && strlen(password) > 0)
        {
            RTSPServer_SetAuthAccount(server, user, password);
        }
        ESP_LOGI(TAG, "RTSP Server Started on port %d", get_param_int32(CONFIG_RTSP_SERVER, RTSP_SERVER_PORT));
    }
}

/**
 * @brief 重启RTSP服务器（用于配置变更后）
 */
void restart_rtsp_server(void)
{
    // Stop existing server
    RTSPServer* server = RTSPServer_GetInstance();
    if (server != NULL)
    {
        RTSPServer_Destory(server);
        ESP_LOGI(TAG, "RTSP Server stopped");
    }

    // Start with new config
    start_rtsp_server();
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

    time_sntp_init();

    // check if SD card is mounted
    if (!sdcard_init())
    {
        ESP_LOGE(TAG, "sdcard_init failed");
        return;
    }
    ESP_LOGI(TAG, "SD Card Mounted Successfully");

    if (!configCenterInit())
    {
        ESP_LOGE(TAG, "Config Center Init Failed");
        return;
    }

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

    web_server_start();

    // start rtsp server
    start_rtsp_server();

    xTaskCreate(save_picture_to_sdcard, "save_picture_to_sdcard", 8192, (void *)&(int){360000}, 5, &save_picture_to_sdcardHandle);

    // 初始化运动检测
    setNightSwitch(get_param_uint8(CONFIG_MOTION, MD_NIGHT_SWITCH));
    setDetectSensitivity(get_param_uint8(CONFIG_MOTION, MD_SENSITIVITY));
    changeMotionDetectStatus(get_param_bool(CONFIG_MOTION, MD_ENABLE));

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
