#ifndef _MODULE_MQTT_H_
#define _MODULE_MQTT_H_
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

bool initialize_mqtt();
bool start_mqtt();
bool stop_mqtt();
bool mqtt_publish_current_state();
bool mqtt_is_connected();

#ifdef __cplusplus
}
#endif
#endif