#include "display.h"
#include "board.h"
#include <esp_arduino_version.h>

#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp32s3/rom/cache.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

extern "C" int Cache_WriteBack_Addr(uint32_t addr, uint32_t size);
#endif

// ST7701 init for Guition ESP32-4848S040 (shared by both core builds)
static const uint8_t st7701_4848_init[] = {
  BEGIN_WRITE,
  WRITE_COMMAND_8, 0xFF,
  WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x10,

  WRITE_C8_D16, 0xC0, 0x3B, 0x00,
  WRITE_C8_D16, 0xC1, 0x0D, 0x02,
  WRITE_C8_D16, 0xC2, 0x31, 0x05,
  WRITE_C8_D8, 0xCD, 0x00,

  WRITE_COMMAND_8, 0xB0,
  WRITE_BYTES, 16,
  0x00, 0x11, 0x18, 0x0E,
  0x11, 0x06, 0x07, 0x08,
  0x07, 0x22, 0x04, 0x12,
  0x0F, 0xAA, 0x31, 0x18,

  WRITE_COMMAND_8, 0xB1,
  WRITE_BYTES, 16,
  0x00, 0x11, 0x19, 0x0E,
  0x12, 0x07, 0x08, 0x08,
  0x08, 0x22, 0x04, 0x11,
  0x11, 0xA9, 0x32, 0x18,

  WRITE_COMMAND_8, 0xFF,
  WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x11,

  WRITE_C8_D8, 0xB0, 0x60,
  WRITE_C8_D8, 0xB1, 0x32,
  WRITE_C8_D8, 0xB2, 0x07,
  WRITE_C8_D8, 0xB3, 0x80,
  WRITE_C8_D8, 0xB5, 0x49,
  WRITE_C8_D8, 0xB7, 0x85,
  WRITE_C8_D8, 0xB8, 0x21,
  WRITE_C8_D8, 0xC1, 0x78,
  WRITE_C8_D8, 0xC2, 0x78,

  WRITE_COMMAND_8, 0xE0,
  WRITE_BYTES, 3, 0x00, 0x1B, 0x02,

  WRITE_COMMAND_8, 0xE1,
  WRITE_BYTES, 11,
  0x08, 0xA0, 0x00, 0x00,
  0x07, 0xA0, 0x00, 0x00,
  0x00, 0x44, 0x44,

  WRITE_COMMAND_8, 0xE2,
  WRITE_BYTES, 12,
  0x11, 0x11, 0x44, 0x44,
  0xED, 0xA0, 0x00, 0x00,
  0xEC, 0xA0, 0x00, 0x00,

  WRITE_COMMAND_8, 0xE3,
  WRITE_BYTES, 4, 0x00, 0x00, 0x11, 0x11,

  WRITE_C8_D16, 0xE4, 0x44, 0x44,

  WRITE_COMMAND_8, 0xE5,
  WRITE_BYTES, 16,
  0x0A, 0xE9, 0xD8, 0xA0,
  0x0C, 0xEB, 0xD8, 0xA0,
  0x0E, 0xED, 0xD8, 0xA0,
  0x10, 0xEF, 0xD8, 0xA0,

  WRITE_COMMAND_8, 0xE6,
  WRITE_BYTES, 4, 0x00, 0x00, 0x11, 0x11,

  WRITE_C8_D16, 0xE7, 0x44, 0x44,

  WRITE_COMMAND_8, 0xE8,
  WRITE_BYTES, 16,
  0x09, 0xE8, 0xD8, 0xA0,
  0x0B, 0xEA, 0xD8, 0xA0,
  0x0D, 0xEC, 0xD8, 0xA0,
  0x0F, 0xEE, 0xD8, 0xA0,

  WRITE_COMMAND_8, 0xEB,
  WRITE_BYTES, 7,
  0x02, 0x00, 0xE4, 0xE4,
  0x88, 0x00, 0x40,

  WRITE_C8_D16, 0xEC, 0x3C, 0x00,

  WRITE_COMMAND_8, 0xED,
  WRITE_BYTES, 16,
  0xAB, 0x89, 0x76, 0x54,
  0x02, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0x20,
  0x45, 0x67, 0x98, 0xBA,

  WRITE_COMMAND_8, 0xFF,
  WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x13,

  WRITE_C8_D8, 0xE5, 0xE4,

  WRITE_COMMAND_8, 0xFF,
  WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x00,

  WRITE_C8_D8, 0x3A, 0x60,

  DELAY, 10,
  WRITE_COMMAND_8, 0x11,
  END_WRITE,

  DELAY, 120,

  BEGIN_WRITE,
  WRITE_COMMAND_8, 0x29,
  END_WRITE
};

// Draw into a raw RGB565 buffer (never the scanned front buffer)
// --- Arduino core 3.x (IDF 5.x) build: hand-rolled esp_lcd RGB panel ---
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
class FbGFX : public Arduino_GFX {
public:
  FbGFX() : Arduino_GFX(TFT_WIDTH, TFT_HEIGHT) {
    setRotation(0);
  }

  void setFramebuffer(uint16_t *fb) { _fb = fb; }

