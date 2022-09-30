#ifndef _MODULE_SNTP_H_
#define _MODULE_SNTP_H_
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

bool initialize_sntp();
double get_tick_ms();

#ifdef __cplusplus
}
#endif
#endif