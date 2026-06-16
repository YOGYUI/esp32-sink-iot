#include "module_wifi_provisioning.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "nvs.h"
#include "cJSON.h"
#include "defines.h"
#include "module_gpio.h"
#include "module_mqtt.h"
#include "module_sntp.h"
#include "module_webserver.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "WIFI";

static bool g_sta_connected  = false;
static bool g_auto_reconnect = false;
static char g_sta_ip[16]     = {};
static char g_sta_ssid[33]   = {};

#define WIFI_CFG_FILE "/spiffs/config.json"

/* ── SPIFFS config.json helpers ──────────────────────────────────────────── */

static bool spiffs_load_wifi(char *ssid_out, char *pass_out) {
    FILE *f = fopen(WIFI_CFG_FILE, "r");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(f); return false; }
    fread(buf, 1, sz, f);
    buf[sz] = '\0';
    fclose(f);

    cJSON *obj = cJSON_Parse(buf);
    free(buf);
    if (!obj) return false;

    bool found = false;
    const cJSON *js = cJSON_GetObjectItemCaseSensitive(obj, "wifi_ssid");
    const cJSON *jp = cJSON_GetObjectItemCaseSensitive(obj, "wifi_pass");
    if (cJSON_IsString(js) && js->valuestring[0]) {
        strncpy(ssid_out, js->valuestring, 32);
        ssid_out[32] = '\0';
        strncpy(pass_out,
                (cJSON_IsString(jp) && jp->valuestring) ? jp->valuestring : "",
                64);
        pass_out[64] = '\0';
        found = true;
    }
    cJSON_Delete(obj);
    return found;
}

static void spiffs_save_wifi(const char *ssid, const char *pass) {
    cJSON *obj = cJSON_CreateObject();
    if (!obj) return;
    cJSON_AddStringToObject(obj, "wifi_ssid", ssid ? ssid : "");
    cJSON_AddStringToObject(obj, "wifi_pass", pass  ? pass  : "");
    char *json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!json) return;

    FILE *f = fopen(WIFI_CFG_FILE, "w");
    if (f) {
        fputs(json, f);
        fclose(f);
        ESP_LOGI(TAG, "WiFi config saved to SPIFFS");
    } else {
        ESP_LOGW(TAG, "Failed to open %s for write", WIFI_CFG_FILE);
    }
    cJSON_free(json);
}

/* ── WiFi state callbacks ─────────────────────────────────────────────────── */

static void on_sta_connected() {
    turn_led1(ON);
    initialize_sntp();
    start_mqtt();
    webserver_push_wifi_update();
}

static void on_sta_disconnected() {
    turn_led1(OFF);
    stop_mqtt();
    webserver_push_wifi_update();
}

/* ── Event handler ───────────────────────────────────────────────────────── */

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        wifi_event_sta_connected_t *ev = (wifi_event_sta_connected_t *)data;
        memcpy(g_sta_ssid, ev->ssid, ev->ssid_len);
        g_sta_ssid[ev->ssid_len] = '\0';
        ESP_LOGI(TAG, "STA connected to: %s", g_sta_ssid);

    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        snprintf(g_sta_ip, sizeof(g_sta_ip), IPSTR, IP2STR(&ev->ip_info.ip));
        g_sta_connected  = true;
        g_auto_reconnect = true;
        ESP_LOGI(TAG, "STA got IP: %s", g_sta_ip);
        on_sta_connected();

    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *ev = (wifi_event_sta_disconnected_t *)data;
        ESP_LOGW(TAG, "STA disconnected (reason: %d)", ev->reason);
        bool was_connected = g_sta_connected;
        g_sta_connected = false;
        memset(g_sta_ip, 0, sizeof(g_sta_ip));
        if (was_connected) {
            on_sta_disconnected();
        }
        if (g_auto_reconnect) {
            esp_wifi_connect();
        }
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */

bool wifi_get_sta_status(char *ssid_out, char *ip_out) {
    if (ssid_out) strncpy(ssid_out, g_sta_ssid, 33);
    if (ip_out)   strncpy(ip_out,   g_sta_ip,   16);
    return g_sta_connected;
}

