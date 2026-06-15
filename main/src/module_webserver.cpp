#include "module_webserver.h"
#include "esp_http_server.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "cJSON.h"
#include "nvs.h"
#include "defines.h"
#include "module_gpio.h"
#include "module_mqtt.h"
#include "module_wifi_provisioning.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "WEBSERVER";
static httpd_handle_t g_server = nullptr;
static bool g_spiffs_mounted = false;

#define MAX_WS_CLIENTS   4
#define SPIFFS_BASE_PATH "/spiffs"
#define FILE_CHUNK_SIZE  1024

static int g_ws_fds[MAX_WS_CLIENTS];
static int g_ws_count = 0;
static SemaphoreHandle_t g_ws_mutex = nullptr;
static void restart_task(void *);

/* ── SPIFFS mount / unmount ──────────────────────────────────────────────── */

static bool mount_spiffs() {
    esp_vfs_spiffs_conf_t conf = {};
    conf.base_path              = SPIFFS_BASE_PATH;
    conf.partition_label        = "spiffs";
    conf.max_files              = 8;
    conf.format_if_mount_failed = false;

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed (%s). Run 'idf.py flash' to upload web files.", esp_err_to_name(ret));
        return false;
    }

    size_t total = 0, used = 0;
    esp_spiffs_info("spiffs", &total, &used);
    ESP_LOGI(TAG, "SPIFFS mounted: %u / %u bytes used", (unsigned)used, (unsigned)total);
    g_spiffs_mounted = true;
    return true;
}

static void unmount_spiffs() {
    if (g_spiffs_mounted) {
        esp_vfs_spiffs_unregister("spiffs");
        g_spiffs_mounted = false;
        ESP_LOGI(TAG, "SPIFFS unmounted");
    }
}

/* ── Static file server ──────────────────────────────────────────────────── */

static esp_err_t serve_file(httpd_req_t *req, const char *path, const char *content_type) {
    FILE *f = fopen(path, "r");
    if (!f) {
        ESP_LOGE(TAG, "File not found: %s", path);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, content_type);
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

    char *buf = (char*)malloc(FILE_CHUNK_SIZE);
    if (!buf) { fclose(f); return ESP_ERR_NO_MEM; }

    esp_err_t ret = ESP_OK;
    size_t n;
    while ((n = fread(buf, 1, FILE_CHUNK_SIZE, f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, (ssize_t)n) != ESP_OK) {
            ret = ESP_FAIL;
            break;
        }
    }
    free(buf);
    fclose(f);
    httpd_resp_send_chunk(req, nullptr, 0);
    return ret;
}



/* ── helpers ─────────────────────────────────────────────────────────────── */

static esp_err_t send_json(httpd_req_t *req, const char *json) {
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, (ssize_t)strlen(json));
}

static char *read_body(httpd_req_t *req) {
    int len = req->content_len;
    if (len <= 0 || len > 4096) return nullptr;
    char *buf = (char*)malloc(len + 1);
    if (!buf) return nullptr;
    int received = httpd_req_recv(req, buf, len);
    if (received <= 0) { free(buf); return nullptr; }
    buf[received] = '\0';
    return buf;
}

/* ── WebSocket client list ───────────────────────────────────────────────── */

static void ws_add(int fd) {
    xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
    if (g_ws_count < MAX_WS_CLIENTS)
        g_ws_fds[g_ws_count++] = fd;
    xSemaphoreGive(g_ws_mutex);
    ESP_LOGI(TAG, "WS client added fd=%d count=%d", fd, g_ws_count);
}

static void ws_remove(int fd) {
    xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
    for (int i = 0; i < g_ws_count; i++) {
        if (g_ws_fds[i] == fd) {
            g_ws_fds[i] = g_ws_fds[--g_ws_count];
            break;
        }
    }
    xSemaphoreGive(g_ws_mutex);
    ESP_LOGI(TAG, "WS client removed fd=%d count=%d", fd, g_ws_count);
}

/* ── WebSocket async broadcast (runs inside httpd task via queue_work) ───── */

