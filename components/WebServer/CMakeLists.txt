idf_component_register(SRCS "WebServer.c" "web_index_html.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp32-camera Camera esp_http_server cjson Utils app_update storage paramCenter EasyRTSPServer MotionDetect
                    EMBED_FILES "index.html" "ota.html" "files.html")
