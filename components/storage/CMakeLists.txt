idf_component_register(SRCS "utilsFS.c" "avi.c" "storage.c"
                    INCLUDE_DIRS "include"
                    REQUIRES fatfs esp_timer sdmmc
                    REQUIRES Camera ChipInfo Utils)