void wifi_do_connect(const char *ssid, const char *password) {
    ESP_LOGI(TAG, "Connecting to SSID: %s", ssid);

    /* Persist credentials to SPIFFS config.json and NVS */
    spiffs_save_wifi(ssid, password);
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE_CFG, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_str(nvs, NVS_KEY_WIFI_SSID, ssid);
        nvs_set_str(nvs, NVS_KEY_WIFI_PASS, password ? password : "");
        nvs_commit(nvs);
        nvs_close(nvs);
    }

    wifi_config_t sta_cfg = {};
    strncpy((char *)sta_cfg.sta.ssid,     ssid,     sizeof(sta_cfg.sta.ssid)     - 1);
    strncpy((char *)sta_cfg.sta.password, password, sizeof(sta_cfg.sta.password) - 1);
    sta_cfg.sta.threshold.authmode = (password && password[0])
                                     ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    sta_cfg.sta.pmf_cfg.capable  = true;
    sta_cfg.sta.pmf_cfg.required = false;

    g_auto_reconnect = true;
    esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    esp_wifi_disconnect();
    esp_wifi_connect();
}

void wifi_do_forget() {
    ESP_LOGI(TAG, "Forgetting WiFi credentials");
    g_auto_reconnect = false;

    /* Clear SPIFFS config.json */
    spiffs_save_wifi("", "");

    /* Clear NVS */
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE_CFG, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_erase_key(nvs, NVS_KEY_WIFI_SSID);
        nvs_erase_key(nvs, NVS_KEY_WIFI_PASS);
        nvs_commit(nvs);
        nvs_close(nvs);
    }

    esp_wifi_disconnect();
    g_sta_connected = false;
    memset(g_sta_ip,   0, sizeof(g_sta_ip));
    memset(g_sta_ssid, 0, sizeof(g_sta_ssid));
    webserver_push_wifi_update();
}

/* ── Init ───────────────────────────────────────────────────────────────── */

bool initialize_wifi_provisioning() {
    turn_led1(OFF);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &event_handler, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &event_handler, nullptr));

    /* Build SoftAP SSID from last 3 MAC bytes */
    char ap_ssid[32] = {};
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    snprintf(ap_ssid, sizeof(ap_ssid), "%s%02X%02X%02X",
             PROV_AP_SSID_PREFIX, mac[3], mac[4], mac[5]);

    wifi_config_t ap_cfg = {};
    strncpy((char *)ap_cfg.ap.ssid, ap_ssid, sizeof(ap_cfg.ap.ssid) - 1);
    ap_cfg.ap.ssid_len       = (uint8_t)strlen(ap_ssid);
    strncpy((char *)ap_cfg.ap.password, PROV_SOFTAP_PASSWD,
            sizeof(ap_cfg.ap.password) - 1);
    ap_cfg.ap.authmode       = WIFI_AUTH_WPA2_PSK;
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.channel        = 1;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "SoftAP: SSID=%s PW=%s  (192.168.4.1)", ap_ssid, PROV_SOFTAP_PASSWD);

    /* Web server starts immediately — mounts SPIFFS, accessible via SoftAP */
    initialize_webserver();

    /* Load WiFi credentials: SPIFFS config.json first, then NVS fallback */
    char saved_ssid[33] = {};
    char saved_pass[65] = {};

    if (spiffs_load_wifi(saved_ssid, saved_pass)) {
        ESP_LOGI(TAG, "WiFi credentials loaded from SPIFFS config.json (%s)", saved_ssid);
    } else {
        /* NVS fallback — migrate to SPIFFS if found */
        nvs_handle_t nvs;
        if (nvs_open(NVS_NAMESPACE_CFG, NVS_READONLY, &nvs) == ESP_OK) {
            size_t len;
            len = sizeof(saved_ssid);
            nvs_get_str(nvs, NVS_KEY_WIFI_SSID, saved_ssid, &len);
            len = sizeof(saved_pass);
            nvs_get_str(nvs, NVS_KEY_WIFI_PASS, saved_pass, &len);
            nvs_close(nvs);
        }
        if (saved_ssid[0]) {
            ESP_LOGI(TAG, "WiFi credentials migrated from NVS → SPIFFS (%s)", saved_ssid);
            spiffs_save_wifi(saved_ssid, saved_pass);
        }
    }

    if (saved_ssid[0]) {
        ESP_LOGI(TAG, "Saved WiFi found (%s), connecting...", saved_ssid);
        /* Connect without re-saving (credentials already persisted above) */
        wifi_config_t sta_cfg = {};
        strncpy((char *)sta_cfg.sta.ssid,     saved_ssid, sizeof(sta_cfg.sta.ssid)     - 1);
        strncpy((char *)sta_cfg.sta.password, saved_pass, sizeof(sta_cfg.sta.password) - 1);
        sta_cfg.sta.threshold.authmode = saved_pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
        sta_cfg.sta.pmf_cfg.capable  = true;
        sta_cfg.sta.pmf_cfg.required = false;
        g_auto_reconnect = true;
        esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
        esp_wifi_connect();
    } else {
        ESP_LOGI(TAG, "No saved WiFi. Open browser → http://192.168.4.1 to configure.");
    }

    return true;
}
