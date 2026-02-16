#include <stdio.h>
#include "Camera.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "camera_pins.h"

#define TAG "Camera"

#define NVS_SENSOR_KEY "nvs_sen"

static framesize_t l_frameSize = FRAMESIZE_XGA;

// indexed by frame size - needs to be consistent with sensor.h framesize_t enum
const frameStruct frameData[] = {
    {"96X96", 96, 96, 30, 1, 1}, // 2MP sensors
    {"QQVGA", 160, 120, 30, 1, 1},
    {"128X128", 128, 128, 30, 1, 1},
    {"QCIF", 176, 144, 30, 1, 1},
    {"HQVGA", 240, 176, 30, 2, 1},
    {"240X240", 240, 240, 30, 2, 1},
    {"QVGA", 320, 240, 30, 2, 1},
    {"320X320", 320, 320, 30, 2, 1},
    {"CIF", 400, 296, 25, 2, 1},
    {"HVGA", 480, 320, 25, 2, 1},
    {"VGA", 640, 480, 20, 3, 1},
    {"SVGA", 800, 600, 20, 3, 1},
    {"XGA", 1024, 768, 15, 3, 1},
    {"HD", 1280, 720, 15, 3, 1},
    {"SXGA", 1280, 1024, 10, 3, 1},
    {"UXGA", 1600, 1200, 10, 4, 1},
    {"FHD", 1920, 1080, 5, 3, 1}, // 3MP Sensors
    {"P_HD", 720, 1280, 5, 3, 1},
    {"P_3MP", 864, 1536, 5, 3, 1},
    {"QXGA", 2048, 1536, 5, 4, 1},
    {"QHD", 2560, 1440, 5, 4, 1}, // 5MP Sensors
    {"WQXGA", 2560, 1600, 5, 4, 1},
    {"P_FHD", 1080, 1920, 5, 4, 1},
    {"QSXGA", 2560, 1920, 4, 4, 1},
    {"5MP", 2592, 1944, 4, 4, 1}};

esp_err_t init_camera(void)
{
#if ESP_CAMERA_SUPPORTED
    // initialize the camera
    camera_config_t camera_config = {
        .pin_pwdn = PWDN_GPIO_NUM,
        .pin_reset = RESET_GPIO_NUM,
        .pin_xclk = XCLK_GPIO_NUM,
        .pin_sccb_sda = SIOD_GPIO_NUM,
        .pin_sccb_scl = SIOC_GPIO_NUM,

        .pin_d7 = Y9_GPIO_NUM,
        .pin_d6 = Y8_GPIO_NUM,
        .pin_d5 = Y7_GPIO_NUM,
        .pin_d4 = Y6_GPIO_NUM,
        .pin_d3 = Y5_GPIO_NUM,
        .pin_d2 = Y4_GPIO_NUM,
        .pin_d1 = Y3_GPIO_NUM,
        .pin_d0 = Y2_GPIO_NUM,
        .pin_vsync = VSYNC_GPIO_NUM,
        .pin_href = HREF_GPIO_NUM,
        .pin_pclk = PCLK_GPIO_NUM,

        // XCLK 20MHz or 10MHz for OV2640 double FPS (Experimental)
        .xclk_freq_hz = 20000000,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,

        .pixel_format = PIXFORMAT_JPEG, // YUV422,GRAYSCALE,RGB565,JPEG
        .frame_size = l_frameSize,      // QQVGA-UXGA, For ESP32, do not use sizes above QVGA when not JPEG. The performance of the ESP32-S series has improved a lot, but JPEG mode always gives better frame rates.

        .jpeg_quality = 12, // 0-63, for OV series camera sensors, lower number means higher quality
        .fb_count = FB_CNT, // When jpeg mode is used, if fb_count more than one, the driver will work in continuous mode.
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
    };

    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Camera Init Failed");
        return err;
    }

    if (ESP_OK != esp_camera_load_from_nvs(NVS_SENSOR_KEY))
    {
        ESP_LOGW(TAG, "load sensor nvs failed");
    }

    sensor_t *sen = esp_camera_sensor_get();
    camera_status_t* status = &(sen->status);
    l_frameSize = status->framesize;

    camera_sensor_info_t *info = esp_camera_sensor_get_info(&sen->id);
    ESP_LOGI(TAG, "Use sensor %s, %d, %d. Max Size: %d, %s support jpeg",
             info->name, info->pid, info->model, info->max_size, info->support_jpeg ? "" : "not");

    return ESP_OK;
