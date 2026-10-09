#include <stdio.h>
#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "soc/soc_caps.h"
#if SOC_TEMP_SENSOR_SUPPORTED
#include "driver/temperature_sensor.h"
#endif
#include <time.h>

#define TAG "ChipInfo"

#define WARN_HEAP (32 * 1024)  // low free heap warning
#define WARN_ALLOC (16 * 1024) // low free max allocatable free heap block

#define DEBUG_MEM true // in function debugMemory()

#if SOC_TEMP_SENSOR_SUPPORTED
static temperature_sensor_handle_t cpu_temp_handle = NULL;
#endif

esp_err_t initCpuTemperature(void)
{
#if SOC_TEMP_SENSOR_SUPPORTED
    if (cpu_temp_handle != NULL)
    {
        return ESP_OK;
    }

    temperature_sensor_config_t temp_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    esp_err_t ret = temperature_sensor_install(&temp_config, &cpu_temp_handle);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = temperature_sensor_enable(cpu_temp_handle);
    if (ret != ESP_OK)
    {
        temperature_sensor_uninstall(cpu_temp_handle);
        cpu_temp_handle = NULL;
    }

    return ret;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t getCpuTemperature(float *out_celsius)
{
    if (out_celsius == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

#if SOC_TEMP_SENSOR_SUPPORTED
    if (cpu_temp_handle == NULL)
    {
        ESP_LOGW(TAG, "CPU temperature sensor not initialized, call initCpuTemperature() first");
        return ESP_ERR_INVALID_STATE;
    }

    return temperature_sensor_get_celsius(cpu_temp_handle, out_celsius);
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t deinitCpuTemperature(void)
{
#if SOC_TEMP_SENSOR_SUPPORTED
    if (cpu_temp_handle == NULL)
    {
        ESP_LOGW(TAG, "CPU temperature sensor not initialized, call initCpuTemperature() first");
        return ESP_OK;
    }

    esp_err_t ret = temperature_sensor_disable(cpu_temp_handle);
    if (ret == ESP_OK)
    {
        ret = temperature_sensor_uninstall(cpu_temp_handle);
    }
    if (ret == ESP_OK)
    {
        cpu_temp_handle = NULL;
    }

    return ret;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

void chip_info(void)
{
    uint32_t flash_size = 0;
    esp_chip_info_t chip_info;

    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "This is %s chip with %d CPU core(s), %s%s%s%s, ",
             CONFIG_IDF_TARGET,
             chip_info.cores,
             (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",
             (chip_info.features & CHIP_FEATURE_BT) ? "BT" : "",
             (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "",
             (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : "");

    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;
    ESP_LOGI(TAG, "silicon revision v%d.%d, ", major_rev, minor_rev);

    if (esp_flash_get_size(NULL, &flash_size) != ESP_OK)
    {
        ESP_LOGE(TAG, "Get flash size failed");
    }
    else
    {
        ESP_LOGI(TAG, "%" PRIu32 "MB %s flash\n", flash_size / (uint32_t)(1024 * 1024),
                 (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
    }
    ESP_LOGI(TAG, "In RAM free size: %u bytes", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "SPI Ram free size: %u bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "Total Ram free size: %u bytes", heap_caps_get_free_size(MALLOC_CAP_8BIT));

    float cpu_temp = 0.0f;
    esp_err_t ret = getCpuTemperature(&cpu_temp);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "CPU temperature: %.1f C", cpu_temp);
    }
    else
    {
        ESP_LOGW(TAG, "Get CPU temperature failed: %s", esp_err_to_name(ret));
    }
}

void debugMemory(const char *caller)
{
    if (DEBUG_MEM)
    {
        ESP_LOGI(TAG, "%s > Current Free: %u Bytes, Block %u Bytes, Min Free: %u Bytes", caller,
                 heap_caps_get_free_size(MALLOC_CAP_8BIT),
                 heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                 heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
    }
}

void checkMemory(const char *source)
{
    size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    size_t min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);

    ESP_LOGI(TAG, "%s > Current Free: %u Bytes, Block %u Bytes, Min Free: %u Bytes", source, free_heap, largest_block, min_free);
    if (free_heap < WARN_HEAP)
        ESP_LOGW(TAG, "Free heap only %u Bytes, min %u Bytes", free_heap, min_free);
    if (largest_block < WARN_ALLOC)
        ESP_LOGW(TAG, "Max allocatable heap block is only %u Bytes", largest_block);
}
