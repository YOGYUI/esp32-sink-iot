#ifndef _DEFINES_H_
#define _DEFINES_H_
#pragma once

#include <stdint.h>

#define ON      1
#define OFF     0

/* define GPIO Pin Numbers */
#define GPIO_PIN_RELAY                  16
#define GPIO_PIN_LED_1                  18
#define GPIO_PIN_LED_2                  19
#define GPIO_PIN_FLOW_SENSOR            4
#define GPIO_PIN_PWM_TEST               5

/* define MQTT broker info  */
#define MQTT_BROKER_URI                 "mqtt://broker_address"
#define MQTT_BROKER_PORT                1883
#define MQTT_BROKER_USERNAME            "broker_auth_id"
#define MQTT_BROKER_PASSWORD            "broker_auth_password"

#define MQTT_PUBLISH_TOPIC_DEVICE       "home/hillstate/sinkvalve/state"
#define MQTT_SUBSCRIBE_TOPIC_DEVICE     "home/hillstate/sinkvalve/command"
#define MQTT_SUBSCRIBE_TOPIC_OTA        "home/hillstate/sinkvalve/ota"

/* define misc parameters */
#define RELAY_TOGGLE_INTERVAL_MS        100
#define LED_DISPLAY_SLAVE_ADDR          0x70
#define OTA_FIRMWARE_URL                "ota_url/yogyui-esp32-sink-iot.bin"
#define BLE_PROV_AP_PREFIX              "YOGYUI_"
#define BLE_PROV_POP                    "12345678"
#define FLOW_SENSOR_PULSE_PER_LITER     660         // YF-B2 spec.

/* define flow sensor status class */
struct StFlowSensor {
    bool        is_active;              // false = no flow, true = flow
    bool        is_active_prev;         // 이전 상태값 저장
    int16_t     pulse_count_per_sec;    // 초당 유량계 펄스 개수 
    uint64_t    pulse_count_accum;      // 물이 흐르는 동안의 총 펄스 개수 (YF-B2: 660 pulse / Liter)
    bool        enable_auto_off;        // 자동 절수 기능 활성화 여부
    int64_t     auto_off_time;          // 자동 절수 시간 (단위: 초)
    double      tick_flow_started;      // 물이 흐르기 시작한 시점의 틱
    double      tick_flow_stopped;      // 물이 흐리지 않게 된 시점의 틱
    bool        got_off_command;        // 외부로부터 밸브 잠금 명령 받았는지 여부 플래그

    StFlowSensor() {
        is_active               = false;
        is_active_prev          = false;
        pulse_count_per_sec     = 0;
        pulse_count_accum       = 0;
        enable_auto_off         = false;
        auto_off_time           = 5;
        tick_flow_started       = 0.;
        tick_flow_stopped       = 0.;
        got_off_command         = false;
    }
};

extern StFlowSensor* flow_sensor;    // 전역변수로 설정

#endif