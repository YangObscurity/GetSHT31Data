idf_component_register(SRCS "http_build.c"
                        INCLUDE_DIRS "."
                        REQUIRES common
                        PRIV_REQUIRES esp_http_client)