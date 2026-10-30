#include <stdio.h>
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "common.h"
#include "sdkconfig.h"
#include "network.h"
//SNTPTime
#include "esp_netif_sntp.h"
#include "time.h"

#define LIFE_GUARD_WAITTIME 3000

static EventGroupHandle_t wifi_event_handler = NULL;
static const char * TAG = "WIFI"; 
static int8_t current_retry = 0;
static TaskHandle_t life_guard_task_handler = NULL;
static volatile bool time_synced = false;
void life_guard(void* arg)
{
    for(;;)
    {
        EventBits_t life_guard_handler = xEventGroupWaitBits(wifi_event_handler,WIFI_CONNECT_FAIL,pdFALSE,pdFALSE,portMAX_DELAY);
        if(life_guard_handler & WIFI_CONNECT_FAIL)
        {
            ESP_LOGW(TAG,"[LIFE GUARD]WIFI MAX RETRY REACHED\nSTARTTED WIFI GUARD");
            vTaskDelay(pdMS_TO_TICKS(LIFE_GUARD_WAITTIME));
            esp_wifi_connect();
        }
    }
}
static void event_handler (void* event_handler_arg,
                            esp_event_base_t event_base,
                            int32_t event_id,
                            void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) 
    {
        ESP_LOGI(TAG,"WIFI STATION MODE START");
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        xEventGroupClearBits(wifi_event_handler,WIFI_CONNECT_SUCCESS);
        ESP_LOGI(TAG,"WIFI CONNECT FAILED");
        if(current_retry<MAX_RETRY)
        {
            ESP_LOGI(TAG,"RECONNECTING");
            current_retry++;
            //vTaskDelay(pdMS_TO_TICKS(NETWORK_WAKETIME*current_retry));//2s-4s-6s
            esp_wifi_connect();
        }
        else{
            ESP_LOGI(TAG,"MAX RETRY REACHED");
            xEventGroupSetBits(wifi_event_handler,WIFI_CONNECT_FAIL);
            vTaskResume(life_guard_task_handler);
            ESP_LOGI(TAG,"LIFE GUARD PROCESS START");
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t* ip_event = (ip_event_got_ip_t*) event_data;
        current_retry = 0;
        xEventGroupSetBits(wifi_event_handler,WIFI_CONNECT_SUCCESS);
        xEventGroupClearBits(wifi_event_handler,WIFI_CONNECT_FAIL);
        if(life_guard_task_handler != NULL)
        {
            vTaskSuspend(life_guard_task_handler);
            ESP_LOGI(TAG,"[SUSPEND]WIFI GUARD");
        }
        ESP_LOGI(TAG,"CONNECT SUCCESS, CURRENT IP:" IPSTR,IP2STR(&(ip_event->ip_info.ip)));
        esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(NTP_SERVER_IP);
        esp_netif_sntp_init(&sntp_cfg);
        if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000)) == ESP_OK) {
            time_synced = true;
            ESP_LOGI(TAG, "SNTP SYNC SUCCESS");
        } else {
            ESP_LOGE(TAG, "SNTP SYNC TIMEOUT, SAMPLING STAYS OFF");
        }
        setenv("TZ", "UTC0", 1);
        tzset();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_LOST_IP)
    {
        xEventGroupClearBits(wifi_event_handler,WIFI_CONNECT_SUCCESS);
        ESP_LOGI(TAG,"CONNECTION LOST");
        if(eTaskGetState(life_guard_task_handler) == eSuspended)
        {
            vTaskResume(life_guard_task_handler);
            ESP_LOGI(TAG,"[RESUME]WIFI GUARD");
        }
        else if(eTaskGetState(life_guard_task_handler) == eRunning)
        {
            ESP_LOGI(TAG,"[RUNNING]WIFI GUARD");
        }
        else
        {
            ESP_LOGI(TAG,"[UNKNOWN]WIFI GUARD");
        }
    }
}
void network_init(void)
{
    wifi_event_handler = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t dft_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&dft_cfg));
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_any_ip;
    esp_event_handler_instance_t instance_lost_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,ESP_EVENT_ANY_ID,event_handler,NULL,&instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,IP_EVENT_STA_GOT_IP,event_handler,NULL,&instance_any_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,IP_EVENT_STA_LOST_IP,event_handler,NULL,&instance_lost_ip));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_NETWORK_SSID,
            .password = CONFIG_NETWORK_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            }
        }
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA,&wifi_config));
    ESP_LOGI(TAG,"WIFI ACTIVATING");
    ESP_ERROR_CHECK(esp_wifi_start());
    xTaskCreatePinnedToCore(life_guard,"WIFI LIFE GUARD",NORMAL_STACKSIZE,NULL,9,&life_guard_task_handler,1);
}
EventGroupHandle_t GetEventGroupHandler(void)
{
    return wifi_event_handler;
}
bool network_time_synced(void)
{
    return time_synced;
}