static void ws_broadcast_work(void *arg) {
    char *payload = (char*)arg;
    if (!payload || !g_server) { free(payload); return; }

    httpd_ws_frame_t frame = {};
    frame.final = true;
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = (uint8_t*)payload;
    frame.len = strlen(payload);

    xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
    for (int i = 0; i < g_ws_count; ) {
        if (httpd_ws_send_frame_async(g_server, g_ws_fds[i], &frame) != ESP_OK) {
            ESP_LOGW(TAG, "WS send failed fd=%d, removing", g_ws_fds[i]);
            g_ws_fds[i] = g_ws_fds[--g_ws_count];
        } else {
            i++;
        }
    }
    xSemaphoreGive(g_ws_mutex);
    free(payload);
}

/* ── JSON status builder (shared by REST + WebSocket) ───────────────────── */

static cJSON *make_status_json() {
    cJSON *obj = cJSON_CreateObject();
    if (!obj) return nullptr;
    cJSON_AddBoolToObject(obj,   "flow_active",   flow_sensor->is_active);
    cJSON_AddNumberToObject(obj, "pulse_per_sec", flow_sensor->pulse_count_per_sec);
    cJSON_AddNumberToObject(obj, "pulse_accum",   (double)flow_sensor->pulse_count_accum);
    cJSON_AddNumberToObject(obj, "flow_rate",
        (double)flow_sensor->pulse_count_per_sec / misc_cfg->flow_pulse_per_liter);
    cJSON_AddNumberToObject(obj, "volume",
        (double)flow_sensor->pulse_count_accum / misc_cfg->flow_pulse_per_liter);
    char ssid[33] = {}, ip[16] = {};
    bool wifi_conn = wifi_get_sta_status(ssid, ip);
    cJSON_AddBoolToObject(obj, "wifi_connected",  wifi_conn);
    cJSON_AddStringToObject(obj, "wifi_ssid",     ssid);
    cJSON_AddStringToObject(obj, "wifi_ip",       ip);
    cJSON_AddBoolToObject(obj, "mqtt_connected",  mqtt_is_connected());
    return obj;
}

/* ── HTTP handlers ───────────────────────────────────────────────────────── */

static esp_err_t h_root(httpd_req_t *req) {
    return serve_file(req, SPIFFS_BASE_PATH "/index.html", "text/html; charset=utf-8");
}

static esp_err_t h_css(httpd_req_t *req) {
    return serve_file(req, SPIFFS_BASE_PATH "/app.css", "text/css");
}

static esp_err_t h_js(httpd_req_t *req) {
    return serve_file(req, SPIFFS_BASE_PATH "/app.js", "application/javascript");
}

static esp_err_t h_favicon(httpd_req_t *req) {
    return serve_file(req, SPIFFS_BASE_PATH "/favicon.svg", "image/svg+xml");
}

static esp_err_t h_status(httpd_req_t *req) {
    cJSON *obj = make_status_json();
    if (!obj) return ESP_FAIL;
    char *json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!json) return ESP_FAIL;
    esp_err_t ret = send_json(req, json);
    cJSON_free(json);
    return ret;
}

static esp_err_t h_relay_toggle(httpd_req_t *req) {
    bool ok = blink_relay();
    char buf[24];
    snprintf(buf, sizeof(buf), "{\"ok\":%s}", ok ? "true" : "false");
    return send_json(req, buf);
}

