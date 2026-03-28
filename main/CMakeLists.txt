idf_component_register(SRCS "app_main.c"
                    PRIV_REQUIRES esp_wifi nvs_flash esp_psram mqtt
                    PRIV_REQUIRES ChipInfo Camera EasyRTSPServer ha_mqtt_client storage WebServer paramCenter
                    INCLUDE_DIRS ".")
