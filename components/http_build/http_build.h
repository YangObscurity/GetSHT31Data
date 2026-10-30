#ifndef __HTTP_BUILD_H__
#define __HTTP_BUILD_H__
#include "common.h"
#include "esp_err.h"

esp_err_t http_init(void);
esp_err_t http_upload_build(char * json_body);

#endif /*__HTTP_BUILD_H__*/
