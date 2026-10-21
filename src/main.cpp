#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <TAMC_GT911.h>
#include "board.h"
#include "display.h"
#include "menu_return.h"
#include "types.h"
#include "definitions.h"
#include "deauth.h"
#include "applejuice.h"

// Web UI removed: everything is driven from the 480x480 GT911 touchscreen.
// Layout constants mirror the wifiscan app exactly.

static const int MAX_NETS = 24;
static const int LIST_Y = 88;
static const int LIST_BOTTOM = 348;
static const int ROW_H = 52;
static const int DETAIL_Y = 356;

// Button row (same position as wifiscan's SCAN button)
static const int BTN_Y = 424;
static const int BTN_H = 44;
static const int BTN_SCAN_X = 24,   BTN_SCAN_W = 100;
static const int BTN_DEAUTH_X = 134, BTN_DEAUTH_W = 110;
static const int BTN_ALL_X = 254,   BTN_ALL_W = 90;
static const int BTN_JUICE_X = 354, BTN_JUICE_W = 102;

enum AuthKind {
  AUTH_OPEN,
  AUTH_WEP,
  AUTH_WPA,
  AUTH_WPA2,
  AUTH_WPA3,
  AUTH_ENTERPRISE,
  AUTH_OTHER
};

struct Net {
  char ssid[33];
  uint8_t bssid[6];
  int rssi;
  int channel;
  AuthKind auth;
};

static TAMC_GT911 touch(TP_SDA, TP_SCL, TP_INT, TP_RST, TFT_WIDTH, TFT_HEIGHT);
static Net nets[MAX_NETS];
static int netCount = 0;
static int scrollY = 0;
static int selected = -1;
static bool scanning = true;
static bool wasDown = false;
static bool tracking = false;
static bool moved = false;
static int grabX = 0;
static int grabY = 0;
static int grabScroll = 0;

// Attack state
static bool deauthSingle = false;   // running on the selected network
static bool deauthAll = false;      // running on all networks
static bool juiceRunning = false;
static int shownElim = -1;
static int curr_channel = 1;
static bool dirty = true;

// Bluetooth flood screen (opened via the BLE button)
static bool btScreen = false;
static uint32_t lastBtUi = 0;
static const int BT_FLOOD_X = 90, BT_FLOOD_Y = 270, BT_FLOOD_W = 300, BT_FLOOD_H = 84;
static const int BT_BACK_X = 24, BT_BACK_Y = 424, BT_BACK_W = 140, BT_BACK_H = 44;

static void render();

static AuthKind authFrom(wifi_auth_mode_t mode) {
  switch (mode) {
    case WIFI_AUTH_OPEN: return AUTH_OPEN;
    case WIFI_AUTH_WEP: return AUTH_WEP;
    case WIFI_AUTH_WPA_PSK: return AUTH_WPA;
    case WIFI_AUTH_WPA2_PSK: return AUTH_WPA2;
    case WIFI_AUTH_WPA_WPA2_PSK: return AUTH_WPA2;
#ifdef WIFI_AUTH_WPA3_PSK
    case WIFI_AUTH_WPA3_PSK: return AUTH_WPA3;
#endif
#ifdef WIFI_AUTH_WPA2_WPA3_PSK
    case WIFI_AUTH_WPA2_WPA3_PSK: return AUTH_WPA3;
#endif
    case WIFI_AUTH_WPA2_ENTERPRISE: return AUTH_ENTERPRISE;
    default: return AUTH_OTHER;
  }
}

static const char *authLabel(AuthKind auth) {
  switch (auth) {
    case AUTH_OPEN: return "open";
    case AUTH_WEP: return "WEP";
    case AUTH_WPA: return "WPA";
    case AUTH_WPA2: return "WPA2";
    case AUTH_WPA3: return "WPA3";
    case AUTH_ENTERPRISE: return "enterprise";
    default: return "other";
  }
}

static int barsFor(int rssi) {
  if (rssi >= -50) return 4;
  if (rssi >= -60) return 3;
  if (rssi >= -70) return 2;
  if (rssi >= -80) return 1;
  return 0;
}

static int listMaxScroll() {
  int content = netCount * ROW_H;
  int view = LIST_BOTTOM - LIST_Y;
  return content > view ? content - view : 0;
}

