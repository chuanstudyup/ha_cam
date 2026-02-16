idf_component_register(SRCS "ha_mqtt_client.c"
                    PRIV_REQUIRES esp_wifi mqtt
                    PRIV_REQUIRES Camera
                    INCLUDE_DIRS "include")
