#include "module_wifi_provisioning.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_softap.h"
#include "module_gpio.h"
#include "module_mqtt.h"
#include "module_sntp.h"
#include "module_webserver.h"
#include "esp_log.h"
#include "defines.h"

static const char *TAG = "WIFI_PROV";
static EventGroupHandle_t wifi_event_group;

/* ── Post-connection / post-disconnection hooks ──────────────────────────── */

static void on_wifi_connected() {
    turn_led1(ON);
    initialize_sntp();
    start_mqtt();
    initialize_webserver();
}

static void on_wifi_disconnected() {
    turn_led1(OFF);
    stop_webserver();
    stop_mqtt();
    esp_wifi_connect();
}

/* ── Unified event handler ───────────────────────────────────────────────── */

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_PROV_EVENT) {
        switch (id) {
        case WIFI_PROV_INIT:
            ESP_LOGI(TAG, "Provisioning initialised");
            break;
        case WIFI_PROV_START:
            ESP_LOGI(TAG, "Provisioning started — connect to SoftAP and run esp_prov");
            break;
        case WIFI_PROV_CRED_RECV: {
            wifi_sta_config_t *cfg = reinterpret_cast<wifi_sta_config_t *>(data);
            ESP_LOGI(TAG, "Credentials received (SSID: %s)", (const char *)cfg->ssid);
            break;
        }
        case WIFI_PROV_CRED_FAIL: {
            wifi_prov_sta_fail_reason_t *r = reinterpret_cast<wifi_prov_sta_fail_reason_t *>(data);
            ESP_LOGE(TAG, "Provisioning failed: %s",
                     *r == WIFI_PROV_STA_AUTH_ERROR ? "auth error" : "AP not found");
            break;
        }
        case WIFI_PROV_CRED_SUCCESS:
            ESP_LOGI(TAG, "Provisioning succeeded");
            break;
        case WIFI_PROV_END:
            wifi_prov_mgr_deinit();
            ESP_LOGI(TAG, "Provisioning manager deinitialized");
            break;
        default:
            break;
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi STA started");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = reinterpret_cast<ip_event_got_ip_t *>(data);
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        on_wifi_connected();
        xEventGroupSetBits(wifi_event_group, BIT0);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Wi-Fi STA disconnected, retrying...");
        on_wifi_disconnected();
    }
}

/* ── Start SoftAP provisioning ───────────────────────────────────────────── */

static bool start_provisioning() {
    /* SSID = "<prefix><last 3 MAC bytes>"  e.g. "SINK_AABBCC" */
    char ssid[32] = {};
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    snprintf(ssid, sizeof(ssid), "%s%02X%02X%02X",
             PROV_AP_SSID_PREFIX, mac[3], mac[4], mac[5]);

    if (wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1,
                                         PROV_POP,
                                         ssid,
                                         PROV_SOFTAP_PASSWD) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start provisioning");
        return false;
    }

    ESP_LOGI(TAG, "SoftAP SSID : %s", ssid);
    ESP_LOGI(TAG, "POP         : %s", PROV_POP);
    ESP_LOGI(TAG, "Password    : %s", PROV_SOFTAP_PASSWD);
    return true;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

bool initialize_wifi_provisioning() {
    turn_led1(OFF);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID,  &event_handler, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,      ESP_EVENT_ANY_ID,  &event_handler, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,   IP_EVENT_STA_GOT_IP,    &event_handler, nullptr));

    /* STA netif for normal operation; AP netif for SoftAP provisioning */
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));

    wifi_prov_mgr_config_t prov_cfg = {};
    prov_cfg.scheme               = wifi_prov_scheme_softap;
    prov_cfg.scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE;
    ESP_ERROR_CHECK(wifi_prov_mgr_init(prov_cfg));

    bool is_provisioned = false;
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&is_provisioned));

    if (!is_provisioned) {
        ESP_LOGI(TAG, "Device not provisioned — starting SoftAP");
        if (!start_provisioning())
            return false;
    } else {
        ESP_LOGI(TAG, "Already provisioned — connecting to saved AP");
        wifi_prov_mgr_deinit();
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());
    }

    ESP_LOGI(TAG, "Waiting for Wi-Fi connection...");
    xEventGroupWaitBits(wifi_event_group, BIT0, false, true, portMAX_DELAY);
    return true;
}
