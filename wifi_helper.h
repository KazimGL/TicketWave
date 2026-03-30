#ifndef WIFI_HELPER_H
#define WIFI_HELPER_H

#include <Arduino.h>

// Add these declarations:
void save_wifi_to_nvs(String ssid, String pass);
void wifi_auto_reconnect_logic();
void attempt_connect(String ssid, String pass);

#endif