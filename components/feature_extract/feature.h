#ifndef __FEATURE_H__
#define __FEATURE_H__
#include "common.h"
#include "esp_err.h"

esp_err_t network_failed_buffer(char * received_json, int16_t * amount_pointer);
char ** json_buffer_retrieve(void);
void sht_json_task(void* arg);

#endif /*__FEATURE_H__*/