#include "defines.h"
#include "nvs.h"
#include "esp_log.h"

StFlowSensor* flow_sensor = new StFlowSensor();
StGpioConfig* gpio_cfg    = new StGpioConfig();
StMiscConfig* misc_cfg    = new StMiscConfig();

void load_misc_config() {
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE_CFG, NVS_READONLY, &nvs) != ESP_OK)
        return;

    uint32_t v32;
    uint8_t  v8;
    if (nvs_get_u32(nvs, NVS_KEY_MISC_RELAY_MS,  &v32) == ESP_OK) misc_cfg->relay_toggle_ms    = v32;
    if (nvs_get_u32(nvs, NVS_KEY_MISC_FLOW_PPL,  &v32) == ESP_OK) misc_cfg->flow_pulse_per_liter = v32;
    if (nvs_get_u8 (nvs, NVS_KEY_MISC_DISP_ADDR, &v8)  == ESP_OK) misc_cfg->display_slave_addr = v8;
    nvs_close(nvs);

    ESP_LOGI("MISC_CFG", "relay_ms=%lu ppl=%lu disp_addr=0x%02x",
             misc_cfg->relay_toggle_ms, misc_cfg->flow_pulse_per_liter, misc_cfg->display_slave_addr);
}
