#ifndef _MODULE_GPIO_H_
#define _MODULE_GPIO_H_
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool initialize_gpio();
bool turn_led1(uint32_t v);
bool toggle_led1();
bool turn_led2(uint32_t v);
bool toggle_led2();
bool turn_relay(uint32_t v);
bool blink_relay();

#ifdef __cplusplus
}
#endif
#endif