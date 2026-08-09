// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "esphome/core/defines.h"

#if !defined(USE_ESP32) || !defined(USE_ESP32_FRAMEWORK_ESP_IDF)
#error "zwift_ride_hid::HidKeyboard requires ESP32 with the ESP-IDF framework"
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include <esp_gap_ble_api.h>
#include <esp_gatt_defs.h>
#include <esp_gatts_api.h>

#include "esphome/components/esp32_ble/ble.h"

namespace esphome::zwift_ride_hid {

/**
 * A small BLE HID-over-GATT keyboard transport for ESPHome's Bluedroid owner.
 *
 * This class deliberately does not initialise/deinitialise Bluedroid, NVS, or
 * the Bluetooth controller, and it never installs a process-global ESP-IDF
 * callback. The owning ESPHome component must register gap_event_handler() and
 * gatts_event_handler() with esp32_ble's callback broker and call loop().
 *
 * There is one inbound HID host slot. Outbound central-role connections (for
 * example, the Zwift Ride connection) are ignored using the GATTS link role.
 */
class HidKeyboard {
public:
  static constexpr uint16_t APP_ID = 0x5A49;
  static constexpr size_t MAX_KEYS = 6;

  void set_parent(esp32_ble::ESP32BLE *parent) { this->parent_ = parent; }

  /** Schedule the server to start. The name must contain 1..20 bytes. */
  bool begin(const std::string &name);

  /** Progress deferred app registration and a single bounded pending report. */
  void loop();

  /** Ask ESPHome's advertising owner to advertise the HID service. */
  bool advertise();

  /** Resume report output and advertising after quiesce()/stop(). */
  void resume();

  /** Release all keys, disconnect the HID host, and stop advertising. */
  void stop();

  /** Release all keys, disconnect, and suppress reports until resume(). */
  void quiesce();

  bool send_report(uint8_t modifiers, const uint8_t *key_usages,
                   size_t key_count);
  bool send_report(uint8_t modifiers, uint8_t key0 = 0, uint8_t key1 = 0,
                   uint8_t key2 = 0, uint8_t key3 = 0, uint8_t key4 = 0,
                   uint8_t key5 = 0);
  bool release_all();

  bool connected() const { return this->connected_; }
  bool ready() const;
  bool service_ready() const { return this->services_ready_; }
  bool quiesced() const { return this->quiesced_; }
  bool failed() const { return this->failed_; }
  uint8_t keyboard_leds() const { return this->keyboard_leds_; }
  uint16_t mtu() const { return this->mtu_; }

  void gap_event_handler(esp_gap_ble_cb_event_t event,
                         esp_ble_gap_cb_param_t *param);
  void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                           esp_ble_gatts_cb_param_t *param);

  /** Reset local handles before ESPHome dismantles the shared BLE stack. */
  void ble_before_disabled_event_handler();

protected:
  enum class ServiceStage : uint8_t { DIS, BAS, HID, COMPLETE };
  enum class AttributeKind : uint8_t { CHARACTERISTIC, DESCRIPTOR };

  enum class HandleSlot : uint8_t {
    NONE,
    BATTERY_VALUE,
    BATTERY_CCCD,
    PROTOCOL_MODE,
    BOOT_INPUT,
    BOOT_INPUT_CCCD,
    BOOT_OUTPUT,
    REPORT_INPUT,
    REPORT_INPUT_CCCD,
    REPORT_OUTPUT,
    HID_CONTROL_POINT,
  };

  struct AttributeSpec {
    AttributeKind kind;
    uint16_t uuid;
    esp_gatt_perm_t permissions;
    esp_gatt_char_prop_t properties;
    uint16_t max_length;
    uint16_t length;
    uint8_t *value;
    HandleSlot handle_slot;
  };

  bool try_register_();
  bool configure_security_();
  void refresh_construction_watchdog_();
  bool create_current_service_();
  bool add_battery_include_();
  bool add_next_attribute_();
  bool start_current_service_();
  bool advance_service_();
  const AttributeSpec *current_attributes_(size_t *count) const;
  uint16_t current_service_uuid_() const;
  uint16_t current_service_handle_() const;
  uint16_t current_service_handle_count_() const;
  void set_current_service_handle_(uint16_t handle);
  void set_attribute_handle_(HandleSlot slot, uint16_t handle);
  bool is_our_gatts_event_(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                           const esp_ble_gatts_cb_param_t *param) const;
  bool is_hid_peer_(const esp_bd_addr_t address) const;
  void clear_connection_();
  void reset_server_state_();
  bool send_current_report_();
  bool stop_advertising_();
  bool notifications_enabled_() const;
  uint16_t selected_input_handle_() const;
  void mark_failed_(const char *operation, esp_err_t error);

  esp32_ble::ESP32BLE *parent_{nullptr};
  std::string name_;

  esp_gatt_if_t gatts_if_{ESP_GATT_IF_NONE};
  ServiceStage service_stage_{ServiceStage::DIS};
  size_t attribute_index_{0};
  uint16_t dis_service_handle_{0};
  uint16_t bas_service_handle_{0};
  uint16_t hid_service_handle_{0};
  uint16_t battery_value_handle_{0};
  uint16_t battery_cccd_handle_{0};
  uint16_t protocol_mode_handle_{0};
  uint16_t boot_input_handle_{0};
  uint16_t boot_input_cccd_handle_{0};
  uint16_t boot_output_handle_{0};
  uint16_t report_input_handle_{0};
  uint16_t report_input_cccd_handle_{0};
  uint16_t report_output_handle_{0};
  uint16_t hid_control_point_handle_{0};

  // These member objects remain valid while asynchronous add/create requests
  // are in flight. ESP-IDF copies their pointed-to static values.
  esp_gatt_srvc_id_t pending_service_id_{};
  esp_bt_uuid_t pending_uuid_{};
  esp_attr_value_t pending_attr_value_{};
  esp_attr_control_t pending_attr_control_{ESP_GATT_AUTO_RSP};

  esp_bd_addr_t peer_address_{};
  uint16_t connection_id_{0};
  uint16_t mtu_{23};
  uint8_t protocol_mode_{1};
  uint8_t keyboard_leds_{0};
  std::array<uint8_t, 8> current_report_{};

  uint32_t next_register_attempt_ms_{0};
  uint32_t next_report_attempt_ms_{0};
  uint32_t next_advertising_attempt_ms_{0};
  uint32_t construction_deadline_ms_{0};
  bool begin_requested_{false};
  bool app_registration_requested_{false};
  bool registered_{false};
  bool services_ready_{false};
  bool advertising_uuid_added_{false};
  bool advertising_desired_{false};
  bool advertising_active_{false};
  bool advertising_start_requested_{false};
  bool advertising_stop_requested_{false};
  bool connected_{false};
  bool encrypted_{false};
  bool report_notifications_{false};
  bool boot_notifications_{false};
  bool congested_{false};
  bool suspended_{false};
  bool pending_report_{false};
  bool quiesced_{false};
  bool failed_{false};
};

} // namespace esphome::zwift_ride_hid