static void clampScroll() {
  int m = listMaxScroll();
  if (scrollY < 0) scrollY = 0;
  if (scrollY > m) scrollY = m;
}

static void scrollSelectedIntoView() {
  if (selected < 0) return;
  int y = selected * ROW_H;
  int view = LIST_BOTTOM - LIST_Y;
  if (y < scrollY) scrollY = y;
  if (y + ROW_H > scrollY + view) scrollY = y + ROW_H - view;
  clampScroll();
}

static void scan() {
  // A scan breaks any running attack (channels are hopped during scan)
  if (deauthSingle || deauthAll) {
    stop_deauth();
    deauthSingle = false;
    deauthAll = false;
    shownElim = -1;
  }

  scanning = true;
  selected = -1;
  dirty = true;
  render();

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);
  int n = WiFi.scanNetworks(false, true);
  netCount = 0;
  scrollY = 0;
  if (n < 0) n = 0;
  for (int i = 0; i < n && netCount < MAX_NETS; i++) {
    String name = WiFi.SSID(i);
    if (name.length() == 0) name = "(hidden)";
    name.toCharArray(nets[netCount].ssid, sizeof(nets[netCount].ssid));
    const uint8_t *bssid = WiFi.BSSID(i);
    if (bssid) {
      memcpy(nets[netCount].bssid, bssid, 6);
    } else {
      memset(nets[netCount].bssid, 0, 6);
    }
    nets[netCount].rssi = WiFi.RSSI(i);
    nets[netCount].channel = WiFi.channel(i);
    nets[netCount].auth = authFrom(WiFi.encryptionType(i));
    netCount++;
  }
  WiFi.scanDelete();
  scanning = false;
  Serial.printf("[scan] %d networks\n", netCount);
}

static void drawBars(int x, int y, int level) {
  for (int i = 0; i < 4; i++) {
    int h = 6 + i * 5;
    uint16_t c = i < level ? 0x07E0 : 0x2104;
    gfx->fillRect(x + i * 8, y + 22 - h, 6, h, c);
  }
}

