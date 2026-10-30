#include <stdio.h>
#include "common.h"
#include "feature.h"
#include "network.h"
#include "cJSON.h"
#include "http_build.h"
#include "sht_sensor.h"

static const char * TAG = "WORKER";
static QueueGroupHandle_t queue_group;
int16_t real_data_position = 0;

static void json_network_retrieve(void* arg)
{
    QueueHandle_t worker_json_net_handler = (QueueHandle_t)arg;
    static bool is_failed_before = false;
    static int16_t queue_stored_amount = 0;
    char ** json_buffer = json_buffer_retrieve();
    char * json_receive = NULL;
    for(;;)
    {
        EventGroupHandle_t wifi_event_handler = GetEventGroupHandler();
        EventBits_t event_bits = xEventGroupWaitBits(wifi_event_handler, (WIFI_CONNECT_SUCCESS | WIFI_CONNECT_FAIL), pdFALSE, pdFALSE, portMAX_DELAY);
        if (event_bits & WIFI_CONNECT_SUCCESS)
        {
            ESP_LOGI(TAG, "WIFI CONNECTED SUCCESS");
            while (is_failed_before && queue_stored_amount != 0)
            {
                if (real_data_position < queue_stored_amount)
                {
                    esp_err_t err = http_upload_build(json_buffer[real_data_position]);
                    if (err == ESP_OK)
                    {
                        cJSON_free(json_buffer[real_data_position]);
                        json_buffer[real_data_position] = NULL;
                        real_data_position++;
                        continue;
                    }
                    else
                    {
                        ESP_LOGI(TAG, "CACHED JSON DATA SEND FAILED\nWait for retry");
                        break;
                    }
                }
                else if (real_data_position == queue_stored_amount)
                {
                    queue_stored_amount = 0;
                    real_data_position = 0;
                    is_failed_before = false;
                }
            }
            if (!is_failed_before && queue_stored_amount == 0)
            {
                BaseType_t q_check = xQueueReceive(worker_json_net_handler, &json_receive, portMAX_DELAY);
                if (q_check == pdTRUE && json_receive != NULL)
                {
                    ESP_LOGI(TAG, "Net\\Json\\Data Processing:%s", json_receive);
                    http_upload_build(json_receive);
                    cJSON_free(json_receive);
                }
            }
        }
        else if (event_bits & WIFI_CONNECT_FAIL)
        {
            ESP_LOGI(TAG, "WIFI CONNECTED FAILED");
            BaseType_t q_check = xQueueReceive(worker_json_net_handler, &json_receive, portMAX_DELAY);
            if (q_check == pdTRUE && json_receive != NULL)
            {
                is_failed_before = true;
                esp_err_t buf_ret = network_failed_buffer(json_receive, &queue_stored_amount);
                if (buf_ret != ESP_OK)
                {
                    ESP_LOGW(TAG, "Failed to buffer JSON, discarded");
                }
            }
        }
    }
}

void worker_session(void)
{
    queue_group.worker_sht_json_handler = xQueueCreate(5, sizeof(sht_data_t)); 
    queue_group.worker_json_net_handler = xQueueCreate(5, sizeof(char *)); 
    xTaskCreatePinnedToCore(read_sht_data, "READ SENSOR DATA", NORMAL_STACKSIZE, queue_group.worker_sht_json_handler, 7, NULL, 1);
    xTaskCreatePinnedToCore(sht_json_task, "JSON TO NETWORK", NORMAL_STACKSIZE, &queue_group, 7, NULL, 1);
    xTaskCreatePinnedToCore(json_network_retrieve, "WIFI DATA TRANSFER", NETWORK_STACKSIZE, queue_group.worker_json_net_handler, 7, NULL, 0);
}
