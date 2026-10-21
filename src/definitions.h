#ifndef DEFINITIONS_H
#define DEFINITIONS_H

// The softAP is only brought up during a single-network deauth so the
// AP interface transmits on the target channel. It is hidden; no web
// interface exists in this build.
#define AP_SSID "hahahaha"
#define AP_PASS "11111111"
#define CHANNEL_MAX 13
#define NUM_FRAMES_PER_DEAUTH 16
#define DEAUTH_TYPE_SINGLE 0
#define DEAUTH_TYPE_ALL 1

#endif
