idf_component_register(SRCS "worker.c"
                    INCLUDE_DIRS  "."
                    REQUIRES "common"
                    PRIV_REQUIRES "sht_sensor" "feature_extract" "network" "http_build")