  bool begin(int32_t speed = GFX_NOT_DEFINED) override {
    (void)speed;
    return _fb != nullptr;
  }

  void writePixelPreclipped(int16_t x, int16_t y, uint16_t color) override {
    _fb[(int32_t)y * WIDTH + x] = color;
  }

  void writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override {
    if (!_fb || h <= 0) return;
    if (x < 0 || x >= WIDTH) return;
    if (y < 0) { h += y; y = 0; }
    if (y + h > HEIGHT) h = HEIGHT - y;
    if (h <= 0) return;
    uint16_t *p = _fb + (int32_t)y * WIDTH + x;
    while (h--) {
      *p = color;
      p += WIDTH;
    }
  }

  void writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override {
    if (!_fb || w <= 0) return;
    if (y < 0 || y >= HEIGHT) return;
    if (x < 0) { w += x; x = 0; }
    if (x + w > WIDTH) w = WIDTH - x;
    if (w <= 0) return;
    uint16_t *p = _fb + (int32_t)y * WIDTH + x;
    while (w--) *p++ = color;
  }

  void writeFillRectPreclipped(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override {
    if (!_fb || w <= 0 || h <= 0) return;
    uint16_t *row = _fb + (int32_t)y * WIDTH + x;
    for (int16_t j = 0; j < h; j++) {
      uint16_t *p = row;
      for (int16_t i = 0; i < w; i++) *p++ = color;
      row += WIDTH;
    }
  }

private:
  uint16_t *_fb = nullptr;
};

static FbGFX fbGfx;
Arduino_GFX *gfx = &fbGfx;

static Arduino_DataBus *bus = nullptr;
static esp_lcd_panel_handle_t panel = nullptr;
static uint16_t *fbs[2] = {nullptr, nullptr};
static int backIdx = 0;
static SemaphoreHandle_t semVsync = nullptr;
static const size_t FB_BYTES = (size_t)TFT_WIDTH * TFT_HEIGHT * 2;

static bool IRAM_ATTR onVsync(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t *, void *) {
  BaseType_t hp = pdFALSE;
  xSemaphoreGiveFromISR(semVsync, &hp);
  return hp == pdTRUE;
}

bool displayBegin() {
  bus = new Arduino_SWSPI(
    GFX_NOT_DEFINED /* DC */, TFT_CS,
    TFT_SCK, TFT_SDA, GFX_NOT_DEFINED /* MISO */);

  if (!bus->begin()) return false;
  bus->batchOperation((uint8_t *)st7701_4848_init, sizeof(st7701_4848_init));

  semVsync = xSemaphoreCreateBinary();
  if (!semVsync) return false;

  esp_lcd_rgb_panel_config_t cfg = {};
  cfg.clk_src = LCD_CLK_SRC_DEFAULT;
  cfg.timings.pclk_hz = 12000000;
  cfg.timings.h_res = TFT_WIDTH;
  cfg.timings.v_res = TFT_HEIGHT;
  cfg.timings.hsync_pulse_width = 8;
  cfg.timings.hsync_back_porch = 50;
  cfg.timings.hsync_front_porch = 10;
  cfg.timings.vsync_pulse_width = 8;
  cfg.timings.vsync_back_porch = 20;
  cfg.timings.vsync_front_porch = 10;
  cfg.timings.flags.hsync_idle_low = 0;
  cfg.timings.flags.vsync_idle_low = 0;
  cfg.timings.flags.de_idle_high = 0;
  cfg.timings.flags.pclk_active_neg = 0;
  cfg.timings.flags.pclk_idle_high = 0;

  cfg.data_width = 16;
  cfg.bits_per_pixel = 16;
  cfg.num_fbs = 2;
  cfg.bounce_buffer_size_px = 10 * TFT_WIDTH;
  cfg.hsync_gpio_num = TFT_HSYNC;
  cfg.vsync_gpio_num = TFT_VSYNC;
  cfg.de_gpio_num = TFT_DE;
  cfg.pclk_gpio_num = TFT_PCLK;
  cfg.disp_gpio_num = GPIO_NUM_NC;

  // RGB565 pin order used by Arduino_GFX for this board (little-endian path)
  cfg.data_gpio_nums[0] = TFT_B0;
  cfg.data_gpio_nums[1] = TFT_B1;
  cfg.data_gpio_nums[2] = TFT_B2;
  cfg.data_gpio_nums[3] = TFT_B3;
  cfg.data_gpio_nums[4] = TFT_B4;
  cfg.data_gpio_nums[5] = TFT_G0;
  cfg.data_gpio_nums[6] = TFT_G1;
  cfg.data_gpio_nums[7] = TFT_G2;
  cfg.data_gpio_nums[8] = TFT_G3;
  cfg.data_gpio_nums[9] = TFT_G4;
  cfg.data_gpio_nums[10] = TFT_G5;
  cfg.data_gpio_nums[11] = TFT_R0;
  cfg.data_gpio_nums[12] = TFT_R1;
  cfg.data_gpio_nums[13] = TFT_R2;
  cfg.data_gpio_nums[14] = TFT_R3;
  cfg.data_gpio_nums[15] = TFT_R4;

  cfg.flags.disp_active_low = true;
  cfg.flags.refresh_on_demand = false;
  cfg.flags.fb_in_psram = true;
  cfg.flags.double_fb = true;
  cfg.flags.no_fb = false;
  cfg.flags.bb_invalidate_cache = false;

  if (esp_lcd_new_rgb_panel(&cfg, &panel) != ESP_OK) return false;

  esp_lcd_rgb_panel_event_callbacks_t cbs = {};
  cbs.on_vsync = onVsync;
  esp_lcd_rgb_panel_register_event_callbacks(panel, &cbs, nullptr);

  if (esp_lcd_panel_reset(panel) != ESP_OK) return false;
  if (esp_lcd_panel_init(panel) != ESP_OK) return false;

  void *p0 = nullptr;
  void *p1 = nullptr;
  if (esp_lcd_rgb_panel_get_frame_buffer(panel, 2, &p0, &p1) != ESP_OK) return false;
  fbs[0] = (uint16_t *)p0;
  fbs[1] = (uint16_t *)p1;

  memset(fbs[0], 0, FB_BYTES);
  memset(fbs[1], 0, FB_BYTES);
  Cache_WriteBack_Addr((uint32_t)fbs[0], FB_BYTES);
  Cache_WriteBack_Addr((uint32_t)fbs[1], FB_BYTES);

  // Show buffer 0; draw into buffer 1
  esp_lcd_panel_draw_bitmap(panel, 0, 0, TFT_WIDTH, TFT_HEIGHT, fbs[0]);
  backIdx = 1;
  fbGfx.setFramebuffer(fbs[backIdx]);
  if (!fbGfx.begin()) return false;

  pinMode(TFT_BL, OUTPUT);
  displaySetBacklight(true);
  return true;
}

void displaySetBacklight(bool on) {
  digitalWrite(TFT_BL, on ? HIGH : LOW);
}

uint16_t *displayBackBuffer() {
  return fbs[backIdx];
}

void displayPresent() {
  if (!panel || !fbs[backIdx]) return;

  Cache_WriteBack_Addr((uint32_t)fbs[backIdx], FB_BYTES);

  // Drop stale vsync, then swap and wait for the new frame to latch
  xSemaphoreTake(semVsync, 0);
  esp_lcd_panel_draw_bitmap(panel, 0, 0, TFT_WIDTH, TFT_HEIGHT, fbs[backIdx]);
  xSemaphoreTake(semVsync, pdMS_TO_TICKS(50));

  backIdx ^= 1;
  fbGfx.setFramebuffer(fbs[backIdx]);
}

#else
// --- Arduino core 2.x (IDF 4.4) build: Arduino_GFX RGB panel + PSRAM canvas ---
// Same ST7701 init array, same pins, same 12MHz timings as the core 3 build.
// Deauth frame transmission (esp_wifi_80211_tx with 0xC0 frames) is only
// permitted by the WiFi stack on this core, so the deauth tool runs here.

static Arduino_DataBus *bus2 = nullptr;
static Arduino_ESP32RGBPanel *rgbpanel = nullptr;
static Arduino_RGB_Display *rgb_display = nullptr;
static Arduino_Canvas *canvas = nullptr;
Arduino_GFX *gfx = nullptr;

bool displayBegin() {
  bus2 = new Arduino_SWSPI(
      GFX_NOT_DEFINED /* DC */, TFT_CS,
      TFT_SCK, TFT_SDA, GFX_NOT_DEFINED /* MISO */);

  // Timings identical to the core 3 build:
  // hsync pw 8 / bp 50 / fp 10, vsync pw 8 / bp 20 / fp 10, idle high.
  rgbpanel = new Arduino_ESP32RGBPanel(
      TFT_DE, TFT_VSYNC, TFT_HSYNC, TFT_PCLK,
      TFT_R0, TFT_R1, TFT_R2, TFT_R3, TFT_R4,
      TFT_G0, TFT_G1, TFT_G2, TFT_G3, TFT_G4, TFT_G5,
      TFT_B0, TFT_B1, TFT_B2, TFT_B3, TFT_B4,
      1, 10, 8, 50,
      1, 10, 8, 20,
      0 /* pclk_active_neg */);

  rgb_display = new Arduino_RGB_Display(
      TFT_WIDTH, TFT_HEIGHT, rgbpanel, 0 /* rotation */, false /* auto flush */,
      bus2, GFX_NOT_DEFINED /* rst */,
      st7701_4848_init, sizeof(st7701_4848_init));

  canvas = new Arduino_Canvas(TFT_WIDTH, TFT_HEIGHT, rgb_display);
  gfx = canvas;

  if (!gfx->begin()) return false;
  gfx->fillScreen(0x0000);
  canvas->flush();
  pinMode(TFT_BL, OUTPUT);
  displaySetBacklight(true);
  return true;
}

void displaySetBacklight(bool on) {
  digitalWrite(TFT_BL, on ? HIGH : LOW);
}

uint16_t *displayBackBuffer() {
  return nullptr;
}

void displayPresent() {
  if (canvas) canvas->flush();
}
#endif
