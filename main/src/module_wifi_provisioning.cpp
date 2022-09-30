#include "module_wifi_provisioning.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"
#include <cstring>
#include "module_gpio.h"
#include "module_mqtt.h"
#include "module_sntp.h"
#include "esp_log.h"
#include "defines.h"

static const char *TAG = "WIFI_PROV";
static EventGroupHandle_t wifi_event_group;

static void do_extra_job_after_wifi_connected() {
    turn_led1(ON);
    initialize_sntp();
    initialize_mqtt();
}

static void wifi_prov_event_handler(void* arg, esp_event_base_t evt_base, int32_t evt_id, void* evt_data) {
    if (evt_base == WIFI_PROV_EVENT) {
        if (evt_id == WIFI_PROV_INIT) {
            ESP_LOGI(TAG, "Provisioning Initiated");
        } else if (evt_id == WIFI_PROV_START) {
            ESP_LOGI(TAG, "Provisioning Started");
        } else if (evt_id == WIFI_PROV_CRED_RECV) {
            wifi_sta_config_t* cfg = reinterpret_cast<wifi_sta_config_t*>(evt_data);
            ESP_LOGI(TAG, "Received Wi-Fi credentials (SSID=%s, PASSWD=%s)", (const char*)cfg->ssid, (const char*)cfg->password);
        } else if (evt_id == WIFI_PROV_CRED_FAIL) {
            wifi_prov_sta_fail_reason_t* reason = reinterpret_cast<wifi_prov_sta_fail_reason_t*>(evt_data);
            if (*reason == WIFI_PROV_STA_AUTH_ERROR) {
                ESP_LOGE(TAG, "Failed provisioning (reason: Wi-Fi station authentication failed)");
            } else {
                ESP_LOGE(TAG, "Failed provisioning (reason: cannot find access-point)");
            }
        } else if (evt_id == WIFI_PROV_CRED_SUCCESS) {
            ESP_LOGI(TAG, "Provisioning Finished");
        } else if (evt_id == WIFI_PROV_END) {
            wifi_prov_mgr_deinit();
            ESP_LOGI(TAG, "Provisioning terminated");
        } else if (evt_id == WIFI_PROV_DEINIT) {
            ESP_LOGI(TAG, "Provisioning deinitialized");
        } else {
            ESP_LOGE(TAG, "Unhandled event id - %d", evt_id);
        }
    } else if (evt_base == WIFI_EVENT && evt_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WI-FI STA Started");
        esp_wifi_connect();
    } else if (evt_base == IP_EVENT && evt_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = reinterpret_cast<ip_event_got_ip_t*>(evt_data);
        ESP_LOGI(TAG, "Got IP Address: " IPSTR, IP2STR(&event->ip_info.ip));
        do_extra_job_after_wifi_connected();
        xEventGroupSetBits(wifi_event_group, BIT0);
    } else if (evt_base == WIFI_EVENT && evt_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WI-FI STA Disconnected, Try to connect AP again..");
        turn_led1(OFF);
        esp_wifi_connect();
    }
}

esp_err_t prov_data_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen, uint8_t **outbuf, ssize_t *outlen, void *context) {
    if (inbuf) {
        ESP_LOGI(TAG, "Received data: %.*s", inlen, (char *)inbuf);
    }

    char response[] = "SUCCESS";
    *outbuf = (uint8_t*)strdup(response);
    if (*outbuf == nullptr) {
        ESP_LOGE(TAG, "System out of memory");
        return ESP_ERR_NO_MEM;
    }
    *outlen = strlen(response) + 1;

    return ESP_OK;
}

static bool start_provisioning() {
    ESP_LOGI(TAG, "Start Wi-Fi Provisioning");

    // create AP name string
    char service_name[32] = {};
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    snprintf(service_name, 32, "%s%02X%02X%02X", BLE_PROV_AP_PREFIX, mac[3], mac[4], mac[5]);

    uint8_t custom_service_uuid[] = {
        0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf,
        0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02,
    };
    
    if (wifi_prov_scheme_ble_set_service_uuid(custom_service_uuid) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set BLE service uuid");
        return false;
    }
    if (wifi_prov_mgr_endpoint_create("yogyui") != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create custom endpoint");
        return false;
    }

    wifi_prov_security_t security = WIFI_PROV_SECURITY_1;
    const char *service_key = nullptr;  // password
    if (wifi_prov_mgr_start_provisioning(security, BLE_PROV_POP, service_name, service_key) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start provisioning");
        return false;
    }
    if (wifi_prov_mgr_endpoint_register("yogyui", prov_data_handler, nullptr) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register endpoint handler");
        return false;
    }

    return true;
}

bool initialize_wifi_provisioning() {
    turn_led1(OFF);

    // initialize TCP/IP network interface
    if (esp_netif_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize network interface");
        return false;
    }

    // initialize event loop (core 0 loop)
    if (esp_event_loop_create_default() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create event loop");
        return false;
    }
    wifi_event_group = xEventGroupCreate();

    // register event handler
    if (esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &wifi_prov_event_handler, nullptr) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register wifi provisioning event");
        return false;
    }
    if (esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_prov_event_handler, nullptr) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register wifi event");
        return false;
    }
    if (esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_prov_event_handler, nullptr) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register ip event");
        return false;
    }

    // initialize WiFi 
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&init_config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize wifi");
        return false;
    }

    // config provisioning manager (ble)
    wifi_prov_mgr_config_t prov_config = {};
    prov_config.scheme = wifi_prov_scheme_ble;
    prov_config.scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM;
    if (wifi_prov_mgr_init(prov_config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize wifi provisioning manager");
        return false;
    }

    // check provision status
    bool is_provisioned = false;
    if (wifi_prov_mgr_is_provisioned(&is_provisioned) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to check wifi provision status");
        return false;
    }

    if (!is_provisioned) {
        if (!start_provisioning())
            return false;
    } else {
        ESP_LOGI(TAG, "Already provisioned, starting Wi-Fi STA");
        wifi_prov_mgr_deinit();
        if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set wifi mode as STA");
            return false;
        }
        if (esp_wifi_start() != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start wifi");
            return false;
        }
    }

    ESP_LOGI(TAG, "Wait for Wi-Fi Connection");
    xEventGroupWaitBits(wifi_event_group, BIT0, false, true, portMAX_DELAY);

    return true;
}