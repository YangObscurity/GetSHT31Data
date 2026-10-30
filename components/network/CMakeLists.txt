idf_component_register(SRCS "network.c"
                    INCLUDE_DIRS "."
                    REQUIRES common esp_wifi esp_netif nvs_flash esp_event)