static esp_err_t h_config_get(httpd_req_t *req) {
    char uri[128];
    char user[64];
    char pass[64];
    char client_id[64];
    uint16_t port = DEFAULT_MQTT_BROKER_PORT;

    strncpy(uri,       DEFAULT_MQTT_BROKER_URI,      sizeof(uri)       - 1);
    strncpy(user,      DEFAULT_MQTT_BROKER_USERNAME, sizeof(user)      - 1);
    strncpy(pass,      DEFAULT_MQTT_BROKER_PASSWORD, sizeof(pass)      - 1);
    strncpy(client_id, DEFAULT_MQTT_BROKER_CLIENT_ID, sizeof(client_id) - 1);

    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE_CFG, NVS_READONLY, &nvs) == ESP_OK) {
        size_t len;
        len = sizeof(uri);       nvs_get_str(nvs, NVS_KEY_MQTT_URI,       uri,       &len);
        len = sizeof(user);      nvs_get_str(nvs, NVS_KEY_MQTT_USER,      user,      &len);
        len = sizeof(pass);      nvs_get_str(nvs, NVS_KEY_MQTT_PASS,      pass,      &len);
        len = sizeof(client_id); nvs_get_str(nvs, NVS_KEY_MQTT_CLIENT_ID, client_id, &len);
        nvs_get_u16(nvs, NVS_KEY_MQTT_PORT, &port);
        nvs_close(nvs);
    }

    cJSON *obj = cJSON_CreateObject();
    if (!obj) return ESP_FAIL;
    cJSON_AddStringToObject(obj, "mqtt_uri",          uri);
    cJSON_AddNumberToObject(obj, "mqtt_port",         port);
    cJSON_AddStringToObject(obj, "mqtt_user",         user);
    cJSON_AddStringToObject(obj, "mqtt_pass",         pass);
    cJSON_AddStringToObject(obj, "mqtt_client_id",    client_id);
    cJSON_AddBoolToObject(obj,   "auto_off_enabled",  flow_sensor->enable_auto_off);
    cJSON_AddNumberToObject(obj, "auto_off_time",     (double)flow_sensor->auto_off_time);

    char *json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!json) return ESP_FAIL;
    esp_err_t ret = send_json(req, json);
    cJSON_free(json);
    return ret;
}

static esp_err_t h_config_autooff(httpd_req_t *req) {
    char *body = read_body(req);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad request");
        return ESP_FAIL;
    }

    cJSON *obj = cJSON_Parse(body);
    free(body);
    if (!obj) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    const cJSON *en = cJSON_GetObjectItemCaseSensitive(obj, "auto_off_enabled");
    const cJSON *t  = cJSON_GetObjectItemCaseSensitive(obj, "auto_off_time");

    if (cJSON_IsBool(en))    flow_sensor->enable_auto_off = cJSON_IsTrue(en);
    if (cJSON_IsNumber(t))   flow_sensor->auto_off_time   = (int64_t)t->valuedouble;

    cJSON_Delete(obj);

    /* Persist */
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE_CFG, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u8(nvs,  NVS_KEY_AUTO_OFF_EN,   (uint8_t)flow_sensor->enable_auto_off);
        nvs_set_i64(nvs, NVS_KEY_AUTO_OFF_TIME,  flow_sensor->auto_off_time);
        nvs_commit(nvs);
        nvs_close(nvs);
    }

    ESP_LOGI(TAG, "auto_off: en=%d time=%lld", flow_sensor->enable_auto_off, flow_sensor->auto_off_time);
    return send_json(req, "{\"ok\":true}");
}

static esp_err_t h_config_gpio_get(httpd_req_t *req) {
    nvs_handle_t nvs;
    uint8_t pin_relay = gpio_cfg->pin_relay;
    uint8_t pin_led1  = gpio_cfg->pin_led1;
    uint8_t pin_led2  = gpio_cfg->pin_led2;
    uint8_t pin_flow  = gpio_cfg->pin_flow;
    uint8_t pin_pwm   = gpio_cfg->pin_pwm;

    if (nvs_open(NVS_NAMESPACE_CFG, NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_u8(nvs, NVS_KEY_GPIO_RELAY, &pin_relay);
        nvs_get_u8(nvs, NVS_KEY_GPIO_LED1,  &pin_led1);
        nvs_get_u8(nvs, NVS_KEY_GPIO_LED2,  &pin_led2);
        nvs_get_u8(nvs, NVS_KEY_GPIO_FLOW,  &pin_flow);
        nvs_get_u8(nvs, NVS_KEY_GPIO_PWM,   &pin_pwm);
        nvs_close(nvs);
    }

    cJSON *obj = cJSON_CreateObject();
    if (!obj) return ESP_FAIL;
    cJSON_AddNumberToObject(obj, "pin_relay", pin_relay);
    cJSON_AddNumberToObject(obj, "pin_led1",  pin_led1);
    cJSON_AddNumberToObject(obj, "pin_led2",  pin_led2);
    cJSON_AddNumberToObject(obj, "pin_flow",  pin_flow);
    cJSON_AddNumberToObject(obj, "pin_pwm",   pin_pwm);

    char *json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!json) return ESP_FAIL;
    esp_err_t ret = send_json(req, json);
    cJSON_free(json);
    return ret;
}

