#ifndef _MODULE_TIMER_H_
#define _MODULE_TIMER_H_
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_timer.h"

bool initialize_timer1(uint64_t period_us, esp_timer_cb_t callback);
bool initialize_timer2(uint64_t period_us, esp_timer_cb_t callback);

#ifdef __cplusplus
}
#endif
#endif