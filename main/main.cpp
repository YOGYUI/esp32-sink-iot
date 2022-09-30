#include <stdio.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "sdkconfig.h"
#include "defines.h"
#include "module_display.h"
#include "module_gpio.h"
#include "module_mqtt.h"
#include "module_pulse_counter.h"
#include "module_pwm.h"
#include "module_sntp.h"
#include "module_timer.h"
#include "module_wifi_provisioning.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

static uint32_t timer_count1 = 0;
static char disp_buffer[8] = {0, };
static bool waiting_for_auto_off_done = false;

static void timer1_callback(void *arg) {    
    flow_sensor->pulse_count_per_sec = get_pulse_count();
    flow_sensor->pulse_count_accum += (uint64_t)flow_sensor->pulse_count_per_sec;

    if (flow_sensor->pulse_count_per_sec > 0) {  // water flow active
        flow_sensor->is_active = true;
        ESP_LOGI("MAIN_TIMER", "water flow active, pulse cnt: %d", flow_sensor->pulse_count_per_sec);

        turn_led2(ON);
        snprintf(disp_buffer, 8, "%04d", flow_sensor->pulse_count_per_sec);
        for (int i = 0; i < 4; i++) {
            display_write_ascii(i, disp_buffer[i]);
        }
        display_refresh();

        if (!flow_sensor->got_off_command) {
            // 물이 흐를 때는 유량 변동이 있으므로 타이머 트리거마다 publish
            // 외부로부터 끄기 명령 받았을 때 잔여유량에 의해 state=1이 publish되는 경우가 없도록 조건문
            mqtt_publish_current_state();
        }

        if (!flow_sensor->is_active_prev) {  // state transition (non active -> active)
            flow_sensor->tick_flow_started = get_tick_ms();
            flow_sensor->is_active_prev = true;
            ESP_LOGI("MAIN_TIMER", "Water flow started (tick: %g)", flow_sensor->tick_flow_started);
        }
    } else {    // water flow not active
        flow_sensor->is_active = false;
        flow_sensor->got_off_command = false;
        waiting_for_auto_off_done = false;

        turn_led2(OFF);
        display_clear();
        display_refresh();

        if (flow_sensor->is_active_prev) {   // state transition (active -> non active)
            mqtt_publish_current_state();   // 상태 전이시에만 publish
            flow_sensor->is_active_prev = false;
            flow_sensor->tick_flow_stopped = get_tick_ms();
            double flow_time = flow_sensor->tick_flow_stopped - flow_sensor->tick_flow_started;
            ESP_LOGI("MAIN_TIMER", "Water flow stopped (tick: %g, accum_pulse: %llu, flow time: %g sec)", 
                flow_sensor->tick_flow_stopped, flow_sensor->pulse_count_accum, flow_time);
            flow_sensor->pulse_count_accum = 0;
        }
    }

    timer_count1++;
    if (timer_count1 >= 30) { 
        // 30초에 한번 정기적 publish
        timer_count1 = 0;
        if (!flow_sensor->is_active)
            mqtt_publish_current_state();
    }

    clear_pulse_counter();
}

static void timer2_callback(void *arg) {
    if (flow_sensor->is_active) {
        if (flow_sensor->enable_auto_off && !waiting_for_auto_off_done) {
            // 자동 절수 기능 (물이 켜진 후 일정시간 지나면 자동으로 릴레이 토글)
            double tick_diff = get_tick_ms() - flow_sensor->tick_flow_started;
            if (tick_diff >= flow_sensor->auto_off_time) {
                blink_relay();
                waiting_for_auto_off_done = true;
            }
        }
    }
}

extern "C" void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    initialize_display();
    initialize_gpio();
    initialize_pulse_counter();
//  initialize_pwm();   // pcnt 테스트용
    initialize_timer1(1000000, timer1_callback);    // 1초 주기로 유량계 펄스 카운터 값 체크 및 상태 업데이트
    initialize_timer2(100000, timer2_callback);     // 100ms 주기로 유량계 On-Time Check
    turn_led1(OFF);
    turn_led2(OFF);
    initialize_wifi_provisioning();

    for (;;) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}