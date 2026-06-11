#ifndef _MODULE_WIFI_PROVISIONING_H_
#define _MODULE_WIFI_PROVISIONING_H_
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

bool initialize_wifi_provisioning();

bool wifi_get_sta_status(char *ssid_out, char *ip_out);
void wifi_do_connect(const char *ssid, const char *password);
void wifi_do_forget();

#ifdef __cplusplus
}
#endif
#endif