#else
    ESP_LOGE(TAG, "Camera not supported on this board");
    return ESP_FAIL;
#endif
}

/**
 * @brief 获取传感器型号名称
 *
 * 通过ESP32相机传感器接口获取当前相机的传感器信息，并返回传感器型号名称。
 *
 * @return char* 指向传感器型号名称字符串的指针
 */
const char *get_seneor_model_name(void)
{
    sensor_t *sen = esp_camera_sensor_get();
    if (!sen)
    {
        ESP_LOGE(TAG, "Sensor not found");
        return "No Sensor";
    }
    camera_sensor_info_t *info = esp_camera_sensor_get_info(&sen->id);
    return info->name;
}

/* try to read a frame to test camera */
esp_err_t test_camera(void)
{
    int64_t tm = esp_timer_get_time();

    if (sizeof(frameData) / sizeof(frameStruct) != FRAMESIZE_INVALID)
    {
        ESP_LOGE(TAG, "frameData define not match enum framesize_t in sensor.h. Please check your code.");
        return ESP_FAIL;
    }

    camera_fb_t *pic = esp_camera_fb_get();
    if (!pic)
    {
        ESP_LOGE(TAG, "Camera capture failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Read a frame %s(%dx%d), size=%d, take %lld ms", pic->format == PIXFORMAT_JPEG ? "JPEG" : "RAW",
             pic->width, pic->height, pic->len, (esp_timer_get_time() - tm) / 1000);

    /* check the frame size */
    if (pic->width != frameData[l_frameSize].frameWidth || pic->height != frameData[l_frameSize].frameHeight)
    {
        ESP_LOGE(TAG, "Camera frame size error, expected %dx%d", frameData[l_frameSize].frameWidth, frameData[l_frameSize].frameHeight);
        esp_camera_fb_return(pic);
        return ESP_FAIL;
    }

    esp_camera_fb_return(pic);

    return ESP_OK;
}

framesize_t get_camera_frame_size(void)
{
    return l_frameSize;
}

void get_camera_frame_dimension(int *width, int *height)
{
    if (width)
    {
        *width = frameData[l_frameSize].frameWidth;
    }
    if (height)
    {
        *height = frameData[l_frameSize].frameHeight;
    }
}

// 获取sensor的分辨率、亮度、对比、饱和、锐度、质量，封装成json的形式
#include "cJSON.h"
char *get_camera_sensor_settings_json(void)
{
    sensor_t *sen = esp_camera_sensor_get();
    if (!sen)
    {
        ESP_LOGE(TAG, "Sensor not found");
        return NULL;
    }
    camera_status_t* status = &(sen->status);

    cJSON *root = cJSON_CreateObject();
    char frameSizeStr[32] = {0};
    snprintf(frameSizeStr, sizeof(frameSizeStr), "%s(%dx%d)",
             frameData[status->framesize].frameSizeStr,
             frameData[status->framesize].frameWidth,
             frameData[status->framesize].frameHeight);
    cJSON_AddStringToObject(root, "framesize", frameSizeStr);
    cJSON_AddNumberToObject(root, "brightness", status->brightness);
    cJSON_AddNumberToObject(root, "contrast", status->contrast);
    cJSON_AddNumberToObject(root, "saturation", status->saturation);
    cJSON_AddNumberToObject(root, "sharpness", status->sharpness);
    cJSON_AddNumberToObject(root, "quality", status->quality);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str; // 需要调用者free
}

// get sensro supported frame sizes,封装成json的形式
char *get_camera_supported_framesizes_json(void)
{
    sensor_t *sen = esp_camera_sensor_get();
    if (!sen)    {
        ESP_LOGE(TAG, "Sensor not found");
        return NULL;
    }
    camera_sensor_info_t *info = esp_camera_sensor_get_info(&sen->id);
    cJSON *root = cJSON_CreateObject();
    cJSON *framesize_array = cJSON_CreateArray();
    for (framesize_t fs = FRAMESIZE_96X96; fs < FRAMESIZE_INVALID; fs++)
    {
        if (fs <= info->max_size)
        {
            char frameSizeStr[20] = {0};
            snprintf(frameSizeStr, sizeof(frameSizeStr), "%s(%dx%d)",
                     frameData[fs].frameSizeStr,
                     frameData[fs].frameWidth,
                     frameData[fs].frameHeight);
            cJSON_AddItemToArray(framesize_array, cJSON_CreateString(frameSizeStr));
        }
    }
    cJSON_AddItemToObject(root, "supported_framesizes", framesize_array);
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str; // 需要调用者free
}

// 应用相机配置
esp_err_t apply_camera_config(cJSON *config_json)
{
    sensor_t *sen = esp_camera_sensor_get();
    if (!sen) {
        ESP_LOGE(TAG, "Sensor not found");
        return ESP_FAIL;
    }

    cJSON *frameSize = cJSON_GetObjectItem(config_json, "frameSize");
    cJSON *brightness = cJSON_GetObjectItem(config_json, "brightness");
    cJSON *contrast = cJSON_GetObjectItem(config_json, "contrast");
    cJSON *saturation = cJSON_GetObjectItem(config_json, "saturation");
    cJSON *sharpness = cJSON_GetObjectItem(config_json, "sharpness");
    cJSON *quality = cJSON_GetObjectItem(config_json, "quality");

    if (frameSize) {

        int fs = -99; // invalid frame size
        if (cJSON_IsNumber(frameSize))
        {
            fs = frameSize->valueint;
        }
        else if (cJSON_IsString(frameSize))
        {
            fs = atoi(frameSize->valuestring);
        }
        if (fs >= 0 && fs < FRAMESIZE_INVALID) {
            sen->set_framesize(sen, (framesize_t)fs);
            ESP_LOGI(TAG, "Set frame size to: %d", fs);
        }
    }

    if (brightness) {
        int br = -99;
        if (cJSON_IsNumber(brightness))
        {
            br = brightness->valueint;
        }
        else if (cJSON_IsString(brightness))
        {
            br = atoi(brightness->valuestring);
        }
        if (br >= -2 && br <= 2) {
            sen->set_brightness(sen, br);
            ESP_LOGI(TAG, "Set brightness to: %d", br);
        }
    }

    if (contrast) {
        int ct = -99;
        if (cJSON_IsNumber(contrast))
        {
            ct = contrast->valueint;
        }
        else if (cJSON_IsString(contrast))
        {
            ct = atoi(contrast->valuestring);   
        }
        if (ct >= 0 && ct <= 2) {
            sen->set_contrast(sen, ct);
            ESP_LOGI(TAG, "Set contrast to: %d", ct);
        }
    }

    if (saturation) {
        int st = -99;
        if (cJSON_IsNumber(saturation))
        {
            st = saturation->valueint;
        }
        else if (cJSON_IsString(saturation))
        {
            st = atoi(saturation->valuestring);
        }
        if (st >= -2 && st <= 2) {
            sen->set_saturation(sen, st);
            ESP_LOGI(TAG, "Set saturation to: %d", st);
        }
    }

    if (sharpness) {
        int sh = -99;
        if (cJSON_IsNumber(sharpness))
        {
            sh = sharpness->valueint;
        }
        else if (cJSON_IsString(sharpness))
        {
            sh = atoi(sharpness->valuestring);
        }
        if (sh >= -2 && sh <= 2) {
            sen->set_sharpness(sen, sh);
            ESP_LOGI(TAG, "Set sharpness to: %d", sh);
        }
    }

    if (quality) {
        int ql = -99;
        if (cJSON_IsNumber(quality))
        {
            ql = quality->valueint;
        }
        else if (cJSON_IsString(quality))
        {
            ql = atoi(quality->valuestring);
        }
        if (ql >= 10 && ql <= 63) {
            sen->set_quality(sen, ql);
            ESP_LOGI(TAG, "Set quality to: %d", ql);
        }
    }

    if (ESP_OK != esp_camera_save_to_nvs(NVS_SENSOR_KEY)) {
        ESP_LOGE(TAG, "Failed to save camera config to NVS");
    }

    return ESP_OK;
}
