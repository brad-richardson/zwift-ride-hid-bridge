// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/esp32_ble/ble.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"

#ifdef USE_OTA_STATE_LISTENER
#include "esphome/components/ota/ota_backend.h"
#endif

#include "hid_keyboard.h"
#include "input_state.h"
#include "keymap.h"
#include "ride_client.h"
#include "ride_discovery.h"
#include "ride_protocol.h"

namespace esphome::zwift_ride_hid {

class ZwiftRideHid : public Component,
                     public ble_client::BLEClientNode,
                     public esp32_ble_tracker::ESPBTDeviceListener,
                     public esp32_ble_tracker::BLEScannerStateListener,
                     public RideClientListener
#ifdef USE_OTA_STATE_LISTENER
    ,
                     public ota::OTAGlobalStateListener
#endif
{
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;
  void on_shutdown() override;
  void on_safe_shutdown() override;

  bool parse_device(const esp32_ble_tracker::ESPBTDevice &device) override;
  void on_scanner_state(esp32_ble_tracker::ScannerState state) override;

  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;
  void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) override;
  void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                           esp_ble_gatts_cb_param_t *param);
  void ble_before_disabled_event_handler();

#ifdef USE_OTA_STATE_LISTENER
  void on_ota_global_state(ota::OTAState state, float progress, uint8_t error,
                           ota::OTAComponent *component) override;
#endif

  void on_ride_ready() override;
  void on_ride_disconnected() override;
  void on_ride_notification(const uint8_t *data, uint16_t length) override;
  void on_ride_sync_response(const uint8_t *data, uint16_t length) override;

  void set_ble_parent(esp32_ble::ESP32BLE *parent) { this->ble_parent_ = parent; }
  void set_ble_tracker(esp32_ble_tracker::ESP32BLETracker *tracker) {
    this->ble_tracker_ = tracker;
  }
  void set_hid_name(const std::string &hid_name) { this->hid_name_ = hid_name; }
  void set_profile(const std::string &profile) { this->profile_name_ = profile; }
  void set_press_threshold(uint8_t threshold) { this->press_threshold_ = threshold; }
  void set_release_threshold(uint8_t threshold) { this->release_threshold_ = threshold; }
  void set_expose_raw(bool expose_raw) { this->expose_raw_ = expose_raw; }
  void set_connect_haptic(bool enabled) { this->connect_haptic_ = enabled; }
  void set_button_haptic(bool enabled) { this->button_haptic_ = enabled; }
  void set_debug_capture(bool debug_capture) { this->debug_capture_ = debug_capture; }
  void set_status_led(GPIOPin *pin) { this->status_led_ = pin; }

  void set_ride_connected_sensor(binary_sensor::BinarySensor *sensor) {
    this->ride_connected_sensor_ = sensor;
  }
  void set_hid_connected_sensor(binary_sensor::BinarySensor *sensor) {
    this->hid_connected_sensor_ = sensor;
  }
  void set_ready_sensor(binary_sensor::BinarySensor *sensor) { this->ready_sensor_ = sensor; }
  void set_state_text_sensor(text_sensor::TextSensor *sensor) { this->state_text_sensor_ = sensor; }
  void set_reconnect_count_sensor(sensor::Sensor *sensor) { this->reconnect_count_sensor_ = sensor; }
  void set_invalid_frame_count_sensor(sensor::Sensor *sensor) {
    this->invalid_frame_count_sensor_ = sensor;
  }
  void set_hid_report_count_sensor(sensor::Sensor *sensor) { this->hid_report_count_sensor_ = sensor; }
  void set_left_lever_sensor(sensor::Sensor *sensor) { this->left_lever_sensor_ = sensor; }
  void set_right_lever_sensor(sensor::Sensor *sensor) { this->right_lever_sensor_ = sensor; }

 protected:
  enum class BridgeState : uint8_t {
    STARTING,
    SCANNING,
    RIDE_READY,
    HID_READY,
    READY,
    OTA,
    ERROR,
    STOPPED,
  };

  BridgeState bridge_state_() const;
  static const char *bridge_state_name_(BridgeState state);
  void queue_current_report_();
  void release_all_(const char *reason);
  void publish_diagnostics_(bool force = false);
  void update_status_led_();
  void log_capture_(const uint8_t *data, size_t length, const RideInputPacket &packet) const;
  bool submit_pending_report_();
  void quiesce_(const char *reason, bool disconnect_ride);
  void update_scanner_policy_();
  bool scanner_wanted_();

  esp32_ble::ESP32BLE *ble_parent_{nullptr};
  esp32_ble_tracker::ESP32BLETracker *ble_tracker_{nullptr};
  RideClient ride_client_{};
  HidKeyboard hid_keyboard_{};
  InputState input_state_{};
  const Keymap *keymap_{nullptr};

  std::string hid_name_{"Zwift Ride KB"};
  std::string profile_name_{"delta_emulator"};
  uint8_t press_threshold_{35};
  uint8_t release_threshold_{20};
  bool expose_raw_{false};
  bool connect_haptic_{true};
  bool button_haptic_{false};
  bool debug_capture_{false};

  KeyboardReport pending_report_{};
  bool report_pending_{false};
  bool connect_haptic_pending_{false};
  bool button_haptic_pending_{false};
  bool ride_ready_{false};
  bool ride_session_quiesced_{false};
  bool hid_ready_last_{false};
  bool ever_ride_ready_{false};
  bool ota_active_{false};
  bool stopped_{false};
  bool diagnostics_dirty_{true};
  bool auto_discover_ride_{false};
  bool ride_address_selected_{false};
  uint64_t selected_ride_address_{0};
  uint32_t reconnect_count_{0};
  uint32_t invalid_frame_count_{0};
  uint32_t hid_report_count_{0};
  uint32_t last_haptic_ms_{0};
  uint32_t last_diagnostics_ms_{0};
  BridgeState published_state_{BridgeState::STARTING};

  GPIOPin *status_led_{nullptr};
  binary_sensor::BinarySensor *ride_connected_sensor_{nullptr};
  binary_sensor::BinarySensor *hid_connected_sensor_{nullptr};
  binary_sensor::BinarySensor *ready_sensor_{nullptr};
  text_sensor::TextSensor *state_text_sensor_{nullptr};
  sensor::Sensor *reconnect_count_sensor_{nullptr};
  sensor::Sensor *invalid_frame_count_sensor_{nullptr};
  sensor::Sensor *hid_report_count_sensor_{nullptr};
  sensor::Sensor *left_lever_sensor_{nullptr};
  sensor::Sensor *right_lever_sensor_{nullptr};
};

}  // namespace esphome::zwift_ride_hid
