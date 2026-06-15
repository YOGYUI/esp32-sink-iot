#include "module_mqtt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include "defines.h"
#include "nvs.h"
#include <cstring>
#include "cJSON.h"
#include "module_gpio.h"
#include "module_ota.h"

static const char *TAG = "MQTT";
static esp_mqtt_client_handle_t mqtt_client = nullptr;

static char s_mqtt_uri[128];
static char s_mqtt_user[64];
static char s_mqtt_pass[64];
static char s_mqtt_client_id[64];
static uint16_t s_mqtt_port;
static char s_topic_pub[128];
static char s_topic_sub[128];
static char s_topic_ota[128];
static bool is_valid_connection = false;

static void load_mqtt_config() {
    strncpy(s_mqtt_uri, DEFAULT_MQTT_BROKER_URI, sizeof(s_mqtt_uri) - 1);
    strncpy(s_mqtt_user, DEFAULT_MQTT_BROKER_USERNAME, sizeof(s_mqtt_user) - 1);
    strncpy(s_mqtt_pass, DEFAULT_MQTT_BROKER_PASSWORD, sizeof(s_mqtt_pass) - 1);
    strncpy(s_mqtt_client_id, DEFAULT_MQTT_BROKER_CLIENT_ID, sizeof(s_mqtt_client_id) - 1);
    strncpy(s_topic_pub, DEFAULT_MQTT_PUBLISH_TOPIC_DEVICE, sizeof(s_topic_pub) - 1);
    strncpy(s_topic_sub, DEFAULT_MQTT_SUBSCRIBE_TOPIC_DEVICE, sizeof(s_topic_sub) - 1);
    strncpy(s_topic_ota, DEFAULT_MQTT_SUBSCRIBE_TOPIC_OTA, sizeof(s_topic_ota) - 1);
    s_mqtt_port = DEFAULT_MQTT_BROKER_PORT;

    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE_CFG, NVS_READONLY, &nvs) != ESP_OK)
        return;

    size_t len;
    len = sizeof(s_mqtt_uri);       nvs_get_str(nvs, NVS_KEY_MQTT_URI,       s_mqtt_uri,       &len);
    len = sizeof(s_mqtt_user);      nvs_get_str(nvs, NVS_KEY_MQTT_USER,      s_mqtt_user,      &len);
    len = sizeof(s_mqtt_pass);      nvs_get_str(nvs, NVS_KEY_MQTT_PASS,      s_mqtt_pass,      &len);
    len = sizeof(s_mqtt_client_id); nvs_get_str(nvs, NVS_KEY_MQTT_CLIENT_ID, s_mqtt_client_id, &len);
    len = sizeof(s_topic_pub);      nvs_get_str(nvs, NVS_KEY_MQTT_TOPIC_PUB, s_topic_pub,      &len);
    len = sizeof(s_topic_sub);      nvs_get_str(nvs, NVS_KEY_MQTT_TOPIC_SUB, s_topic_sub,      &len);
    len = sizeof(s_topic_ota);      nvs_get_str(nvs, NVS_KEY_MQTT_TOPIC_OTA, s_topic_ota,      &len);
    nvs_get_u16(nvs, NVS_KEY_MQTT_PORT, &s_mqtt_port);
    nvs_close(nvs);

    ESP_LOGI(TAG, "MQTT: uri=%s port=%d user=%s", s_mqtt_uri, s_mqtt_port, s_mqtt_user);
    ESP_LOGI(TAG, "MQTT topics: pub=%s sub=%s ota=%s", s_topic_pub, s_topic_sub, s_topic_ota);
}
// static bool free_to_publish = true;

static void subscribe_topics() {
    int msg_id;
    msg_id = esp_mqtt_client_subscribe(mqtt_client, s_topic_sub, 0);
    ESP_LOGI(TAG, "sent subscribe (cmd), msg_id=%d", msg_id);
    msg_id = esp_mqtt_client_subscribe(mqtt_client, s_topic_ota, 0);
    ESP_LOGI(TAG, "sent subscribe (ota), msg_id=%d", msg_id);
}

