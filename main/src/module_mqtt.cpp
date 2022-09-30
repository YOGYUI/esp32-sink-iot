#include "module_mqtt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include "defines.h"
#include <cstring>
#include "cJSON.h"
#include "module_gpio.h"
#include "module_ota.h"

static const char *TAG = "MQTT";
static esp_mqtt_client_handle_t mqtt_client = nullptr;
// static bool free_to_publish = true;

static void subscribe_topics() {
    int msg_id;
    msg_id = esp_mqtt_client_subscribe(mqtt_client, MQTT_SUBSCRIBE_TOPIC_DEVICE, 0);
    ESP_LOGI(TAG, "sent subscribe, msg_id=%d", msg_id);
    msg_id = esp_mqtt_client_subscribe(mqtt_client, MQTT_SUBSCRIBE_TOPIC_OTA, 0);
    ESP_LOGI(TAG, "sent subscribe, msg_id=%d", msg_id);
}

bool mqtt_publish_current_state() {
    if (!mqtt_client)
        return false;
    
    /*
    if (!free_to_publish)
        return false; 
    free_to_publish = false;
    */

   ESP_LOGI(TAG, "current state -  is_active: %d, pulse_count_per_sec: %d, pulse_count_accum: %llu", 
        flow_sensor->is_active, flow_sensor->pulse_count_per_sec, flow_sensor->pulse_count_accum);

    cJSON* obj = cJSON_CreateObject();
    if (obj == nullptr)
        return false;
    
    cJSON* item_state = cJSON_CreateNumber(flow_sensor->is_active);
    if (item_state) {
        cJSON_AddItemToObject(obj, "state", item_state);
    }

    double flow_rate = (double)flow_sensor->pulse_count_per_sec / (double)FLOW_SENSOR_PULSE_PER_LITER;    // unit: L/sec 
    cJSON* item_flow_rate = cJSON_CreateNumber(flow_rate);
    if (item_flow_rate) {
        cJSON_AddItemToObject(obj, "flow_rate", item_flow_rate);
    }

    cJSON* item_pulse_count_acc = cJSON_CreateNumber(flow_sensor->pulse_count_accum);
    if (item_pulse_count_acc) {
        cJSON_AddItemToObject(obj, "pulse_cnt_acc", item_pulse_count_acc);
    }

        double volume = (double)flow_sensor->pulse_count_accum / (double)FLOW_SENSOR_PULSE_PER_LITER;
    cJSON* item_flow_volume = cJSON_CreateNumber(volume);
    if (item_flow_volume) {
        cJSON_AddItemToObject(obj, "volume", item_flow_volume);
    }

    cJSON* item_auto_off = cJSON_CreateNumber(flow_sensor->enable_auto_off);
    if (item_auto_off) {
        cJSON_AddItemToObject(obj, "auto_off", item_auto_off);
    }

    cJSON* item_off_time = cJSON_CreateNumber(flow_sensor->auto_off_time);
    if (item_auto_off) {
        cJSON_AddItemToObject(obj, "off_time", item_off_time);
    }

    char* payload = cJSON_Print(obj);
    if (payload) {
        esp_mqtt_client_publish(mqtt_client, MQTT_PUBLISH_TOPIC_DEVICE, payload, 0, 1, 0);
        // ESP_LOGI(TAG, "try to publish (topic: %s, payload: %s)", MQTT_PUBLISH_TOPIC_DEVICE, payload);
    } else {
        return false;
    }

    return true;
}

static void parse_recv_message(const char* payload, size_t payload_len) {
    cJSON* obj = cJSON_ParseWithLength(payload, payload_len);
    if (obj == nullptr) {
        const char* err = cJSON_GetErrorPtr();
        if (err) {
            ESP_LOGE(TAG, "Payload JSON parse error: %s", err);
        }
    } else {
        ESP_LOGI(TAG, "payload parse result: %s", cJSON_Print(obj));

        const cJSON* state = cJSON_GetObjectItemCaseSensitive(obj, "state");
        if (cJSON_IsNumber(state)) {
            int target_state = state->valueint;
            if (!target_state) {    // 외부로부터의 끄기 명령
                flow_sensor->got_off_command = true;
            }
            
            // 현재 및 타겟 상태값에 관계없이 풋스위치가 눌린 것처럼 행동
            blink_relay();
        }

        const cJSON* auto_off = cJSON_GetObjectItemCaseSensitive(obj, "auto_off");
        if (cJSON_IsNumber(auto_off)) {
            flow_sensor->enable_auto_off = (bool)auto_off->valueint;
            ESP_LOGI(TAG, "Flow sensor enable auto off: %d", flow_sensor->enable_auto_off);
        }

        const cJSON* off_time = cJSON_GetObjectItemCaseSensitive(obj, "off_time");
        if (cJSON_IsNumber(off_time)) {
            flow_sensor->auto_off_time = off_time->valueint;
            ESP_LOGI(TAG, "Flow sensor auto off time: %lld sec", flow_sensor->auto_off_time);
        }
    }
}

static void mqtt_event_handler(void* arg, esp_event_base_t base, int32_t evt_id, void* evt_data) {
    esp_mqtt_event_t* event = reinterpret_cast<esp_mqtt_event_t*>(evt_data);
    // esp_mqtt_client_handle_t client = event->client;
    esp_mqtt_event_id_t event_id = (esp_mqtt_event_id_t)evt_id;
    switch (event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Connected");
            subscribe_topics();
            mqtt_publish_current_state();
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGE(TAG, "Disconnected");
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "subscribe event, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "publish event, msg_id=%d", event->msg_id);
            // free_to_publish = true;
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "message arrived [%.*s] %.*s", event->topic_len, event->topic, event->data_len, event->data);
            if (!strncmp(event->topic, MQTT_SUBSCRIBE_TOPIC_DEVICE, strlen(MQTT_SUBSCRIBE_TOPIC_DEVICE))) {
                parse_recv_message(event->data, event->data_len);
            } else if (!strncmp(event->topic, MQTT_SUBSCRIBE_TOPIC_OTA, strlen(MQTT_SUBSCRIBE_TOPIC_OTA))) {
                initialize_ota();
            }
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "Error");
            break;
        default:
            ESP_LOGI(TAG, "Unhandled Event ID - %d", event_id);
            break;
    }
}

bool initialize_mqtt() {
    esp_mqtt_client_config_t config = {};
    config.uri = MQTT_BROKER_URI;
    config.port = MQTT_BROKER_PORT;
    config.username = MQTT_BROKER_USERNAME;
    config.password = MQTT_BROKER_PASSWORD;

    mqtt_client = esp_mqtt_client_init(&config);
    if (esp_mqtt_client_register_event(mqtt_client, MQTT_EVENT_ANY, mqtt_event_handler, nullptr) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register mqtt client event");
        return false;
    } 

    if (esp_mqtt_client_start(mqtt_client) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start mqtt client");
        return false;
    }

    ESP_LOGI(TAG, "Configured MQTT");

    return true;
}