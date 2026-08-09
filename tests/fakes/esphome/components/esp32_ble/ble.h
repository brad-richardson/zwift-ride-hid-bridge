// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <string>

#include "esp_bt_defs.h"
#include "esphome/components/esp32_ble/ble_uuid.h"
#include "fake_ble.h"

namespace esphome::esp32_ble {

/// Stand-in for ESPHome's BLE owner. The HID server never initialises the
/// stack itself; it only asks this object to advertise and reports whether the
/// stack is up, so those are the parts modelled.
class ESP32BLE {
 public:
  bool is_active() const { return this->active_; }
  void set_name(const std::string &name) { this->name_ = name; }
  void advertising_set_appearance(uint16_t appearance) { this->appearance_ = appearance; }
  void advertising_add_service_uuid(ESPBTUUID uuid) {
    (void) uuid;
    fake_ble::state().advertising_uuid_adds++;
    fake_ble::state().advertising_starts++;
  }
  void advertising_start() { fake_ble::state().advertising_starts++; }

  // --- test control --------------------------------------------------------
  void set_active(bool active) { this->active_ = active; }
  const std::string &name() const { return this->name_; }
  uint16_t appearance() const { return this->appearance_; }

 protected:
  bool active_{true};
  std::string name_;
  uint16_t appearance_{0};
};

extern ESP32BLE *global_ble;

}  // namespace esphome::esp32_ble
