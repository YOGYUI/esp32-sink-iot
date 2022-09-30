#ifndef _MODULE_DISPLAY_H_
#define _MODULE_DISPLAY_H_
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool initialize_display();
void display_clear();
bool display_refresh();
void display_write_ascii(uint8_t idx, uint8_t dat, bool dot = false);
bool display_set_blink_rate(uint8_t rate);
bool display_set_brightness(uint8_t value);

#ifdef __cplusplus
}
#endif
#endif