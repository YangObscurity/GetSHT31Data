#ifndef __NETWORK_H__
#define __NETWORK_H__
#include "common.h"
//WIFI CONFIG DEFINE
#define MAX_RETRY 3
#define NETWORK_WAKETIME 2000
//WIFI STATUS DEFINE
#define WIFI_CONNECT_SUCCESS BIT0
#define WIFI_CONNECT_FAIL BIT1
void network_init(void);
EventGroupHandle_t GetEventGroupHandler(void);
#endif /*__NETWORK_H__*/