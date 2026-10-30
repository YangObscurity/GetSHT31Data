#include <stdio.h>
#include "cJSON.h"
#include "common.h"
#include "feature.h"
#include "sdkconfig.h"

static const char * TAG = "cJSON";
static char * json_buffer[JSON_BUFFER_SIZE];

static esp_err_t sht_body_build(QueueHandle_t sht_handler, QueueHandle_t json_handler)
{  
    sht_data_t get_data;
    BaseType_t check = xQueueReceive(sht_handler, &get_data, portMAX_DELAY);
    if (check != pdTRUE) return ESP_ERR_QUEUE_EMPTY;

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return ESP_ERR_JSON_BUILD_FAILED;

    cJSON_AddStringToObject(root, "v", CONFIG_ALGORISM_VERSION);
    cJSON_AddStringToObject(root, "d_id", CONFIG_ESP_DEVICE_ID);
    cJSON_AddNumberToObject(root, "seq", get_data.sequence);
    cJSON_AddNumberToObject(root, "temp", get_data.temperature);
    cJSON_AddNumberToObject(root, "humi", get_data.humidity);
    cJSON_AddBoolToObject(root, "st", get_data.state);
    cJSON_AddNumberToObject(root, "ts", get_data.ts_ms);

    char * build_data_body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (build_data_body == NULL) return ESP_ERR_NO_MEM;

    check = xQueueSend(json_handler, &build_data_body, portMAX_DELAY);
    if (check != pdTRUE)
    {
        cJSON_free(build_data_body);
        return ESP_ERR_QUEUE_FULL;
    }
    return ESP_OK;
}

esp_err_t network_failed_buffer(char * received_json, int16_t * amount_pointer)
{
    if ((*amount_pointer) >= JSON_BUFFER_SIZE - 1)
    {  
        ESP_LOGW(TAG, "JSON BUFFER ZONE IS FULL!\nSTOP STORING DATA:%d", *amount_pointer);
        cJSON_free(received_json);
        return ESP_ERR_QUEUE_FULL;
    }
    json_buffer[(*amount_pointer)++] = received_json;
    ESP_LOGI(TAG, "JSON BUFFER ZONE DATA COUNT:%d", *amount_pointer);
    return ESP_OK;
}

void sht_json_task(void* arg)
{
    QueueGroupHandle_t * WorkerQueueGroup = (QueueGroupHandle_t*)arg;
    for(;;)
    {
        sht_body_build(WorkerQueueGroup->worker_sht_json_handler, WorkerQueueGroup->worker_json_net_handler);
    }
}

char ** json_buffer_retrieve(void)
{
    return json_buffer;
}