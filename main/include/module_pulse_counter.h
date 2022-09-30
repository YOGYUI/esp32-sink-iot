#ifndef _MODULE_PULSE_COUNTER_H_
#define _MODULE_PULSE_COUNTER_H_
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool initialize_pulse_counter();
bool clear_pulse_counter();
int16_t get_pulse_count();

#ifdef __cplusplus
}
#endif
#endif