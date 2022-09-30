#include "module_timer.h"
#include "esp_log.h"
#include "defines.h"

static const char *TAG = "TIMER";

bool initialize_timer1(uint64_t period_us, esp_timer_cb_t callback) {
    esp_timer_create_args_t args = {};
    args.callback = callback;
    args.name = "timer1-periodic";

    esp_timer_handle_t handle;
    if (esp_timer_create(&args, &handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create timer1");
        return false;
    }

    if (esp_timer_start_periodic(handle, period_us) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start periodic timer1");
        return false;
    }

    ESP_LOGI(TAG, "Configured Timer1");
    return true;
}

bool initialize_timer2(uint64_t period_us, esp_timer_cb_t callback) {
    esp_timer_create_args_t args = {};
    args.callback = callback;
    args.name = "timer2-periodic";

    esp_timer_handle_t handle;
    if (esp_timer_create(&args, &handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create timer2");
        return false;
    }

    if (esp_timer_start_periodic(handle, period_us) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start periodic timer2");
        return false;
    }

    ESP_LOGI(TAG, "Configured Timer2");
    return true;
}