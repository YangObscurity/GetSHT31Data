#ifndef __COMMON_H__
#define __COMMON_H__
#include "inttypes.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "esp_log.h"

#define NORMAL_STACKSIZE 4096
#define NETWORK_STACKSIZE 8192
#define JSON_BUFFER_SIZE 128
/*

typedef struct {
    int16_t tempreture;
    int16_t humidity;
    bool status;
} sht_int_data_t;

*/
typedef struct {
    uint64_t ts_ms;
    uint32_t sequence;
    float    temperature;
    float    humidity;
    uint8_t  state;
    char     device_id[16];
    char     version[8];
} sht_data_t;

#define WORKFLOW_ERR_BASE               0x10000
#define ESP_ERR_QUEUE_FULL              (WORKFLOW_ERR_BASE + 1)
#define ESP_ERR_QUEUE_EMPTY             (WORKFLOW_ERR_BASE + 2)
#define ESP_ERR_JSON_BUILD_FAILED       (WORKFLOW_ERR_BASE + 3)

typedef struct {
    QueueHandle_t worker_sht_json_handler;
    QueueHandle_t worker_json_net_handler;
} QueueGroupHandle_t;

#endif /*__COMMON_H__*/