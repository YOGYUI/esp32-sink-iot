#include "module_ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
// #include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "defines.h"

static const char *TAG = "OTA";
/*
extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
*/

esp_err_t _http_event_handler(esp_http_client_event_t* event) {
    switch (event->event_id) {
        case HTTP_EVENT_ERROR:
            break;
        case HTTP_EVENT_ON_CONNECTED:
            break;
        case HTTP_EVENT_HEADER_SENT:
            break;
        case HTTP_EVENT_ON_HEADER:
            break;
        case HTTP_EVENT_ON_DATA:
            break;
        case HTTP_EVENT_ON_FINISH:
            break;
        case HTTP_EVENT_DISCONNECTED:
            break;
        default:
            break;
    }

    return ESP_OK;
}

void ota_task(void* pvParameter) {
    esp_http_client_config_t config = {};
    config.url = OTA_FIRMWARE_URL;
    // config.cert_pem = (char *)server_cert_pem_start;
    config.event_handler = _http_event_handler;
    config.keep_alive_enable = true;

    if (esp_https_ota(&config) == ESP_OK) {
        ESP_LOGI(TAG, "Firmware Updated");
        esp_restart();
    } else {
        ESP_LOGE(TAG, "Failed");
    }
}

bool initialize_ota() {
    xTaskCreate(&ota_task, "OTA Task", 8192, nullptr, 5, nullptr);
    return true;
}