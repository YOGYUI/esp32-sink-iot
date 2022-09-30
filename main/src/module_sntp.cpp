#include "module_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sntp.h"
#include "lwip/apps/sntp.h"
#include "esp_log.h"
#include "defines.h"
#include <chrono>

static const char *TAG = "SNTP";

void callback_time_sync(struct timeval *tv) {
    ESP_LOGI(TAG, "syncronized time");
    setenv("TZ", "KST-9", 1);   // 한국: 표준시 - 9
    tzset();

    time_t now;
    struct tm timeinfo;
    char buf[64];
    
    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(buf, sizeof(buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "current time: %s", buf);
}

bool initialize_sntp() {
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_set_time_sync_notification_cb(callback_time_sync);
    // sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);
    sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);

    ESP_LOGI(TAG, "start time syncronization");
    sntp_init();

    int retry_cnt = 0;
    const int retry_limit = 10;

    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry_cnt < retry_limit) {
        ESP_LOGI(TAG, "SNTP process (%d/%d)", retry_cnt, retry_limit);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }

    return (retry_cnt < retry_limit);
}

double get_tick_ms() {
    auto now = std::chrono::system_clock::now();
    time_t tm_now = std::chrono::system_clock::to_time_t(now);
    auto duration = now.time_since_epoch();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration) % 1000;

    return (double)tm_now + (double)millis.count() / 1000.; 
}