static esp_err_t h_config_gpio_post(httpd_req_t *req) {
    char *body = read_body(req);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad request");
        return ESP_FAIL;
    }

    cJSON *obj = cJSON_Parse(body);
    free(body);
    if (!obj) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    bool ok = false;
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE_CFG, NVS_READWRITE, &nvs) == ESP_OK) {
        const cJSON *relay = cJSON_GetObjectItemCaseSensitive(obj, "pin_relay");
        const cJSON *led1  = cJSON_GetObjectItemCaseSensitive(obj, "pin_led1");
        const cJSON *led2  = cJSON_GetObjectItemCaseSensitive(obj, "pin_led2");
        const cJSON *flow  = cJSON_GetObjectItemCaseSensitive(obj, "pin_flow");
        const cJSON *pwm   = cJSON_GetObjectItemCaseSensitive(obj, "pin_pwm");

        if (cJSON_IsNumber(relay)) nvs_set_u8(nvs, NVS_KEY_GPIO_RELAY, (uint8_t)relay->valuedouble);
        if (cJSON_IsNumber(led1))  nvs_set_u8(nvs, NVS_KEY_GPIO_LED1,  (uint8_t)led1->valuedouble);
        if (cJSON_IsNumber(led2))  nvs_set_u8(nvs, NVS_KEY_GPIO_LED2,  (uint8_t)led2->valuedouble);
        if (cJSON_IsNumber(flow))  nvs_set_u8(nvs, NVS_KEY_GPIO_FLOW,  (uint8_t)flow->valuedouble);
        if (cJSON_IsNumber(pwm))   nvs_set_u8(nvs, NVS_KEY_GPIO_PWM,   (uint8_t)pwm->valuedouble);

        nvs_commit(nvs);
        nvs_close(nvs);
        ok = true;
    }

    cJSON_Delete(obj);

    char buf[24];
    snprintf(buf, sizeof(buf), "{\"ok\":%s}", ok ? "true" : "false");
    send_json(req, buf);

    if (ok)
        xTaskCreate(restart_task, "restart", 2048, nullptr, 5, nullptr);

    return ESP_OK;
}

static void restart_task(void *) {
    char *payload = strdup("{\"type\":\"restart\"}");
    if (payload && g_server) {
        httpd_queue_work(g_server, ws_broadcast_work, payload);
    }
    vTaskDelay(1500 / portTICK_PERIOD_MS);
    esp_restart();
}

static esp_err_t h_config_mqtt(httpd_req_t *req) {
    char *body = read_body(req);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad request");
        return ESP_FAIL;
    }

    cJSON *obj = cJSON_Parse(body);
    free(body);
    if (!obj) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    bool ok = false;
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE_CFG, NVS_READWRITE, &nvs) == ESP_OK) {
        const cJSON *uri       = cJSON_GetObjectItemCaseSensitive(obj, "mqtt_uri");
        const cJSON *port      = cJSON_GetObjectItemCaseSensitive(obj, "mqtt_port");
        const cJSON *user      = cJSON_GetObjectItemCaseSensitive(obj, "mqtt_user");
        const cJSON *pass      = cJSON_GetObjectItemCaseSensitive(obj, "mqtt_pass");
        const cJSON *client_id = cJSON_GetObjectItemCaseSensitive(obj, "mqtt_client_id");

        if (cJSON_IsString(uri))  nvs_set_str(nvs, NVS_KEY_MQTT_URI,       uri->valuestring);
        if (cJSON_IsNumber(port)) nvs_set_u16(nvs, NVS_KEY_MQTT_PORT,      (uint16_t)port->valuedouble);
        if (cJSON_IsString(user)) nvs_set_str(nvs, NVS_KEY_MQTT_USER,      user->valuestring);
        if (cJSON_IsString(pass)) nvs_set_str(nvs, NVS_KEY_MQTT_PASS,      pass->valuestring);
        if (cJSON_IsString(client_id) && client_id->valuestring[0])
            nvs_set_str(nvs, NVS_KEY_MQTT_CLIENT_ID, client_id->valuestring);

        nvs_commit(nvs);
        nvs_close(nvs);
        ok = true;
    }

    cJSON_Delete(obj);

    char buf[24];
    snprintf(buf, sizeof(buf), "{\"ok\":%s}", ok ? "true" : "false");
    send_json(req, buf);

    if (ok)
        xTaskCreate(restart_task, "restart", 2048, nullptr, 5, nullptr);

    return ESP_OK;
}

