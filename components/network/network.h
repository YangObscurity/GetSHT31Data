#ifndef __NETWORK_H__
#define __NETWORK_H__
#include <stdbool.h>
#include "common.h"
//WIFI CONFIG DEFINE
#define MAX_RETRY 3
#define NETWORK_WAKETIME 2000
//WIFI STATUS DEFINE
#define WIFI_CONNECT_SUCCESS BIT0
#define WIFI_CONNECT_FAIL BIT1
#define NTP_SERVER_IP "10.42.0.1"
void network_init(void);
EventGroupHandle_t GetEventGroupHandler(void);
bool network_time_synced(void);
#endif /*__NETWORK_H__*/