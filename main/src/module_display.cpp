#include "module_display.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "defines.h"

static const char *TAG = "DISPLAY";
static uint16_t display_buffer[8];

#define HT16K33_BLINK_CMD       0x80
#define HT16K33_BLINK_DISPLAYON 0x01 
#define HT16K33_CMD_BRIGHTNESS  0xE0 

static const uint16_t alphafonttable[] = {
    0b0000000000000001, 0b0000000000000010, 0b0000000000000100,
    0b0000000000001000, 0b0000000000010000, 0b0000000000100000,
    0b0000000001000000, 0b0000000010000000, 0b0000000100000000,
    0b0000001000000000, 0b0000010000000000, 0b0000100000000000,
    0b0001000000000000, 0b0010000000000000, 0b0100000000000000,
    0b1000000000000000, 0b0000000000000000, 0b0000000000000000,
    0b0000000000000000, 0b0000000000000000, 0b0000000000000000,
    0b0000000000000000, 0b0000000000000000, 0b0000000000000000,
    0b0001001011001001, 0b0001010111000000, 0b0001001011111001,
    0b0000000011100011, 0b0000010100110000, 0b0001001011001000,
    0b0011101000000000, 0b0001011100000000,
    0b0000000000000000, //
    0b0000000000000110, // !
    0b0000001000100000, // "
    0b0001001011001110, // #
    0b0001001011101101, // $
    0b0000110000100100, // %
    0b0010001101011101, // &
    0b0000010000000000, // '
    0b0010010000000000, // (
    0b0000100100000000, // )
    0b0011111111000000, // *
    0b0001001011000000, // +
    0b0000100000000000, // ,
    0b0000000011000000, // -
    0b0100000000000000, // .
    0b0000110000000000, // /
    0b0000110000111111, // 0
    0b0000000000000110, // 1
    0b0000000011011011, // 2
    0b0000000010001111, // 3
    0b0000000011100110, // 4
    0b0010000001101001, // 5
    0b0000000011111101, // 6
    0b0000000000000111, // 7
    0b0000000011111111, // 8
    0b0000000011101111, // 9
    0b0001001000000000, // :
    0b0000101000000000, // ;
    0b0010010000000000, // <
    0b0000000011001000, // =
    0b0000100100000000, // >
    0b0001000010000011, // ?
    0b0000001010111011, // @
    0b0000000011110111, // A
    0b0001001010001111, // B
    0b0000000000111001, // C
    0b0001001000001111, // D
    0b0000000011111001, // E
    0b0000000001110001, // F
    0b0000000010111101, // G
    0b0000000011110110, // H
    0b0001001000001001, // I
    0b0000000000011110, // J
    0b0010010001110000, // K
    0b0000000000111000, // L
    0b0000010100110110, // M
    0b0010000100110110, // N
    0b0000000000111111, // O
    0b0000000011110011, // P
    0b0010000000111111, // Q
    0b0010000011110011, // R
    0b0000000011101101, // S
    0b0001001000000001, // T
    0b0000000000111110, // U
    0b0000110000110000, // V
    0b0010100000110110, // W
    0b0010110100000000, // X
    0b0001010100000000, // Y
    0b0000110000001001, // Z
    0b0000000000111001, // [
    0b0010000100000000, //
    0b0000000000001111, // ]
    0b0000110000000011, // ^
    0b0000000000001000, // _
    0b0000000100000000, // `
    0b0001000001011000, // a
    0b0010000001111000, // b
    0b0000000011011000, // c
    0b0000100010001110, // d
    0b0000100001011000, // e
    0b0000000001110001, // f
    0b0000010010001110, // g
    0b0001000001110000, // h
    0b0001000000000000, // i
    0b0000000000001110, // j
    0b0011011000000000, // k
    0b0000000000110000, // l
    0b0001000011010100, // m
    0b0001000001010000, // n
    0b0000000011011100, // o
    0b0000000101110000, // p
    0b0000010010000110, // q
    0b0000000001010000, // r
    0b0010000010001000, // s
    0b0000000001111000, // t
    0b0000000000011100, // u
    0b0010000000000100, // v
    0b0010100000010100, // w
    0b0010100011000000, // x
    0b0010000000001100, // y
    0b0000100001001000, // z
    0b0000100101001001, // {
    0b0001001000000000, // |
    0b0010010010001001, // }
    0b0000010100100000, // ~
    0b0011111111111111,
};

bool i2c_write(uint8_t* buffer, size_t buffer_len) {
    if (i2c_master_write_to_device(0, LED_DISPLAY_SLAVE_ADDR, buffer, buffer_len, 1000 / portTICK_RATE_MS) != ESP_OK) {
        // ESP_LOGE(TAG, "Failed to write i2c data");
        return false;
    }

    return true;
}

bool initialize_display() {
    i2c_config_t config = {};
    config.mode = I2C_MODE_MASTER;
    config.sda_io_num = 21;
    config.scl_io_num = 22;
    config.sda_pullup_en = true;
    config.scl_pullup_en = true;
    config.master.clk_speed = 400000;

    if (i2c_param_config(0, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to config i2c parameters");
        return false;
    }

    if (i2c_driver_install(0, config.mode, 0, 0, 0) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install i2c driver");
        return false;
    }

    // turn on oscillator
    uint8_t buffer[1] = {0x21};
    i2c_write(buffer, 1);

    display_clear();
    display_refresh();

    display_set_blink_rate(0);
    display_set_brightness(15);

    ESP_LOGI(TAG, "Configured LED Display (I2C)");

    return true;
}

void display_clear() {
    for (uint8_t i = 0; i < 8; i++) {
        display_buffer[i] = 0;
    }
}

bool display_refresh() {
    uint8_t buffer[17];

    buffer[0] = 0x00;

    for (uint8_t i = 0; i < 8; i++) {
        buffer[1 + 2 * i] = display_buffer[i] & 0xFF;
        buffer[2 + 2 * i] = display_buffer[i] >> 8;
    }

    return i2c_write(buffer, 17);
}

void display_write_ascii(uint8_t idx, uint8_t dat, bool dot/*=false*/) {
    if (idx >= 8)
        return;
    
    display_buffer[idx] = alphafonttable[dat];

    if (dot)
        display_buffer[idx] |= (1 << 14);
}

bool display_set_blink_rate(uint8_t rate) {
    if (rate > 3)
        rate = 0;
    uint8_t buffer = HT16K33_BLINK_CMD | HT16K33_BLINK_DISPLAYON | (rate << 1);
    return i2c_write(&buffer, 1);
}

bool display_set_brightness(uint8_t value) {
    if (value > 15)
        value = 15;
    uint8_t buffer = HT16K33_CMD_BRIGHTNESS | value;

    return i2c_write(&buffer, 1);
}