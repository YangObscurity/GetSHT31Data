idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES "nvs_flash" "common" "worker" "network" "http_build")
                    #REQUIRES "dht_sensor" "feature_extract" "network" "common")