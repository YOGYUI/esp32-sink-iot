#ifndef _DEFINES_H_
#define _DEFINES_H_
#pragma once

#include <stdint.h>

#define ON      1
#define OFF     0

/* define default GPIO Pin Numbers */
#define DEFAULT_GPIO_PIN_RELAY          16
#define DEFAULT_GPIO_PIN_LED_1          18
#define DEFAULT_GPIO_PIN_LED_2          19
#define DEFAULT_GPIO_PIN_FLOW_SENSOR    4
#define DEFAULT_GPIO_PIN_PWM_TEST       5

/* define default MQTT broker info  */
#define DEFAULT_MQTT_BROKER_URI         "mqtt://127.0.0.1"
#define DEFAULT_MQTT_BROKER_PORT        1883
#define DEFAULT_MQTT_BROKER_USERNAME    "username"
#define DEFAULT_MQTT_BROKER_PASSWORD    "password"
#define DEFAULT_MQTT_BROKER_CLIENT_ID   "esp32-sinkvalve-iot"

/* define default MQTT topics */
#define DEFAULT_MQTT_PUBLISH_TOPIC_DEVICE   "home/hillstate/sinkvalve/state"
#define DEFAULT_MQTT_SUBSCRIBE_TOPIC_DEVICE "home/hillstate/sinkvalve/command"
#define DEFAULT_MQTT_SUBSCRIBE_TOPIC_OTA    "home/hillstate/sinkvalve/ota"

/* define default misc parameters */
#define DEFAULT_RELAY_TOGGLE_MS         100
#define DEFAULT_LED_DISPLAY_ADDR        0x70
#define DEFAULT_FLOW_PULSE_PER_LITER    660

#define OTA_FIRMWARE_URL                "ota_url/yogyui-esp32-sink-iot.bin"
#define PROV_AP_SSID_PREFIX             "YOGYUI_SINKVALVE_"
#define PROV_POP                        "abcd1234"
#define PROV_SOFTAP_PASSWD              "12345678"

/* NVS configuration storage */
#define NVS_NAMESPACE_CFG       "sink_cfg"
#define NVS_KEY_MQTT_URI        "mqtt_uri"
#define NVS_KEY_MQTT_PORT       "mqtt_port"
#define NVS_KEY_MQTT_USER       "mqtt_user"
#define NVS_KEY_MQTT_PASS       "mqtt_pass"
#define NVS_KEY_MQTT_CLIENT_ID  "mqtt_client_id"
#define NVS_KEY_AUTO_OFF_EN     "auto_off_en"
#define NVS_KEY_AUTO_OFF_TIME   "auto_off_t"
#define NVS_KEY_GPIO_RELAY      "gpio_relay"
#define NVS_KEY_GPIO_LED1       "gpio_led1"
#define NVS_KEY_GPIO_LED2       "gpio_led2"
#define NVS_KEY_GPIO_FLOW       "gpio_flow"
#define NVS_KEY_GPIO_PWM        "gpio_pwm"
#define NVS_KEY_MQTT_TOPIC_PUB  "mqtt_t_pub"
#define NVS_KEY_MQTT_TOPIC_SUB  "mqtt_t_sub"
#define NVS_KEY_MQTT_TOPIC_OTA  "mqtt_t_ota"
#define NVS_KEY_MISC_RELAY_MS   "misc_relay_ms"
#define NVS_KEY_MISC_FLOW_PPL   "misc_flow_ppl"
#define NVS_KEY_MISC_DISP_ADDR  "misc_disp_addr"
#define NVS_KEY_WIFI_SSID       "wifi_ssid"
#define NVS_KEY_WIFI_PASS       "wifi_pass"

/* GPIO pin configuration (runtime, loaded from NVS on boot) */
struct StGpioConfig {
    uint8_t pin_relay;
    uint8_t pin_led1;
    uint8_t pin_led2;
    uint8_t pin_flow;
    uint8_t pin_pwm;

    StGpioConfig() :
        pin_relay(DEFAULT_GPIO_PIN_RELAY),
        pin_led1(DEFAULT_GPIO_PIN_LED_1),
        pin_led2(DEFAULT_GPIO_PIN_LED_2),
        pin_flow(DEFAULT_GPIO_PIN_FLOW_SENSOR),
        pin_pwm(DEFAULT_GPIO_PIN_PWM_TEST) {}
};

extern StGpioConfig* gpio_cfg;

/* Misc runtime configuration (loaded from NVS on boot) */
struct StMiscConfig {
    uint32_t relay_toggle_ms;
    uint32_t flow_pulse_per_liter;
    uint8_t  display_slave_addr;

    StMiscConfig() :
        relay_toggle_ms(DEFAULT_RELAY_TOGGLE_MS),
        flow_pulse_per_liter(DEFAULT_FLOW_PULSE_PER_LITER),
        display_slave_addr(DEFAULT_LED_DISPLAY_ADDR) {}
};
extern StMiscConfig* misc_cfg;
void load_misc_config();

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