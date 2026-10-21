#pragma once

// Games loaded from the SD card boot out of the "game" slot.
// Hold is not required: a tap on the MENU corner switches the boot
// partition back to the launcher and restarts.

#include <esp_ota_ops.h>
#include <esp_system.h>

inline bool menuCornerHit(int x, int y) {
  return x >= 360 && y >= 0 && y < 46;
}

inline void returnToLauncher() {
  const esp_partition_t *launcher = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, "launcher");
  if (!launcher) return;
  esp_ota_set_boot_partition(launcher);
  esp_restart();
}
