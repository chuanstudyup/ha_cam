idf_component_register(SRCS "MotionDetect.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp32-camera Camera ChipInfo paramCenter esp_new_jpeg)
