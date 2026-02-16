idf_component_register(SRCS "station_example_main.c"
                    PRIV_REQUIRES esp_wifi nvs_flash esp_psram esp_http_server mqtt
                    PRIV_REQUIRES ChipInfo Camera EasyRTSPServer ha_mqtt_client 
                    INCLUDE_DIRS "."
                    EMBED_FILES "index.html")