static esp_err_t h_config_misc_get(httpd_req_t *req) {
    cJSON *obj = cJSON_CreateObject();
    if (!obj) return ESP_FAIL;
    cJSON_AddNumberToObject(obj, "relay_toggle_ms",    misc_cfg->relay_toggle_ms);
    cJSON_AddNumberToObject(obj, "flow_pulse_per_liter", misc_cfg->flow_pulse_per_liter);
    cJSON_AddNumberToObject(obj, "display_slave_addr", misc_cfg->display_slave_addr);

    char *json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!json) return ESP_FAIL;
    esp_err_t ret = send_json(req, json);
    cJSON_free(json);
    return ret;
}

static esp_err_t h_config_misc_post(httpd_req_t *req) {
    char *body = read_body(req);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad request");
        return ESP_FAIL;
    }

    cJSON *obj = cJSON_Parse(body);
    free(body);
    if (!obj) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    nvs_handle_t nvs;
    bool ok = nvs_open(NVS_NAMESPACE_CFG, NVS_READWRITE, &nvs) == ESP_OK;

    const cJSON *relay_ms  = cJSON_GetObjectItemCaseSensitive(obj, "relay_toggle_ms");
    const cJSON *flow_ppl  = cJSON_GetObjectItemCaseSensitive(obj, "flow_pulse_per_liter");
    const cJSON *disp_addr = cJSON_GetObjectItemCaseSensitive(obj, "display_slave_addr");

    if (cJSON_IsNumber(relay_ms) && relay_ms->valuedouble >= 1) {
        misc_cfg->relay_toggle_ms = (uint32_t)relay_ms->valuedouble;
        if (ok) nvs_set_u32(nvs, NVS_KEY_MISC_RELAY_MS, misc_cfg->relay_toggle_ms);
    }
    if (cJSON_IsNumber(flow_ppl) && flow_ppl->valuedouble >= 1) {
        misc_cfg->flow_pulse_per_liter = (uint32_t)flow_ppl->valuedouble;
        if (ok) nvs_set_u32(nvs, NVS_KEY_MISC_FLOW_PPL, misc_cfg->flow_pulse_per_liter);
    }
    if (cJSON_IsNumber(disp_addr)) {
        misc_cfg->display_slave_addr = (uint8_t)disp_addr->valuedouble;
        if (ok) nvs_set_u8(nvs, NVS_KEY_MISC_DISP_ADDR, misc_cfg->display_slave_addr);
    }

    if (ok) { nvs_commit(nvs); nvs_close(nvs); }
    cJSON_Delete(obj);

    ESP_LOGI(TAG, "misc: relay_ms=%lu ppl=%lu disp=0x%02x",
             misc_cfg->relay_toggle_ms, misc_cfg->flow_pulse_per_liter, misc_cfg->display_slave_addr);
    return send_json(req, "{\"ok\":true}");
}

