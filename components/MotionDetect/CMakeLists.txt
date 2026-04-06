idf_component_register(SRCS "MotionDetect.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp32-camera Camera ChipInfo esp_new_jpeg)
