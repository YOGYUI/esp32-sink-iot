#include "module_pulse_counter.h"
#include "driver/pcnt.h"
#include "esp_log.h"
#include "defines.h"

static const char *TAG = "PCNT";
static const pcnt_unit_t pcnt_unit = PCNT_UNIT_0;

bool initialize_pulse_counter() {
    pcnt_config_t config = {};

    config.pulse_gpio_num = GPIO_PIN_FLOW_SENSOR;   // 유량계 펄스 신호 입력핀
    
    config.ctrl_gpio_num = 2;                       // Control GPIO (사용하지 않는 핀으로 할당 - floating)
    config.lctrl_mode = PCNT_MODE_REVERSE;          // LOW(gnd)일 경우 cound down
    config.hctrl_mode = PCNT_MODE_KEEP;             // HIGH(floating)일 경우 count Up

    config.pos_mode = PCNT_CHANNEL_EDGE_ACTION_INCREASE;    // 상승엣지에서 카운트 업
    config.neg_mode = PCNT_CHANNEL_EDGE_ACTION_HOLD;        // 하강엣지에서는 카운트 홀드 (변경 X)
    config.counter_h_lim = 0x7FFF;                          // 카운터 최대값 리미트
    config.counter_l_lim = 0;                               // 카운터 최소값 리미트
    config.unit = pcnt_unit;
    config.channel = PCNT_CHANNEL_0;
    if (pcnt_unit_config(&config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure PCNT");
        return false;
    }

    clear_pulse_counter();
    ESP_LOGI(TAG, "Configured PCNT");

    return true;
}

bool clear_pulse_counter() {
    if (pcnt_counter_pause(pcnt_unit) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to pause PCNT counter");
        return false;
    }

    if (pcnt_counter_clear(pcnt_unit) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear PCNT counter");
        return false;
    }

    if (pcnt_counter_resume(pcnt_unit) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to resume PCNT counter");
        return false;
    }
//  ESP_LOGI(TAG, "Count cleared");

    return true;
}

int16_t get_pulse_count() {
    int16_t cnt;
    if (pcnt_get_counter_value(pcnt_unit, &cnt) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get PCNT count");
        return -1;
    }

    return cnt;
}