static esp_err_t h_config_topic_get(httpd_req_t *req) {
    char pub[128], sub[128], ota[128];
    strncpy(pub, DEFAULT_MQTT_PUBLISH_TOPIC_DEVICE, sizeof(pub) - 1);
    strncpy(sub, DEFAULT_MQTT_SUBSCRIBE_TOPIC_DEVICE, sizeof(sub) - 1);
    strncpy(ota, DEFAULT_MQTT_SUBSCRIBE_TOPIC_OTA, sizeof(ota) - 1);

    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE_CFG, NVS_READONLY, &nvs) == ESP_OK) {
        size_t len;
        len = sizeof(pub); nvs_get_str(nvs, NVS_KEY_MQTT_TOPIC_PUB, pub, &len);
        len = sizeof(sub); nvs_get_str(nvs, NVS_KEY_MQTT_TOPIC_SUB, sub, &len);
        len = sizeof(ota); nvs_get_str(nvs, NVS_KEY_MQTT_TOPIC_OTA, ota, &len);
        nvs_close(nvs);
    }

    cJSON *obj = cJSON_CreateObject();
    if (!obj) return ESP_FAIL;
    cJSON_AddStringToObject(obj, "topic_pub", pub);
    cJSON_AddStringToObject(obj, "topic_sub", sub);
    cJSON_AddStringToObject(obj, "topic_ota", ota);

    char *json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!json) return ESP_FAIL;
    esp_err_t ret = send_json(req, json);
    cJSON_free(json);
    return ret;
}

static esp_err_t h_config_topic_post(httpd_req_t *req) {
    char *body = read_body(req);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad request");
        return ESP_FAIL;
    }

    cJSON *obj = cJSON_Parse(body);
    free(body);
    if (!obj) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    bool ok = false;
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE_CFG, NVS_READWRITE, &nvs) == ESP_OK) {
        const cJSON *pub = cJSON_GetObjectItemCaseSensitive(obj, "topic_pub");
        const cJSON *sub = cJSON_GetObjectItemCaseSensitive(obj, "topic_sub");
        const cJSON *ota = cJSON_GetObjectItemCaseSensitive(obj, "topic_ota");

        if (cJSON_IsString(pub) && pub->valuestring[0])
            nvs_set_str(nvs, NVS_KEY_MQTT_TOPIC_PUB, pub->valuestring);
        if (cJSON_IsString(sub) && sub->valuestring[0])
            nvs_set_str(nvs, NVS_KEY_MQTT_TOPIC_SUB, sub->valuestring);
        if (cJSON_IsString(ota) && ota->valuestring[0])
            nvs_set_str(nvs, NVS_KEY_MQTT_TOPIC_OTA, ota->valuestring);

        nvs_commit(nvs);
        nvs_close(nvs);
        ok = true;
    }

    cJSON_Delete(obj);

    char buf[24];
    snprintf(buf, sizeof(buf), "{\"ok\":%s}", ok ? "true" : "false");
    send_json(req, buf);

    if (ok)
        xTaskCreate(restart_task, "restart", 2048, nullptr, 5, nullptr);

    return ESP_OK;
}

/* ── WiFi API handlers ───────────────────────────────────────────────────── */

static esp_err_t h_wifi_status(httpd_req_t *req) {
    char ssid[33] = {}, ip[16] = {};
    bool connected = wifi_get_sta_status(ssid, ip);

    cJSON *obj = cJSON_CreateObject();
    if (!obj) return ESP_FAIL;
    cJSON_AddBoolToObject(obj,   "connected", connected);
    cJSON_AddStringToObject(obj, "ssid",      ssid);
    cJSON_AddStringToObject(obj, "ip",        ip);

    char *json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!json) return ESP_FAIL;
    esp_err_t ret = send_json(req, json);
    cJSON_free(json);
    return ret;
}

