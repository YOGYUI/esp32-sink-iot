/* Just for testing pulse counter module */
#include "module_pwm.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "defines.h"

static const char *TAG = "PWM";

bool initialize_pwm() {
    ledc_timer_config_t tm_config = {};
    tm_config.speed_mode = LEDC_LOW_SPEED_MODE;
    tm_config.duty_resolution = LEDC_TIMER_10_BIT;
    tm_config.timer_num = LEDC_TIMER_1;
    tm_config.freq_hz = 100;
    tm_config.clk_cfg = LEDC_AUTO_CLK;
    if (ledc_timer_config(&tm_config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC timer");
        return false;
    }

    ledc_channel_config_t ch_config = {};
    ch_config.gpio_num = GPIO_PIN_PWM_TEST;
    ch_config.speed_mode = LEDC_LOW_SPEED_MODE;
    ch_config.channel = LEDC_CHANNEL_1;
    ch_config.intr_type = LEDC_INTR_DISABLE;
    ch_config.timer_sel = LEDC_TIMER_1;
    ch_config.duty = 512;
    ch_config.hpoint = 0;
    if (ledc_channel_config(&ch_config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC channel");
        return false;
    }

    ESP_LOGI(TAG, "Configured PWM");
    return true;
}