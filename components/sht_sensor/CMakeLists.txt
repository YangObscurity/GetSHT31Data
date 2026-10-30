idf_component_register(SRCS "sht_sensor.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES esp_driver_gpio esp-idf-lib__sht3x nvs_flash network
                    REQUIRES common)