static esp_err_t h_wifi_scan(httpd_req_t *req) {
    wifi_scan_config_t scan_cfg = {};
    scan_cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;

    esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi scan failed: %s", esp_err_to_name(err));
        return send_json(req, "{\"aps\":[],\"error\":\"scan failed\"}");
    }

    uint16_t count = 0;
    esp_wifi_scan_get_ap_num(&count);
    if (count == 0) {
        return send_json(req, "{\"aps\":[]}");
    }
    if (count > 20) count = 20;

    wifi_ap_record_t *records = (wifi_ap_record_t *)malloc(count * sizeof(wifi_ap_record_t));
    if (!records) return ESP_ERR_NO_MEM;

    esp_wifi_scan_get_ap_records(&count, records);

    cJSON *root = cJSON_CreateObject();
    cJSON *aps  = cJSON_CreateArray();

    for (int i = 0; i < count; i++) {
        if (records[i].ssid[0] == '\0') continue;
        cJSON *ap = cJSON_CreateObject();
        cJSON_AddStringToObject(ap, "ssid", (char *)records[i].ssid);
        cJSON_AddNumberToObject(ap, "rssi", records[i].rssi);
        cJSON_AddNumberToObject(ap, "auth", records[i].authmode);
        cJSON_AddItemToArray(aps, ap);
    }

    free(records);
    cJSON_AddItemToObject(root, "aps", aps);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return ESP_FAIL;
    esp_err_t ret = send_json(req, json);
    cJSON_free(json);
    return ret;
}

static esp_err_t h_wifi_connect(httpd_req_t *req) {
    char *body = read_body(req);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad request");
        return ESP_FAIL;
    }

    cJSON *obj = cJSON_Parse(body);
    free(body);
    if (!obj) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(obj, "ssid");
    const cJSON *pass = cJSON_GetObjectItemCaseSensitive(obj, "password");

    if (!cJSON_IsString(ssid) || !ssid->valuestring || !ssid->valuestring[0]) {
        cJSON_Delete(obj);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ssid required");
        return ESP_FAIL;
    }

    const char *password = (cJSON_IsString(pass) && pass->valuestring) ? pass->valuestring : "";

    /* wifi_do_connect() persists credentials to SPIFFS config.json and NVS */
    wifi_do_connect(ssid->valuestring, password);
    cJSON_Delete(obj);
    return send_json(req, "{\"ok\":true}");
}

static esp_err_t h_wifi_forget(httpd_req_t *req) {
    wifi_do_forget();
    return send_json(req, "{\"ok\":true}");
}

static esp_err_t h_ws(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        /* Initial WebSocket handshake */
        ws_add(httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    /* Read incoming frame (required even if we don't process it) */
    httpd_ws_frame_t frame = {};
    esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);
    if (ret != ESP_OK) return ret;

    if (frame.len > 0) {
        frame.payload = (uint8_t*)malloc(frame.len + 1);
        if (!frame.payload) return ESP_ERR_NO_MEM;
        ret = httpd_ws_recv_frame(req, &frame, frame.len);
        free(frame.payload);
        frame.payload = nullptr;
        if (ret != ESP_OK) return ret;
    }

    if (frame.type == HTTPD_WS_TYPE_CLOSE)
        ws_remove(httpd_req_to_sockfd(req));

    return ESP_OK;
}

/* ── URI registration helper ─────────────────────────────────────────────── */

static void reg(httpd_handle_t srv, const char *uri, httpd_method_t method,
                esp_err_t (*handler)(httpd_req_t *), bool is_ws = false) {
    httpd_uri_t u = {};
    u.uri          = uri;
    u.method       = method;
    u.handler      = handler;
    u.is_websocket = is_ws;
    httpd_register_uri_handler(srv, &u);
}

/* ── Public API ──────────────────────────────────────────────────────────── */

