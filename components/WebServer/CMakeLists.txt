idf_component_register(SRCS "WebServer.c" "web_index_html.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp32-camera Camera esp_http_server cjson Utils
                    EMBED_FILES "index.html")
