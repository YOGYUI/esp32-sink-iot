#include "module_gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "defines.h"

static const char *TAG = "GPIO";
static uint32_t level_led1 = 0;
static uint32_t level_led2 = 0;

bool initialize_gpio() {
    gpio_config_t config = {};

    // LED & Relay Signal (out)
    config.pin_bit_mask = ( (1ULL << GPIO_PIN_LED_1) |   
                            (1ULL << GPIO_PIN_LED_2) |   
                            (1ULL << GPIO_PIN_RELAY) );
    config.mode = GPIO_MODE_OUTPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    if (gpio_config(&config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO");
        return false;
    }

    ESP_LOGI(TAG, "Configured GPIO");
    return true;
}

bool turn_led1(uint32_t v) {
    level_led1 = v;
    if (gpio_set_level((gpio_num_t)GPIO_PIN_LED_1, level_led1) != ESP_OK) {
        return false;
    }
    return true;
}

bool toggle_led1() {
    return turn_led1(!level_led1);
}

bool turn_led2(uint32_t v) {
    level_led2 = v;
    if (gpio_set_level((gpio_num_t)GPIO_PIN_LED_2, level_led2) != ESP_OK) {
        return false;
    }
    return true;
}

bool toggle_led2() {
    return turn_led2(!level_led2);
}

bool turn_relay(uint32_t v) {
    if (gpio_set_level((gpio_num_t)GPIO_PIN_RELAY, v) != ESP_OK) {
        return false;
    }

    ESP_LOGI(TAG, "Set relay signal: %d", v);
    return true;
}

bool blink_relay() {
    if (!turn_relay(ON))
        return false;
    
    vTaskDelay(RELAY_TOGGLE_INTERVAL_MS / portTICK_PERIOD_MS);
    
    if (!turn_relay(OFF))
        return false;

    return true;
}