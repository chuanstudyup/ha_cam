idf_component_register(SRCS "rjpeg.c" "EasyRTSPServer.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp32-camera mbedtls esp_timer)
