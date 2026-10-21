#ifndef DEAUTH_H
#define DEAUTH_H

#include <Arduino.h>

// bssid/channel come from the app's own stored scan data;
// WiFi scan records are deleted after each scan, so reading them
// back via WiFi.BSSID()/WiFi.channel() would return NULL/0 and crash.
void start_deauth(const uint8_t *bssid, int channel, int attack_type, uint16_t reason);
void stop_deauth();

extern int eliminated_stations;
extern int deauth_type;

#endif
