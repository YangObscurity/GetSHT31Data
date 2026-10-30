#include <stdio.h>
#include <sys/time.h>
#include <string.h>
#include "common.h"
#include "sht3x.h"
#include "sht_sensor.h"
#include "sdkconfig.h"
#include "esp_err.h"
#include "nvs.h"
#include "network.h"


#define SHT_WAKETIME 1000
#define SHT_WAITTIME 100
#define SHT_I2C_ADDRESS 0x44
#define SHT_I2C_PORT I2C_NUM_0

#define SEQ_NVS_NAMESPACE "sapi"
#define SEQ_NVS_KEY       "seq"
#define SEQ_BLOCK         64

static const char * TAG = "SHT31";
static bool initate_sensor = false;
static uint32_t sequence_number = 0;
static sht3x_t sht3x_config;
static nvs_handle_t seq_nvs = 0;
static uint32_t seq_persist_at = 0;

//Private Functions
static void seq_load(void)
{
    sequence_number = 0;
    seq_persist_at = 0;
    esp_err_t err = nvs_open(SEQ_NVS_NAMESPACE, NVS_READWRITE, &seq_nvs);
    if (err != ESP_OK) {
        seq_nvs = 0;
        ESP_LOGW(TAG, "NVS open failed (%s), seq starts from 0 (NOT persistent)",
                 esp_err_to_name(err));
        return;
    }
    uint32_t stored = 0;
    if (nvs_get_u32(seq_nvs, SEQ_NVS_KEY, &stored) == ESP_OK) {
        sequence_number = stored;
    } else {
        sequence_number = 0;
    }
    seq_persist_at = sequence_number + SEQ_BLOCK;
    if (nvs_set_u32(seq_nvs, SEQ_NVS_KEY, seq_persist_at) == ESP_OK) {
        nvs_commit(seq_nvs);
    }
    ESP_LOGI(TAG, "SEQ restored=%" PRIu32 ", reserved until %" PRIu32,
             sequence_number, seq_persist_at);
}

static void seq_maybe_persist(void)
{
    if (seq_nvs == 0 || sequence_number < seq_persist_at) return;
    seq_persist_at = sequence_number + SEQ_BLOCK;
    if (nvs_set_u32(seq_nvs, SEQ_NVS_KEY, seq_persist_at) == ESP_OK) {
        nvs_commit(seq_nvs);
    }
    ESP_LOGI(TAG, "SEQ reserved until %" PRIu32, seq_persist_at);
}

static uint64_t get_timestamp_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)(tv.tv_sec) * 1000 + (uint64_t)(tv.tv_usec) / 1000;
}
static esp_err_t init_sht_sensor(void)
{
    esp_err_t ret = i2cdev_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2cdev_init Failed: %s", esp_err_to_name(ret));
        return ret;
    }

    memset(&sht3x_config, 0, sizeof(sht3x_t));
    esp_err_t res = sht3x_init_desc(&sht3x_config, SHT_I2C_ADDRESS, SHT_I2C_PORT, CONFIG_I2CDEV_DEFAULT_SDA_PIN, CONFIG_I2CDEV_DEFAULT_SCL_PIN);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "sht3x_init_desc Failed: %s", esp_err_to_name(res));
        return res;
    }

    ret = sht3x_init(&sht3x_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "sht3x_init Failed: %s", esp_err_to_name(ret));
        return ret;
    }
    initate_sensor = true;
    return ESP_OK;
}
static esp_err_t data_success_float(float * tempreture, float * humidity, sht_data_t * data_struct, QueueHandle_t QueueHandler)
{
    if (!tempreture || !humidity || !data_struct || !QueueHandler) {
        return ESP_ERR_INVALID_ARG;
    }
    data_struct->temperature = *tempreture;
    data_struct->humidity = *humidity;
    data_struct->ts_ms = get_timestamp_ms();
    data_struct->sequence = sequence_number++;
    data_struct->state = true;
    ESP_LOGI(TAG, "SHT31 DATA READ SUCCESS"
             "\nTemperature: %.2f °C"
             "\nHumidity: %.2f %%"
             "\nTimestamp: %" PRIu64 " ms"
             "\nSequence Number: %" PRIu32
             "\nState: %d",
             data_struct->temperature,
             data_struct->humidity,
             data_struct->ts_ms,
             data_struct->sequence,
             data_struct->state);
    BaseType_t check = xQueueSend(QueueHandler, data_struct, pdMS_TO_TICKS(SHT_WAITTIME));
    if (check != pdTRUE) {
        ESP_LOGE(TAG, "Failed to send data to queue");
        return ESP_ERR_QUEUE_FULL;
    }
    return ESP_OK;
}
static esp_err_t data_failed(sht_data_t * data_struct, QueueHandle_t QueueHandler)
{
    data_struct->sequence = sequence_number++;
    data_struct->ts_ms = get_timestamp_ms();
    data_struct->temperature = 0.0f;
    data_struct->humidity = 0.0f;
    data_struct->state = false;
    ESP_LOGW(TAG, "SHT31 DATA READ FAILED"
             "\nTimestamp: %" PRIu64 " ms"
             "\nSequence Number: %" PRIu32
             "\nState: %d",
             data_struct->ts_ms,
             data_struct->sequence,
             data_struct->state);
    BaseType_t check = xQueueSend(QueueHandler, data_struct, pdMS_TO_TICKS(SHT_WAITTIME));
    if (check != pdTRUE) {
        ESP_LOGE(TAG, "Failed to send failed data to queue");
        return ESP_ERR_QUEUE_FULL;
    }
    return ESP_OK;
}
//Public Functions
void read_sht_data(void* arg)
{
    QueueHandle_t sht_queue_handler = (QueueHandle_t)arg;
    sht_data_t sht_data;
    TickType_t last_wake_time;
    float temperature = 0.0f;
    float humidity = 0.0f;

    seq_load();

    ESP_LOGI(TAG, "WAITING FOR SNTP TIME SYNC");
    uint32_t waited_ms = 0;
    while (!network_time_synced()) {
        vTaskDelay(pdMS_TO_TICKS(200));
        waited_ms += 200;
        if (waited_ms % 30000 == 0) {
            ESP_LOGW(TAG, "STILL WAITING FOR TIME (%" PRIu32 " s) - sampling suppressed",
                     waited_ms / 1000);
        }
    }
    ESP_LOGI(TAG, "TIME VALID -> START 1Hz SAMPLING, seq=%" PRIu32, sequence_number);
    last_wake_time = xTaskGetTickCount();

    for(;;) {
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(SHT_WAKETIME));
        if (!initate_sensor) {
            esp_err_t init_result = init_sht_sensor();
            if (init_result != ESP_OK) {
                ESP_LOGE(TAG, "Failed to initialize SHT31 sensor: %s", esp_err_to_name(init_result));
                continue;
            }
        }

        esp_err_t read_result = sht3x_measure(&sht3x_config, &temperature, &humidity);
        if (read_result == ESP_OK) {
            data_success_float(&temperature, &humidity, &sht_data, sht_queue_handler);
        } else {
            data_failed(&sht_data, sht_queue_handler);
        }
        seq_maybe_persist();
    }
}