bool mqtt_publish_current_state() {
    if (!mqtt_client)
        return false;
    
    /*
    if (!free_to_publish)
        return false; 
    free_to_publish = false;
    */

   ESP_LOGI(TAG, "current state - is_active: %d, pulse_count_per_sec: %d, pulse_count_accum: %llu", 
        flow_sensor->is_active, flow_sensor->pulse_count_per_sec, flow_sensor->pulse_count_accum);

    cJSON* obj = cJSON_CreateObject();
    if (obj == nullptr)
        return false;
    
    cJSON* item_state = cJSON_CreateNumber(flow_sensor->is_active);
    if (item_state) {
        cJSON_AddItemToObject(obj, "state", item_state);
    }

    double flow_rate = (double)flow_sensor->pulse_count_per_sec / (double)misc_cfg->flow_pulse_per_liter;    // unit: L/sec 
    cJSON* item_flow_rate = cJSON_CreateNumber(flow_rate);
    if (item_flow_rate) {
        cJSON_AddItemToObject(obj, "flow_rate", item_flow_rate);
    }

    cJSON* item_pulse_count_acc = cJSON_CreateNumber(flow_sensor->pulse_count_accum);
    if (item_pulse_count_acc) {
        cJSON_AddItemToObject(obj, "pulse_cnt_acc", item_pulse_count_acc);
    }

    double volume = (double)flow_sensor->pulse_count_accum / (double)misc_cfg->flow_pulse_per_liter;
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
        esp_mqtt_client_publish(mqtt_client, s_topic_pub, payload, 0, 1, 0);
        cJSON_free(payload);
    } else {
        return false;
    }

    cJSON_Delete(obj);

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
        char* payload = cJSON_Print(obj);
        if (payload) {
            ESP_LOGI(TAG, "payload parse result: %s", payload);
            cJSON_free(payload);
        }

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

        cJSON_Delete(obj);
    }
}

static void mqtt_event_handler(void* arg, esp_event_base_t base, int32_t evt_id, void* evt_data) {
    esp_mqtt_event_t* event = reinterpret_cast<esp_mqtt_event_t*>(evt_data);
    esp_mqtt_client_handle_t client = event->client;
    esp_mqtt_event_id_t event_id = (esp_mqtt_event_id_t)evt_id;
    switch (event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Connected");
            is_valid_connection = true;
            subscribe_topics();
            mqtt_publish_current_state();
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGE(TAG, "Disconnected");
            if (is_valid_connection) {
                ESP_LOGI(TAG, "Try to reconnect");
                esp_mqtt_client_reconnect(client);
            }
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
            if (!strncmp(event->topic, s_topic_sub, strlen(s_topic_sub))) {
                parse_recv_message(event->data, event->data_len);
            } else if (!strncmp(event->topic, s_topic_ota, strlen(s_topic_ota))) {
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

bool mqtt_is_connected() { 
    return is_valid_connection; 
}

bool initialize_mqtt() {
    load_mqtt_config();

    esp_mqtt_client_config_t config = {};
    config.broker.address.uri = s_mqtt_uri;
    config.broker.address.port = s_mqtt_port;
    config.credentials.client_id = s_mqtt_client_id;
    config.credentials.username = s_mqtt_user;
    config.credentials.authentication.password = s_mqtt_pass;
    
    mqtt_client = esp_mqtt_client_init(&config);
    if (esp_mqtt_client_register_event(mqtt_client, MQTT_EVENT_ANY, mqtt_event_handler, nullptr) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register mqtt client event");
        return false;
    } 

    ESP_LOGI(TAG, "Initialized MQTT");

    return true;
}

bool start_mqtt() {
    if (!mqtt_client) {
        ESP_LOGE(TAG, "mqtt client is not initialized");
        return false;
    }

    if (esp_mqtt_client_start(mqtt_client) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start mqtt client");
        return false;
    }

    ESP_LOGI(TAG, "MQTT Client started");

    return true;
}

bool stop_mqtt() {
    if (!mqtt_client) {
        ESP_LOGE(TAG, "mqtt client is not initialized");
        return false;
    }

    if (esp_mqtt_client_stop(mqtt_client) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop mqtt client");
        return false;
    }

    ESP_LOGI(TAG, "MQTT Client stopped");

    return true;
}