// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <string>

#include "esphome/components/ble_client/ble_client.h"
#include "esphome/core/component.h"

namespace esphome::zwift_ride_hid {

class ZwiftRideHid : public Component, public ble_client::BLEClientNode {
 public:
  void setup() override;
  void dump_config() override;
  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;

  void set_hid_name(const std::string &hid_name) { this->hid_name_ = hid_name; }
  void set_profile(const std::string &profile) { this->profile_ = profile; }
  void set_press_threshold(uint8_t threshold) { this->press_threshold_ = threshold; }
  void set_release_threshold(uint8_t threshold) { this->release_threshold_ = threshold; }
  void set_expose_raw(bool expose_raw) { this->expose_raw_ = expose_raw; }
  void set_debug_capture(bool debug_capture) { this->debug_capture_ = debug_capture; }

 protected:
  std::string hid_name_{"Zwift Ride KB"};
  std::string profile_{"delta_emulator"};
  uint8_t press_threshold_{35};
  uint8_t release_threshold_{20};
  bool expose_raw_{false};
  bool debug_capture_{false};
};

}  // namespace esphome::zwift_ride_hid
