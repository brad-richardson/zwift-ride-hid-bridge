// SPDX-License-Identifier: GPL-3.0-only
#include "zwift_ride_hid.h"

#include "esphome/core/log.h"

namespace esphome::zwift_ride_hid {

static const char *const TAG = "zwift_ride_hid";

void ZwiftRideHid::setup() {
  ESP_LOGW(TAG, "Milestone-0 scaffold only; Ride decoding and HID output are not implemented yet");
}

void ZwiftRideHid::dump_config() {
  ESP_LOGCONFIG(TAG, "Zwift Ride HID Bridge (scaffold):");
  ESP_LOGCONFIG(TAG, "  HID name: %s", this->hid_name_.c_str());
  ESP_LOGCONFIG(TAG, "  Mapping profile: %s", this->profile_.c_str());
  ESP_LOGCONFIG(TAG, "  Lever thresholds: press=%u, release=%u", this->press_threshold_,
                this->release_threshold_);
  ESP_LOGCONFIG(TAG, "  Expose raw lever values: %s", YESNO(this->expose_raw_));
  ESP_LOGCONFIG(TAG, "  Debug capture: %s", YESNO(this->debug_capture_));
}

void ZwiftRideHid::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                       esp_ble_gattc_cb_param_t *param) {
  // Deliberately empty in Milestone 0. Future Ride handling belongs here so
  // ESPHome remains the owner of Bluedroid and the process-global callbacks.
  (void) event;
  (void) gattc_if;
  (void) param;
}

}  // namespace esphome::zwift_ride_hid
