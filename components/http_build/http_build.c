#include <string.h>
#include "common.h"
#include "esp_http_client.h"
#include "http_build.h"
#include "sdkconfig.h"

static const char * TAG = "HTTP";
static esp_http_client_handle_t http_handler;
int8_t max_retry = 3;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id)
    {
    case HTTP_EVENT_ERROR:
        ESP_LOGI(TAG,"HTTP ERROR");
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGI(TAG,"HTTP RECEIVED DATA");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(TAG,"HTTP DISCONNECTED");
        break;
    default:
        break;
    }
    return ESP_OK;
}

esp_err_t http_init(void)
{
    esp_http_client_config_t cfg = {
        .url = CONFIG_HTTP_SERVER_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 4000,
        .event_handler = http_event_handler,
        .buffer_size = 512,
        .buffer_size_tx = 512,
        .keep_alive_enable = true,
    };
    http_handler = esp_http_client_init(&cfg);
    if (http_handler == NULL)
    {
        ESP_LOGE(TAG,"HTTP INIT FAILED!");
        return ESP_FAIL;
    }
    esp_http_client_set_header(http_handler,"Content-Type","application/json");
    return ESP_OK;
}

esp_err_t http_upload_build(char * json_body)
{
    esp_http_client_set_post_field(http_handler,json_body,strlen(json_body));
    for(int8_t retry=0;retry<max_retry;retry++)
    {
        esp_err_t check = esp_http_client_perform(http_handler);
        if (check == ESP_OK)
        {
            int status_code = esp_http_client_get_status_code(http_handler);
            int64_t content_length = esp_http_client_get_content_length(http_handler);
            if (status_code >= 200 && status_code < 300)
            {
                ESP_LOGI(TAG,"[SUCCESS]Status code:%d,Content length:%lld",status_code,content_length);
                return ESP_OK;
            }
            ESP_LOGW(TAG,"[WARN]Server returned non-2xx status code: %d",status_code);
        }
        else
        {
            ESP_LOGI(TAG,"[FAIL]Response fail\nerror code:%s\nRetrying:%d time",esp_err_to_name(check),retry+1);
        }
    }
    return ESP_FAIL;
}