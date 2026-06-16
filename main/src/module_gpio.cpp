#include "module_gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "nvs.h"
#include "defines.h"

static const char *TAG = "GPIO";
static uint32_t level_led1 = 0;
static uint32_t level_led2 = 0;

static void load_gpio_config() {
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE_CFG, NVS_READONLY, &nvs) != ESP_OK)
        return;

    uint8_t val;
    if (nvs_get_u8(nvs, NVS_KEY_GPIO_RELAY, &val) == ESP_OK) gpio_cfg->pin_relay = val;
    if (nvs_get_u8(nvs, NVS_KEY_GPIO_LED1,  &val) == ESP_OK) gpio_cfg->pin_led1  = val;
    if (nvs_get_u8(nvs, NVS_KEY_GPIO_LED2,  &val) == ESP_OK) gpio_cfg->pin_led2  = val;
    if (nvs_get_u8(nvs, NVS_KEY_GPIO_FLOW,  &val) == ESP_OK) gpio_cfg->pin_flow  = val;
    if (nvs_get_u8(nvs, NVS_KEY_GPIO_PWM,   &val) == ESP_OK) gpio_cfg->pin_pwm   = val;

    nvs_close(nvs);
    ESP_LOGI(TAG, "GPIO pins — relay:%d led1:%d led2:%d flow:%d pwm:%d",
             gpio_cfg->pin_relay, gpio_cfg->pin_led1, gpio_cfg->pin_led2,
             gpio_cfg->pin_flow, gpio_cfg->pin_pwm);
}

bool initialize_gpio() {
    load_gpio_config();

    gpio_config_t config = {};

    // LED & Relay Signal (out)
    config.pin_bit_mask = ( (1ULL << gpio_cfg->pin_led1)  |
                            (1ULL << gpio_cfg->pin_led2)  |
                            (1ULL << gpio_cfg->pin_relay) );
    config.mode         = GPIO_MODE_OUTPUT;
    config.pull_up_en   = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type    = GPIO_INTR_DISABLE;
    if (gpio_config(&config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO");
        return false;
    }

    ESP_LOGI(TAG, "Configured GPIO");
    return true;
}

bool turn_led1(uint32_t v) {
    level_led1 = v;
    return gpio_set_level((gpio_num_t)gpio_cfg->pin_led1, level_led1) == ESP_OK;
}

bool toggle_led1() {
    return turn_led1(!level_led1);
}

bool turn_led2(uint32_t v) {
    level_led2 = v;
    return gpio_set_level((gpio_num_t)gpio_cfg->pin_led2, level_led2) == ESP_OK;
}

bool toggle_led2() {
    return turn_led2(!level_led2);
}

bool turn_relay(uint32_t v) {
    if (gpio_set_level((gpio_num_t)gpio_cfg->pin_relay, v) != ESP_OK)
        return false;

    ESP_LOGI(TAG, "Set relay signal: %d", v);
    return true;
}

bool blink_relay() {
    if (!turn_relay(ON))
        return false;
    
    vTaskDelay(misc_cfg->relay_toggle_ms / portTICK_PERIOD_MS);
    
    if (!turn_relay(OFF))
        return false;

    return true;
}