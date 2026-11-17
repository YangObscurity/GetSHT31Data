# GetSHT31Data

GetSHT31Data is an ESP-IDF C firmware application for ESP32 that samples temperature and humidity from an SHT31 sensor over I2C, serializes each reading with `cJSON`, buffers pending JSON payloads in RAM when Wi-Fi disconnects, and uploads measurements via HTTP POST using `esp_http_client`. Sensor sampling waits for SNTP clock synchronization before running a periodic 1 Hz loop with NVS-backed sequence number reservation. The sister firmware targeting a single-wire DHT11 sensor is maintained in [GetDHTData](https://github.com/YangObscurity/GetDHTData).

## Architecture and component layout

```text
+-------------------+  network_time_synced()  +-------------------+  worker_sht_json_handler  +-------------------+
| network.c         | ----------------------> | read_sht_data     | ---(sht_data_t, len 5)--> | sht_json_task     |
| life_guard (C1,P9)|                         | (sht_sensor.c)    |                           | (feature.c)       |
+-------------------+                         | Core 1, Prio 7    |                           | Core 1, Prio 7    |
         |                                    +-------------------+                           +-------------------+
         | wifi_event_handler                                                                           |
         | (BIT0 SUCCESS / BIT1 FAIL)                                           worker_json_net_handler |
         v                                                                               (char *, len 5)|
+------------------------+   WIFI_CONNECT_SUCCESS    +------------------------+                         |
| http_upload_build()    | <----- HTTP POST -------- | json_network_retrieve  | <-----------------------+
| (http_build.c)         |   (drain cache, then Q)   | (worker.c)             |
+------------------------+                           | Core 0, Prio 7         |
                                                     +------------------------+
                                                        |                  ^
                                   WIFI_CONNECT_FAIL    |                  | FIFO drain on
                                   network_failed_buffer|                  | WIFI_CONNECT_SUCCESS
                                                        v                  |
                                                     +------------------------+
                                                     | json_buffer[128]       |
                                                     | (feature.c, max 127)   |
                                                     +------------------------+
```

The `components/feature_extract` directory (`feature.c`) performs `cJSON` object serialization of `sht_data_t` structs and holds the static RAM fallback array `json_buffer` for failed network states. It does not perform signal processing or statistical feature extraction.

### Repository directories and files

| Path | Files | Responsibility |
| :--- | :--- | :--- |
| `main` | `main.c`, `CMakeLists.txt`, `Kconfig` | Entry point `app_main`: sets global log level to `ESP_LOG_WARN`, initializes NVS (erasing on any error), calls `network_init()`, `http_init()`, and `worker_session()`. |
| `components/common` | `common.h`, `CMakeLists.txt` | Header-only shared types (`sht_data_t`, `QueueGroupHandle_t`), stack size macros (`NORMAL_STACKSIZE`, `NETWORK_STACKSIZE`), `JSON_BUFFER_SIZE`, and custom error codes (`0x10001`-`0x10003`). |
| `components/sht_sensor` | `sht_sensor.c`, `sht_sensor.h`, `CMakeLists.txt`, `Kconfig`, `idf_component.yml` | Initializes `i2cdev` and `sht3x`, loads and reserves NVS sequence blocks, waits for SNTP sync, polls SHT31 at 1 Hz, and pushes `sht_data_t` structs to `worker_sht_json_handler`. |
| `components/feature_extract` | `feature.c`, `feature.h`, `CMakeLists.txt`, `idf_component.yml` | Reads `sht_data_t` from `worker_sht_json_handler`, builds unformatted JSON strings via `cJSON`, forwards pointers to `worker_json_net_handler`, and stores up to 127 buffered pointers in `json_buffer`. |
| `components/http_build` | `http_build.c`, `http_build.h`, `CMakeLists.txt`, `Kconfig` | Configures a persistent `esp_http_client` handle for `HTTP_METHOD_POST` with `Content-Type: application/json` and retries failed transfers. |
| `components/network` | `network.c`, `network.h`, `CMakeLists.txt`, `Kconfig` | Configures Wi-Fi STA mode, manages reconnect retries and the `life_guard` task via `wifi_event_handler`, initializes SNTP on IP acquisition, and sets `TZ` to `UTC0`. |
| `components/worker` | `worker.c`, `worker.h`, `CMakeLists.txt` | Creates the two FreeRTOS queues, spawns the sensor, JSON, and network transfer tasks, and implements the `json_network_retrieve` dispatch and cache-drain loop. |

### FreeRTOS tasks

| Function | Task name string | Stack size (bytes) | Priority | Core ID | Queues / EventGroups |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `life_guard` | `WIFI LIFE GUARD` | `4096` (`NORMAL_STACKSIZE`) | `9` | `1` | Waits on `wifi_event_handler` (`WIFI_CONNECT_FAIL`) |
| `read_sht_data` | `READ SENSOR DATA` | `4096` (`NORMAL_STACKSIZE`) | `7` | `1` | Sends to `worker_sht_json_handler` |
| `sht_json_task` | `JSON TO NETWORK` | `4096` (`NORMAL_STACKSIZE`) | `7` | `1` | Receives from `worker_sht_json_handler`, sends to `worker_json_net_handler` |
| `json_network_retrieve` | `WIFI DATA TRANSFER` | `8192` (`NETWORK_STACKSIZE`) | `7` | `0` | Waits on `wifi_event_handler` (`WIFI_CONNECT_SUCCESS \| WIFI_CONNECT_FAIL`), receives from `worker_json_net_handler` |

## Sampling, buffering, and HTTP retry behavior

### SHT31 I2C and timing

- **I2C driver API**: `components/sht_sensor/sht_sensor.c` uses the managed ESP Component Registry driver `esp-idf-lib/sht3x` (`1.0.8`) built on `esp-idf-lib/i2cdev` (`2.1.2`). It calls `i2cdev_init()`, `sht3x_init_desc()`, `sht3x_init()`, and `sht3x_measure()`.
- **I2C parameters**: Port `SHT_I2C_PORT` (`I2C_NUM_0`), 7-bit address `SHT_I2C_ADDRESS` (`0x44`), and pins `CONFIG_I2CDEV_DEFAULT_SDA_PIN` and `CONFIG_I2CDEV_DEFAULT_SCL_PIN` (provided by `esp-idf-lib/i2cdev` Kconfig rather than hardcoded in `sht_sensor.c`). SHT31 command bytes and CRC checks are handled internally inside `esp-idf-lib/sht3x` and do not appear directly in this repository's source files.
- **SNTP startup gate**: Before sampling starts, `read_sht_data` polls `network_time_synced()` every `200` ms (`vTaskDelay`) and logs a warning every `30000` ms while time remains unsynced.
- **Sampling interval and timestamps**: Once SNTP sync completes, `read_sht_data` captures `xTaskGetTickCount()` and runs a 1 Hz loop with `vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(SHT_WAKETIME))` where `SHT_WAKETIME` is `1000` ms. Each reading obtains `ts_ms` in Unix epoch milliseconds via `gettimeofday(&tv, NULL)`.
- **Sequence persistence**: `seq_load()` reads `u32` key `SEQ_NVS_KEY` (`"seq"`) from NVS namespace `SEQ_NVS_NAMESPACE` (defaulting to `0`). To limit NVS flash writes, firmware reserves blocks of `SEQ_BLOCK` (`64`): on boot it writes `sequence_number + 64` to NVS, and `seq_maybe_persist()` commits the next `+64` reservation whenever `sequence_number >= seq_persist_at`.
- **Sensor error handling**: If `init_sht_sensor()` fails, `read_sht_data` logs an error and retries on the next 1000 ms period without enqueuing a sample. Once initialized, if `sht3x_measure()` fails, `data_failed()` enqueues a record with `temperature = 0.0f`, `humidity = 0.0f`, and `state = false`. Queue sends to `worker_sht_json_handler` wait up to `SHT_WAITTIME` (`100` ms).

### Queues and RAM disconnect buffering

- **FreeRTOS queues**: `worker_session()` allocates `worker_sht_json_handler` (`xQueueCreate(5, sizeof(sht_data_t))`) and `worker_json_net_handler` (`xQueueCreate(5, sizeof(char *))`).
- **RAM fallback buffer**: `feature.c` declares `static char * json_buffer[JSON_BUFFER_SIZE]` (`128`). `network_failed_buffer()` checks `(*amount_pointer) >= JSON_BUFFER_SIZE - 1`, giving an effective capacity of `127` JSON string pointers. When `127` items are already buffered, incoming JSON strings are freed with `cJSON_free()` and dropped with `ESP_ERR_QUEUE_FULL`.
- **Disconnect and drain flow**:
  - When `WIFI_CONNECT_FAIL` (`BIT1`) is set in `wifi_event_handler`, `json_network_retrieve` receives pointers from `worker_json_net_handler`, sets `is_failed_before = true`, and appends them via `network_failed_buffer()`.
  - When `WIFI_CONNECT_SUCCESS` (`BIT0`) is set and `is_failed_before && queue_stored_amount != 0`, `json_network_retrieve` sends entries from `json_buffer[real_data_position]` in FIFO order via `http_upload_build()`, freeing each successfully uploaded string with `cJSON_free()`. Once `real_data_position == queue_stored_amount`, counters reset and `is_failed_before` becomes `false`.
  - When no buffered entries remain (`!is_failed_before && queue_stored_amount == 0`), `json_network_retrieve` blocks on `worker_json_net_handler`, calls `http_upload_build(json_receive)`, and calls `cJSON_free(json_receive)`.

### Wi-Fi, SNTP, and `esp_http_client` settings

- **Wi-Fi (`components/network/network.c`, `network.h`)**:
  - Station mode (`WIFI_MODE_STA`), auth threshold `WIFI_AUTH_WPA2_PSK`, PMF `capable = true`, `required = false`.
  - On `WIFI_EVENT_STA_DISCONNECTED`, clears `WIFI_CONNECT_SUCCESS` and performs up to `MAX_RETRY` (`3`) immediate `esp_wifi_connect()` calls (`NETWORK_WAKETIME` `2000` is defined in `network.h` but commented out in `network.c`). After `3` failed retries, sets `WIFI_CONNECT_FAIL` and resumes `life_guard`, which delays `LIFE_GUARD_WAITTIME` (`3000` ms) before each subsequent `esp_wifi_connect()`.
  - On `IP_EVENT_STA_GOT_IP`, resets `current_retry = 0`, sets `WIFI_CONNECT_SUCCESS`, clears `WIFI_CONNECT_FAIL`, suspends `life_guard`, initializes SNTP with `ESP_NETIF_SNTP_DEFAULT_CONFIG(NTP_SERVER_IP)` (`NTP_SERVER_IP` is defined as a hardcoded local IPv4 address in `components/network/network.h`), waits up to `15000` ms in `esp_netif_sntp_sync_wait()`, sets `time_synced = true` on `ESP_OK`, and configures `TZ` to `"UTC0"`.
- **HTTP client (`components/http_build/http_build.c`)**:
  - Initialized once in `http_init()` with `.url = CONFIG_HTTP_SERVER_URL`, `.method = HTTP_METHOD_POST`, `.timeout_ms = 4000`, `.buffer_size = 512`, `.buffer_size_tx = 512`, `.keep_alive_enable = true`, and header `Content-Type: application/json`.
  - `http_upload_build()` calls `esp_http_client_perform()` up to `max_retry` (`3`) total attempts consecutively with no inter-retry delay. Only HTTP status codes `200 <= status_code < 300` return `ESP_OK`; otherwise it continues up to `3` total attempts and returns `ESP_FAIL`.

## Configuration (Kconfig and sdkconfig.defaults)

### Project Kconfig options

`CONFIG_NETWORK_SSID`, `CONFIG_NETWORK_PASSWORD`, and `CONFIG_HTTP_SERVER_URL` have no defaults and must be configured in `idf.py menuconfig` before building.

| Symbol | File | Type | Default | Description |
| :--- | :--- | :--- | :--- | :--- |
| `CONFIG_ESP_DEBUG_LOGV` | `main/Kconfig` | `bool` | `y` | Declared for verbose debug logging (`SHOW THE INFORMATION OF DEBUG`), though unused in C source. |
| `CONFIG_ESP_DEVICE_ID` | `main/Kconfig` | `string` | `"ESP32_1"` | Device identifier emitted in the JSON `"d_id"` field. |
| `CONFIG_ALGORISM_VERSION` | `components/sht_sensor/Kconfig` | `string` | `"B1"` | Algorithm/firmware version string emitted in the JSON `"v"` field. |
| `CONFIG_NETWORK_SSID` | `components/network/Kconfig` | `string` | (none) | Target Wi-Fi station SSID. |
| `CONFIG_NETWORK_PASSWORD` | `components/network/Kconfig` | `string` | (none) | Target Wi-Fi station password. |
| `CONFIG_HTTP_SERVER_URL` | `components/http_build/Kconfig` | `string` | (none) | HTTP POST endpoint URL passed to `esp_http_client`. |

I2C SDA and SCL GPIOs are configured through `CONFIG_I2CDEV_DEFAULT_SDA_PIN` and `CONFIG_I2CDEV_DEFAULT_SCL_PIN` under the managed `i2cdev` component menu in `idf.py menuconfig`.

### Settings enforced in `sdkconfig.defaults`

| Setting | Value | Purpose in firmware |
| :--- | :--- | :--- |
| `CONFIG_FREERTOS_HZ` | `100` | 10 ms FreeRTOS tick resolution; tickless idle is explicitly disabled. |
| `CONFIG_PM_ENABLE` | not set | Disables dynamic power management and DFS. |
| `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ` | `160` | Sets default CPU clock to 160 MHz (`CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_160=y`). |
| `CONFIG_ESP_PHY_MAX_WIFI_TX_POWER` | `20` | Caps maximum Wi-Fi TX power at 20 dBm. |
| `CONFIG_ESP_WIFI_AMPDU_TX_ENABLED` / `RX_ENABLED` | `y` | Enables Wi-Fi TX/RX A-MPDU aggregation with `TX_BA_WIN=6` and `RX_BA_WIN=6`. |
| `CONFIG_ESP_WIFI_STA_DISCONNECTED_PM_ENABLE` | `y` | Enables STA power management while disconnected; `CONFIG_ESP_WIFI_SLP_IRAM_OPT` is not set. |
| `CONFIG_LWIP_SNTP_MAX_SERVERS` | `1` | Limits lwIP SNTP servers to 1. |
| `CONFIG_LWIP_SNTP_UPDATE_DELAY` | `600000` | Sets SNTP periodic resync interval to 600,000 ms (10 minutes); startup delay is disabled. |
| `CONFIG_LOG_DEFAULT_LEVEL` | `2` (`WARN`) | Compiles default log level at `WARN` (`CONFIG_LOG_DEFAULT_LEVEL_WARN=y`). |
| `CONFIG_ESP_CONSOLE_UART_BAUDRATE` | `115200` | UART0 console baud rate. |
| `CONFIG_COMPILER_OPTIMIZATION_SIZE` | `y` | Enables `-Os` size optimization (`CONFIG_COMPILER_OPTIMIZATION_DEBUG` not set). |
| `CONFIG_LWIP_MAX_SOCKETS` | `10` | Allocates up to 10 lwIP sockets. |

## HTTP JSON payload

`feature.c` serializes each `sht_data_t` sample with `cJSON_PrintUnformatted(root)` (single-line JSON on the wire; formatted below for readability):

```json
{
  "v": "B1",
  "d_id": "ESP32_1",
  "seq": 64,
  "temp": 25.38,
  "humi": 54.12,
  "st": true,
  "ts": 1731888000123
}
```

| Key | `cJSON` builder | Source | Type / Meaning |
| :--- | :--- | :--- | :--- |
| `"v"` | `cJSON_AddStringToObject` | `CONFIG_ALGORISM_VERSION` | String version tag |
| `"d_id"` | `cJSON_AddStringToObject` | `CONFIG_ESP_DEVICE_ID` | String device identifier |
| `"seq"` | `cJSON_AddNumberToObject` | `get_data.sequence` (`uint32_t`) | Monotonic sequence counter (NVS block-reserved in steps of 64) |
| `"temp"` | `cJSON_AddNumberToObject` | `get_data.temperature` (`float`) | Temperature in degrees Celsius (`0.0` when `st` is `false`) |
| `"humi"` | `cJSON_AddNumberToObject` | `get_data.humidity` (`float`) | Relative humidity in percent (`0.0` when `st` is `false`) |
| `"st"` | `cJSON_AddBoolToObject` | `get_data.state` (`uint8_t`) | Boolean read status (`true` on `sht3x_measure` `ESP_OK`, `false` on failure) |
| `"ts"` | `cJSON_AddNumberToObject` | `get_data.ts_ms` (`uint64_t`) | Unix epoch timestamp in milliseconds (`UTC0`) |

This repository contains only the ESP32 firmware; the companion HTTP JSON ingestion server is maintained in a separate repository.

## Build and flash

The top-level `CMakeLists.txt` requires CMake `3.22` and defines the project name `Paperdata`. `dependencies.lock` targets `esp32` on ESP-IDF `6.1.0` with managed dependencies `esp-idf-lib/sht3x` (`1.0.8`), `esp-idf-lib/i2cdev` (`2.1.2`), `esp-idf-lib/esp_idf_lib_helpers` (`1.4.0`), and `espressif/cjson` (`1.7.19~2`).

```bash
idf.py set-target esp32
idf.py menuconfig
idf.py build
idf.py -p <PORT> flash monitor
```

In `idf.py menuconfig`, configure `NETWORK_CFG` (`NETWORK_SSID`, `NETWORK_PASSWORD`), `HTTP Setting` (`HTTP_SERVER_URL`), `ESP_MAIN_SETTING` (`ESP_DEVICE_ID`), `SHT Sensor Configuration` (`ALGORISM_VERSION`), and the `i2cdev` SDA/SCL pin numbers (`CONFIG_I2CDEV_DEFAULT_SDA_PIN`, `CONFIG_I2CDEV_DEFAULT_SCL_PIN`).

## Limitations

- **Hardcoded SNTP host and blocking callback**: `NTP_SERVER_IP` is hardcoded in `components/network/network.h` rather than exposed via Kconfig. Inside `event_handler` (`IP_EVENT_STA_GOT_IP`), `esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000))` blocks the default `esp_event` loop task for up to 15 seconds if the SNTP server is unreachable, and subsequent reconnects call `esp_netif_sntp_init()` again without calling `esp_netif_sntp_deinit()`.
- **No sampling without SNTP sync**: `read_sht_data` spins on `network_time_synced()` before entering the measurement loop, so no readings are captured or buffered if SNTP fails on startup.
- **Volatile RAM cache and 127-item ceiling**: `json_buffer` holds at most `127` (`JSON_BUFFER_SIZE - 1`) `cJSON` strings in RAM. Power loss clears unsent records, and any new sample arriving while the buffer is full is freed and dropped.
- **No buffering for HTTP failures while Wi-Fi is connected**: `json_network_retrieve` only buffers into `json_buffer` when `WIFI_CONNECT_FAIL` (`BIT1`) is set. While `WIFI_CONNECT_SUCCESS` (`BIT0`) is set and the buffer is empty, `cJSON_free(json_receive)` is called unconditionally after `http_upload_build()`, discarding payloads if the HTTP endpoint returns non-2xx or times out while Wi-Fi remains associated. In addition, if a cached drain fails while `WIFI_CONNECT_SUCCESS` stays set, the `while` loop breaks and the outer loop immediately re-enters without backoff because `xEventGroupWaitBits(..., pdFALSE, ...)` does not clear `WIFI_CONNECT_SUCCESS`.
- **Disconnect state transitions**: `IP_EVENT_STA_LOST_IP` resumes `life_guard` without setting `WIFI_CONNECT_FAIL`, whereas `life_guard` blocks on `WIFI_CONNECT_FAIL` (set only by `WIFI_EVENT_STA_DISCONNECTED` after `MAX_RETRY` failures). `current_retry` is also reset only on `IP_EVENT_STA_GOT_IP`, so after hitting `MAX_RETRY` once, reconnects rely on `life_guard` until an IP is acquired.
- **NVS erase policy and runtime log clamping**: `app_main()` calls `esp_log_level_set("*", ESP_LOG_WARN);`, suppressing all `ESP_LOGI` output across components regardless of `CONFIG_ESP_DEBUG_LOGV`. On any non-`ESP_OK` return from `nvs_flash_init()`, `app_main()` calls `nvs_flash_erase()`, wiping stored `SEQ_NVS_NAMESPACE` / `SEQ_NVS_KEY` sequence state, and does not check the second `nvs_flash_init()` return code.
- **Unused struct fields and plain HTTP**: `sht_data_t` defines `device_id[16]` and `version[8]`, which are never populated (`feature.c` reads Kconfig strings directly). `http_init()` does not configure TLS verification (`cert_pem` or `crt_bundle_attach`).
