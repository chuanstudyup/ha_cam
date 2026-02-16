#include <stdio.h>

#include "ha_mqtt_client.h"

#ifdef CONFIG_ENABLE_MQTT

#include "esp_log.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "esp_timer.h"

#include "vCenter.h"

#define TAG "HA_MQTT"

#define EXAMPLE_ESP_MQTT_HOST "192.168.31.158" // MQTT服务器IP地址
#define EXAMPLE_ESP_MQTT_PORT 1883             // MQTT服务器端口（默认非加密端口）

static uint8_t l_mac[6] = {0};
static esp_mqtt_client_handle_t l_client = NULL;
static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0)
    {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

// 广播设备，homeassistant的设备发现协议，用于在homeassistant中自动发现设备
// 发布到主题：homeassistant/sensor/hum_sensor/config
void mqtt_broadcast_discovery()
{
    int msg_id;

    //char *cam_paylioad = "{\"topic\":\"homeassistant/camera/bedroomCamera/jpeg\",\"image_encoding \":\"b64\",\"unique_id\":\"cam01ae\",\"device\":{\"identifiers\":[\"bedroomCamera\"],\"name\":\"Camera\"}}";
    char *cam_paylioad = "{\"topic\":\"homeassistant/camera/bedroomCamera/jpeg\",\"unique_id\":\"cam01ae\",\"device\":{\"identifiers\":[\"bedroomCamera\"],\"name\":\"Camera\"}}";
    msg_id = esp_mqtt_client_publish(l_client,
                                     "homeassistant/camera/cam0123/config",
                                     cam_paylioad, 0, 1, 0);
    ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
}

void mqtt_publish_data()
{
    size_t _jpg_buf_len = 0;
    uint8_t *_jpg_buf = NULL;
    video_node *node = get_latest_video_frame();
    int width = 0, height = 0;
    get_camera_frame_dimension(&width, &height);
    int64_t t1 = esp_timer_get_time();

    if (!node)
    {
        ESP_LOGE(TAG, "Camera capture failed");
        return;
    }

    if (node->format != PIXFORMAT_JPEG)
    {
        bool jpeg_converted = fmt2jpg(node->data, node->size, width, height, node->format, 80, &_jpg_buf, &_jpg_buf_len);
        if (!jpeg_converted)
        {
            ESP_LOGE(TAG, "JPEG compression failed");
            return;
        }
    }
    else
    {
        _jpg_buf_len = node->size;
        _jpg_buf = node->data;
    }

    int msg_id = esp_mqtt_client_publish(l_client, "homeassistant/camera/bedroomCamera/jpeg", (const char *)_jpg_buf, _jpg_buf_len, 1, 0);

    put_video_frame(node);
    if (node->format != PIXFORMAT_JPEG)
    {
        free(_jpg_buf);
    }
    int64_t t2 = esp_timer_get_time();
    ESP_LOGI(TAG, "mqtt send picture JPG: %uKB %ums, msg_id=%d", _jpg_buf_len/ 1024, (t2 - t1) / 1000, msg_id);
}

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        mqtt_broadcast_discovery();
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

void mqtt_app_start()
{
    if (l_mac[0] == 0)
    {
        esp_wifi_get_mac(WIFI_IF_STA, l_mac);
    }

    char username[32] = {0};
    snprintf(username, sizeof(username), "sensor_%02X%02X", l_mac[4], l_mac[5]);
    char client_id[32] = {0};
    snprintf(client_id, sizeof(username), "esp_sensor_%02X%02X", l_mac[4], l_mac[5]);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.hostname = EXAMPLE_ESP_MQTT_HOST,
        .broker.address.port = EXAMPLE_ESP_MQTT_PORT,
        .broker.address.transport = MQTT_TRANSPORT_OVER_TCP,
        .credentials.username = username,
        .credentials.client_id = client_id,
        .credentials.set_null_client_id = false,
    };

    l_client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(l_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(l_client);
}
#endif /* CONFIG_ENABLE_MQTT  */
