#include <stdio.h>
#include "common.h"
#include "nvs_flash.h"
#include "worker.h"
#include "network.h"
#include "http_build.h"

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_WARN);
    esp_err_t res = nvs_flash_init();
    if (res != ESP_OK){
        ESP_ERROR_CHECK(nvs_flash_erase());
        res = nvs_flash_init();
    }
    network_init();
    if (http_init() != ESP_OK) {
        ESP_LOGE("MAIN", "Failed to initialize HTTP client");
    }
    worker_session();
}