static void formatBssid(const uint8_t *bssid, char *out, size_t n) {
  snprintf(out, n, "%02X:%02X:%02X:%02X:%02X:%02X",
           bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
}

static void drawHeader() {
  gfx->fillRect(0, 0, TFT_WIDTH, LIST_Y - 6, 0x0841);
  gfx->setTextSize(3);
  gfx->setTextColor(0x07FF);
  gfx->setCursor(20, 22);
  gfx->print("DEAUTH");
  gfx->setTextSize(2);
  if (deauthAll) {
    gfx->setTextColor(0xF800);
    gfx->setCursor(150, 32);
    gfx->print("ALL NETWORKS");
  } else if (deauthSingle) {
    gfx->setTextColor(0xF800);
    gfx->setCursor(150, 32);
    gfx->print("ATTACK ON");
  } else if (scanning) {
    gfx->setTextColor(0xC618);
    gfx->setCursor(150, 32);
    gfx->print("scanning");
  } else {
    gfx->setTextColor(0xC618);
    gfx->setCursor(150, 32);
    gfx->printf("%d found", netCount);
  }

  gfx->fillRoundRect(362, 6, 108, 34, 6, 0x0000);
  gfx->drawRoundRect(362, 6, 108, 34, 6, 0x07FF);
  gfx->setTextColor(0x07FF);
  gfx->setCursor(378, 14);
  gfx->print("MENU");
}

static void drawDetail() {
  gfx->fillRoundRect(12, DETAIL_Y, 456, 58, 8, 0x1082);
  gfx->setTextSize(1);

  if (deauthAll) {
    gfx->setTextSize(2);
    gfx->setTextColor(0xF800);
    gfx->setCursor(24, DETAIL_Y + 8);
    gfx->print("DEAUTHING ALL NETWORKS");
    gfx->setTextSize(1);
    gfx->setTextColor(0xC618);
    gfx->setCursor(24, DETAIL_Y + 34);
    gfx->printf("stations knocked: %d   tap ALL to stop", eliminated_stations);
    return;
  }

  if (selected < 0 || selected >= netCount) {
    gfx->setTextColor(0xC618);
    gfx->setCursor(28, DETAIL_Y + 22);
    gfx->print("Tap a network, then DEAUTH");
    return;
  }

  const Net &net = nets[selected];
  char bssid[20];
  formatBssid(net.bssid, bssid, sizeof(bssid));

  gfx->setTextSize(2);
  gfx->setTextColor(deauthSingle ? 0xF800 : 0xFFFF);
  gfx->setCursor(24, DETAIL_Y + 8);
  gfx->print(net.ssid);

  gfx->setTextSize(1);
  gfx->setTextColor(0xC618);
  gfx->setCursor(24, DETAIL_Y + 34);
  if (deauthSingle) {
    gfx->printf("stations knocked: %d   tap DEAUTH to stop", eliminated_stations);
  } else {
    gfx->printf("%s   ch %d   %d dBm   %s",
                bssid, net.channel, net.rssi, authLabel(net.auth));
  }
}

static void drawButtons() {
  // SCAN — same behavior as wifiscan
  gfx->fillRoundRect(BTN_SCAN_X, BTN_Y, BTN_SCAN_W, BTN_H, 8, scanning ? 0x4208 : 0x07FF);
  gfx->setTextSize(2);
  gfx->setTextColor(scanning ? 0xC618 : 0x0000);
  gfx->setCursor(BTN_SCAN_X + 26, BTN_Y + 14);
  gfx->print(scanning ? "WAIT" : "SCAN");

  // DEAUTH — attack on the selected network
  bool canDeauth = !scanning && !deauthAll && selected >= 0 && selected < netCount;
  gfx->fillRoundRect(BTN_DEAUTH_X, BTN_Y, BTN_DEAUTH_W, BTN_H, 8,
                     deauthSingle ? 0xF800 : (canDeauth ? 0xFBE0 : 0x4208));
  gfx->setTextColor(deauthSingle ? 0xFFFF : (canDeauth ? 0x0000 : 0xC618));
  gfx->setCursor(BTN_DEAUTH_X + 19, BTN_Y + 14);
  gfx->print(deauthSingle ? "STOP" : "DEAUTH");

  // ALL — deauth every network
  gfx->fillRoundRect(BTN_ALL_X, BTN_Y, BTN_ALL_W, BTN_H, 8, deauthAll ? 0xF800 : 0xFBE0);
  gfx->setTextColor(deauthAll ? 0xFFFF : 0x0000);
  gfx->setCursor(BTN_ALL_X + 28, BTN_Y + 14);
  gfx->print(deauthAll ? "STOP" : "ALL");

  // BLE — opens the Bluetooth flood screen
  gfx->fillRoundRect(BTN_JUICE_X, BTN_Y, BTN_JUICE_W, BTN_H, 8, juiceRunning ? 0xF81F : 0x7BEF);
  gfx->setTextColor(juiceRunning ? 0xFFFF : 0x0000);
  gfx->setCursor(BTN_JUICE_X + (juiceRunning ? 12 : 27), BTN_Y + 14);
  gfx->print(juiceRunning ? "BLE ON" : "BLE");
}

static void renderBt() {
  gfx->fillScreen(0x0841);

  // Title
  gfx->setTextSize(3);
  gfx->setTextColor(0xF81F);
  gfx->setCursor(20, 22);
  gfx->print("BLE FLOOD");

  // MENU corner
  gfx->fillRoundRect(362, 6, 108, 34, 6, 0x0000);
  gfx->drawRoundRect(362, 6, 108, 34, 6, 0x07FF);
  gfx->setTextSize(2);
  gfx->setTextColor(0x07FF);
  gfx->setCursor(378, 14);
  gfx->print("MENU");

  // Status card
  gfx->fillRoundRect(12, 96, 456, 156, 10, 0x1082);
  gfx->setTextSize(3);
  gfx->setTextColor(juiceRunning ? 0xF81F : 0xC618);
  gfx->setCursor(24, 112);
  gfx->print(juiceRunning ? "FLOODING" : "IDLE");
  gfx->setTextSize(2);
  gfx->setTextColor(0xFFFF);
  gfx->setCursor(24, 160);
  gfx->printf("adverts sent: %lu", (unsigned long)applejuice_cycles);
  gfx->setTextSize(1);
  gfx->setTextColor(0xC618);
  gfx->setCursor(24, 192);
  gfx->print("Apple continuity spam, rotating fake MACs");
  gfx->setCursor(24, 210);
  gfx->print(juiceRunning ? "Tap STOP to end the flood" : "Tap FLOOD to start the flood");

  // Big FLOOD / STOP button
  gfx->fillRoundRect(BT_FLOOD_X, BT_FLOOD_Y, BT_FLOOD_W, BT_FLOOD_H, 12,
                     juiceRunning ? 0xF800 : 0x7BEF);
  gfx->setTextSize(3);
  gfx->setTextColor(juiceRunning ? 0xFFFF : 0x0000);
  gfx->setCursor(BT_FLOOD_X + 96, BT_FLOOD_Y + 30);
  gfx->print(juiceRunning ? "STOP" : "FLOOD");

  // BACK button
  gfx->fillRoundRect(BT_BACK_X, BT_BACK_Y, BT_BACK_W, BT_BACK_H, 8, 0x1082);
  gfx->setTextSize(2);
  gfx->setTextColor(0x07FF);
  gfx->setCursor(BT_BACK_X + 40, BT_BACK_Y + 14);
  gfx->print("BACK");

  displayPresent();
}

static void render() {
  if (btScreen) {
    renderBt();
    return;
  }

  gfx->fillScreen(0x0841);

  if (!scanning && netCount == 0) {
    gfx->setTextSize(2);
    gfx->setTextColor(0xFFFF);
    gfx->setCursor(40, 200);
    gfx->print("No networks");
  }

  for (int i = 0; i < netCount; i++) {
    int y = LIST_Y - scrollY + i * ROW_H;
    if (y + ROW_H - 6 < LIST_Y || y > LIST_BOTTOM) continue;
    bool isSel = (i == selected);
    gfx->fillRoundRect(12, y, 456, ROW_H - 6, 6, isSel ? (deauthSingle ? 0x6000 : 0x1A6E) : 0x1082);
    if (isSel) gfx->drawRoundRect(12, y, 456, ROW_H - 6, 6, deauthSingle ? 0xF800 : 0x07FF);
    drawBars(24, y + 10, barsFor(nets[i].rssi));
    gfx->setTextSize(2);
    gfx->setTextColor(0xFFFF);
    gfx->setCursor(64, y + 8);
    gfx->print(nets[i].ssid);
    gfx->setTextSize(1);
    gfx->setTextColor(nets[i].auth == AUTH_OPEN ? 0x07E0 : 0xFD20);
    gfx->setCursor(64, y + 30);
    gfx->printf("ch %d   %d dBm   %s",
                nets[i].channel, nets[i].rssi, authLabel(nets[i].auth));
  }

  int maxS = listMaxScroll();
  if (maxS > 0) {
    int trackH = LIST_BOTTOM - LIST_Y;
    int thumbH = trackH * trackH / (trackH + maxS);
    if (thumbH < 28) thumbH = 28;
    if (thumbH > trackH) thumbH = trackH;
    int thumbY = LIST_Y + (trackH - thumbH) * scrollY / maxS;
    gfx->fillRoundRect(466, LIST_Y, 8, trackH, 3, 0x2104);
    gfx->fillRoundRect(466, thumbY, 8, thumbH, 3, 0x07FF);
  }

  drawHeader();
  drawDetail();
  drawButtons();

  displayPresent();
}

static int tappedNetwork(int tx, int ty) {
  if (ty < LIST_Y || ty >= LIST_BOTTOM) return -1;
  for (int i = 0; i < netCount; i++) {
    int y = LIST_Y - scrollY + i * ROW_H;
    if (tx >= 12 && tx < 468 && ty >= y && ty < y + ROW_H - 6) return i;
  }
  return -1;
}

static bool inRect(int tx, int ty, int x, int y, int w, int h) {
  return tx >= x && tx < x + w && ty >= y && ty < y + h;
}

static void toggleDeauthSingle() {
  if (deauthSingle) {
    stop_deauth();
    deauthSingle = false;
    shownElim = -1;
  } else if (!scanning && !deauthAll && selected >= 0 && selected < netCount) {
    start_deauth(nets[selected].bssid, nets[selected].channel, DEAUTH_TYPE_SINGLE, 1);  // reason 1 = unspecified
    deauthSingle = true;
    shownElim = 0;
  }
}

static void toggleDeauthAll() {
  if (deauthAll) {
    stop_deauth();
    deauthAll = false;
    shownElim = -1;
  } else {
    if (deauthSingle) {
      stop_deauth();
      deauthSingle = false;
    }
    start_deauth(nullptr, 0, DEAUTH_TYPE_ALL, 1);  // reason 1 = unspecified
    deauthAll = true;
    curr_channel = 1;
    shownElim = 0;
  }
}

static void toggleJuice() {
  if (juiceRunning) {
    stop_attack_task();
    juiceRunning = false;
  } else {
    start_attack_task();
    juiceRunning = true;
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("Deauther touch");
  Serial.printf("boot reason %d\n", (int)esp_reset_reason());

  if (!displayBegin()) {
    Serial.println("Display init failed");
    while (true) delay(1000);
  }

  Wire.begin(TP_SDA, TP_SCL);
  touch.begin();
  touch.setRotation(ROTATION_INVERTED);
  touch.setResolution(TFT_WIDTH, TFT_HEIGHT);

  applejuice_init();

  scan();
  render();
  dirty = false;
}

void loop() {
  // Deauth-all hops channels rapidly (mirrors the original tool)
  if (deauthAll) {
    if (curr_channel > CHANNEL_MAX) curr_channel = 1;
    esp_wifi_set_channel(curr_channel, WIFI_SECOND_CHAN_NONE);
    curr_channel++;
    if (eliminated_stations != shownElim) {
      shownElim = eliminated_stations;
      dirty = true;
    }
  }

  touch.read();
  bool down = touch.isTouched && touch.touches > 0;
  int tx = down ? touch.points[0].x : 0;
  int ty = down ? touch.points[0].y : 0;

  if (down && !wasDown) {
    if (menuCornerHit(tx, ty)) returnToLauncher();
    tracking = true;
    moved = false;
    grabX = tx;
    grabY = ty;
    grabScroll = scrollY;
  }

  if (down && tracking && !scanning && !deauthAll && !btScreen) {
    int dy = ty - grabY;
    if (abs(dy) > 12) moved = true;
    if (moved) {
      int next = grabScroll - dy;
      int maxS = listMaxScroll();
      if (next < 0) next = 0;
      if (next > maxS) next = maxS;
      if (next != scrollY) {
        scrollY = next;
        dirty = true;
      }
    }
  }

  if (!down && wasDown && tracking) {
    if (!moved) {
      if (btScreen) {
        if (inRect(grabX, grabY, BT_FLOOD_X, BT_FLOOD_Y, BT_FLOOD_W, BT_FLOOD_H)) {
          toggleJuice();
        } else if (inRect(grabX, grabY, BT_BACK_X, BT_BACK_Y, BT_BACK_W, BT_BACK_H)) {
          btScreen = false;
        }
      } else if (inRect(grabX, grabY, BTN_SCAN_X, BTN_Y, BTN_SCAN_W, BTN_H)) {
        if (!scanning) scan();
      } else if (inRect(grabX, grabY, BTN_DEAUTH_X, BTN_Y, BTN_DEAUTH_W, BTN_H)) {
        toggleDeauthSingle();
      } else if (inRect(grabX, grabY, BTN_ALL_X, BTN_Y, BTN_ALL_W, BTN_H)) {
        toggleDeauthAll();
      } else if (inRect(grabX, grabY, BTN_JUICE_X, BTN_Y, BTN_JUICE_W, BTN_H)) {
        btScreen = true;
      } else if (!deauthAll) {
        int hit = tappedNetwork(grabX, grabY);
        if (hit >= 0 && !deauthSingle) {
          selected = hit;
          scrollSelectedIntoView();
        }
      }
    }
    tracking = false;
  }

  if (!down) tracking = false;
  wasDown = down;

  if (deauthSingle && eliminated_stations != shownElim) {
    shownElim = eliminated_stations;
    dirty = true;
  }

  if (btScreen && millis() - lastBtUi >= 500) {
    lastBtUi = millis();
    dirty = true;  // refresh the advert counter
  }

  if (dirty) {
    render();
    dirty = false;
  }

  delay(deauthAll ? 10 : 16);
}