bool initialize_webserver() {
    if (g_server) {
        ESP_LOGI(TAG, "HTTP server already running");
        return true;
    }

    if (!g_ws_mutex) {
        g_ws_mutex = xSemaphoreCreateMutex();
        if (!g_ws_mutex) {
            ESP_LOGE(TAG, "Failed to create WS mutex");
            return false;
        }
    }

    if (!mount_spiffs()) {
        ESP_LOGW(TAG, "Starting without SPIFFS — static files unavailable");
    }

    /* Load persisted auto_off settings into live flow_sensor */
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE_CFG, NVS_READONLY, &nvs) == ESP_OK) {
        uint8_t en = 0;
        int64_t t  = 0;
        if (nvs_get_u8(nvs,  NVS_KEY_AUTO_OFF_EN,   &en) == ESP_OK) flow_sensor->enable_auto_off = (bool)en;
        if (nvs_get_i64(nvs, NVS_KEY_AUTO_OFF_TIME,  &t)  == ESP_OK) flow_sensor->auto_off_time   = t;
        nvs_close(nvs);
        ESP_LOGI(TAG, "auto_off loaded: en=%d time=%lld", flow_sensor->enable_auto_off, flow_sensor->auto_off_time);
    }

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size       = 8192;
    cfg.max_open_sockets = 7;
    cfg.max_uri_handlers = 24;

    if (httpd_start(&g_server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return false;
    }

    reg(g_server, "/",                   HTTP_GET,  h_root);
    reg(g_server, "/favicon.svg",        HTTP_GET,  h_favicon);
    reg(g_server, "/app.css",            HTTP_GET,  h_css);
    reg(g_server, "/app.js",             HTTP_GET,  h_js);
    reg(g_server, "/api/status",         HTTP_GET,  h_status);
    reg(g_server, "/api/relay/toggle",   HTTP_POST, h_relay_toggle);
    reg(g_server, "/api/config",         HTTP_GET,  h_config_get);
    reg(g_server, "/api/config/autooff", HTTP_POST, h_config_autooff);
    reg(g_server, "/api/config/mqtt",    HTTP_POST, h_config_mqtt);
    reg(g_server, "/api/config/gpio",    HTTP_GET,  h_config_gpio_get);
    reg(g_server, "/api/config/gpio",    HTTP_POST, h_config_gpio_post);
    reg(g_server, "/api/config/topic",   HTTP_GET,  h_config_topic_get);
    reg(g_server, "/api/config/topic",   HTTP_POST, h_config_topic_post);
    reg(g_server, "/api/config/misc",    HTTP_GET,  h_config_misc_get);
    reg(g_server, "/api/config/misc",    HTTP_POST, h_config_misc_post);
    reg(g_server, "/api/wifi/status",    HTTP_GET,  h_wifi_status);
    reg(g_server, "/api/wifi/scan",      HTTP_GET,  h_wifi_scan);
    reg(g_server, "/api/wifi/connect",   HTTP_POST, h_wifi_connect);
    reg(g_server, "/api/wifi/forget",    HTTP_POST, h_wifi_forget);
    reg(g_server, "/ws",                 HTTP_GET,  h_ws, true);

    ESP_LOGI(TAG, "HTTP server started on port %d", cfg.server_port);
    return true;
}

void stop_webserver() {
    if (!g_server) return;
    httpd_stop(g_server);
    g_server = nullptr;
    xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
    g_ws_count = 0;
    xSemaphoreGive(g_ws_mutex);
    unmount_spiffs();
    ESP_LOGI(TAG, "HTTP server stopped");
}

void webserver_push_wifi_update() {
    if (!g_server) return;

    xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
    bool has_clients = (g_ws_count > 0);
    xSemaphoreGive(g_ws_mutex);
    if (!has_clients) return;

    char ssid[33] = {}, ip[16] = {};
    bool connected = wifi_get_sta_status(ssid, ip);

    cJSON *obj = cJSON_CreateObject();
    if (!obj) return;
    cJSON_AddStringToObject(obj, "type",      "wifi");
    cJSON_AddBoolToObject(obj,   "connected", connected);
    cJSON_AddStringToObject(obj, "ssid",      ssid);
    cJSON_AddStringToObject(obj, "ip",        ip);

    char *payload = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!payload) return;

    if (httpd_queue_work(g_server, ws_broadcast_work, payload) != ESP_OK)
        free(payload);
}

void webserver_push_state_update() {
    if (!g_server) return;

    xSemaphoreTake(g_ws_mutex, portMAX_DELAY);
    bool has_clients = (g_ws_count > 0);
    xSemaphoreGive(g_ws_mutex);

    if (!has_clients) return;

    cJSON *obj = make_status_json();
    if (!obj) return;
    cJSON_AddStringToObject(obj, "type", "status");

    char *payload = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!payload) return;

    if (httpd_queue_work(g_server, ws_broadcast_work, payload) != ESP_OK)
        free(payload);
}
