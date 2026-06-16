#ifndef _MODULE_WEBSERVER_H_
#define _MODULE_WEBSERVER_H_
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

bool initialize_webserver();
void stop_webserver();
void webserver_push_state_update();
void webserver_push_wifi_update();

#ifdef __cplusplus
}
#endif
#endif
