// SPDX-License-Identifier: GPL-3.0-only
#include "fake_ble.h"

#include <cstring>
#include <string>

#include "esp_err.h"
#include "esp_gattc_api.h"

namespace fake_ble {
namespace {
State g_state;
}  // namespace

State &state() { return g_state; }
void reset() { g_state = State{}; }
void advance(uint32_t milliseconds) { g_state.now_ms += milliseconds; }

}  // namespace fake_ble

const char *esp_err_to_name(esp_err_t error) {
  return error == ESP_OK ? "ESP_OK" : "ESP_FAIL";
}

namespace {

// The component looks characteristics up by 128-bit UUID. Only the first byte
// of the Zwift UUIDs differs, which is enough to tell them apart here.
const fake_ble::DiscoveredCharacteristic *lookup(const esp_bt_uuid_t &uuid) {
  auto &s = fake_ble::state();
  if (uuid.len != ESP_UUID_LEN_128)
    return nullptr;
  // ESPBTUUID::from_raw parses "00000002-..." into little-endian bytes, so the
  // discriminating nibble lands in the last byte.
  switch (uuid.uuid.uuid128[12]) {
    case 0x02:
      return &s.async_char;
    case 0x03:
      return &s.sync_rx_char;
    case 0x04:
      return &s.sync_tx_char;
    default:
      return nullptr;
  }
}

}  // namespace

esp_gatt_status_t esp_ble_gattc_get_service(esp_gatt_if_t, uint16_t, esp_bt_uuid_t *,
                                            esp_gattc_service_elem_t *result, uint16_t *count,
                                            uint16_t) {
  auto &s = fake_ble::state();
  if (!s.service_present) {
    *count = 0;
    return ESP_GATT_NOT_FOUND;
  }
  result->start_handle = s.service_start;
  result->end_handle = s.service_end;
  result->is_primary = true;
  *count = 1;
  return ESP_GATT_OK;
}

esp_gatt_status_t esp_ble_gattc_get_char_by_uuid(esp_gatt_if_t, uint16_t, uint16_t, uint16_t,
                                                 esp_bt_uuid_t char_uuid,
                                                 esp_gattc_char_elem_t *result, uint16_t *count) {
  const auto *entry = lookup(char_uuid);
  if (entry == nullptr || !entry->present) {
    *count = 0;
    return ESP_GATT_NOT_FOUND;
  }
  result->char_handle = entry->handle;
  result->properties = entry->properties;
  *count = 1;
  return ESP_GATT_OK;
}

esp_gatt_status_t esp_ble_gattc_get_descr_by_char_handle(esp_gatt_if_t, uint16_t,
                                                         uint16_t char_handle, esp_bt_uuid_t,
                                                         esp_gattc_descr_elem_t *result,
                                                         uint16_t *count) {
  auto &s = fake_ble::state();
  for (const auto *entry : {&s.async_char, &s.sync_rx_char, &s.sync_tx_char}) {
    if (entry->handle != char_handle)
      continue;
    if (!entry->cccd_present) {
      *count = 0;
      return ESP_GATT_NOT_FOUND;
    }
    result->handle = entry->cccd_handle;
    *count = 1;
    return ESP_GATT_OK;
  }
  *count = 0;
  return ESP_GATT_NOT_FOUND;
}

esp_err_t esp_ble_gattc_write_char(esp_gatt_if_t, uint16_t, uint16_t handle, uint16_t value_len,
                                   uint8_t *value, esp_gatt_write_type_t write_type,
                                   esp_gatt_auth_req_t) {
  auto &s = fake_ble::state();
  if (s.write_char_result != ESP_OK)
    return s.write_char_result;
  s.gattc_writes.push_back({handle, std::vector<uint8_t>(value, value + value_len), write_type});
  return ESP_OK;
}

esp_err_t esp_ble_gattc_register_for_notify(esp_gatt_if_t, esp_bd_addr_t, uint16_t handle) {
  auto &s = fake_ble::state();
  if (s.register_for_notify_result != ESP_OK)
    return s.register_for_notify_result;
  s.registered_notify_handles.push_back(handle);
  return ESP_OK;
}

// --- GATT server + GAP fakes ------------------------------------------------

#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esphome/components/esp32_ble/ble.h"

namespace esphome::esp32_ble {
ESP32BLE *global_ble = nullptr;
}  // namespace esphome::esp32_ble

esp_err_t esp_ble_gatts_app_register(uint16_t) {
  auto &s = fake_ble::state();
  if (s.gatts_app_register_result != ESP_OK)
    return s.gatts_app_register_result;
  s.app_registers++;
  return ESP_OK;
}

esp_err_t esp_ble_gatts_create_service(esp_gatt_if_t, esp_gatt_srvc_id_t *, uint16_t) {
  auto &s = fake_ble::state();
  if (s.gatts_create_service_result != ESP_OK)
    return s.gatts_create_service_result;
  s.services_created++;
  return ESP_OK;
}

esp_err_t esp_ble_gatts_add_included_service(uint16_t, uint16_t) {
  fake_ble::state().included_services++;
  return ESP_OK;
}

esp_err_t esp_ble_gatts_add_char(uint16_t, esp_bt_uuid_t *, esp_gatt_perm_t,
                                 esp_gatt_char_prop_t, esp_attr_value_t *,
                                 esp_attr_control_t *) {
  auto &s = fake_ble::state();
  if (s.gatts_add_char_result != ESP_OK)
    return s.gatts_add_char_result;
  s.characteristics_added++;
  return ESP_OK;
}

esp_err_t esp_ble_gatts_add_char_descr(uint16_t, esp_bt_uuid_t *, esp_gatt_perm_t,
                                       esp_attr_value_t *, esp_attr_control_t *) {
  fake_ble::state().descriptors_added++;
  return ESP_OK;
}

esp_err_t esp_ble_gatts_start_service(uint16_t) {
  auto &s = fake_ble::state();
  if (s.gatts_start_service_result != ESP_OK)
    return s.gatts_start_service_result;
  s.services_started++;
  return ESP_OK;
}

esp_err_t esp_ble_gatts_send_indicate(esp_gatt_if_t, uint16_t, uint16_t, uint16_t value_len,
                                      uint8_t *value, bool) {
  auto &s = fake_ble::state();
  if (s.gatts_send_indicate_result != ESP_OK)
    return s.gatts_send_indicate_result;
  s.hid_reports.emplace_back(value, value + value_len);
  return ESP_OK;
}

esp_err_t esp_ble_gatts_set_attr_value(uint16_t attr_handle, uint16_t length,
                                       const uint8_t *value) {
  fake_ble::state().attribute_writes.emplace_back(
      attr_handle, std::vector<uint8_t>(value, value + length));
  return ESP_OK;
}

esp_err_t esp_ble_gap_set_device_name(const char *name) {
  fake_ble::state().device_name = name;
  return ESP_OK;
}

esp_err_t esp_ble_gap_set_security_param(esp_ble_sm_param_t, void *, uint8_t) { return ESP_OK; }

esp_err_t esp_ble_gap_disconnect(esp_bd_addr_t) {
  fake_ble::state().peer_disconnects++;
  return ESP_OK;
}

esp_err_t esp_ble_gap_stop_advertising() {
  auto &s = fake_ble::state();
  if (s.gap_stop_advertising_result != ESP_OK)
    return s.gap_stop_advertising_result;
  s.advertising_stops++;
  return ESP_OK;
}

esp_err_t esp_ble_gap_security_rsp(esp_bd_addr_t, bool) {
  fake_ble::state().security_responses++;
  return ESP_OK;
}

esp_err_t esp_ble_passkey_reply(esp_bd_addr_t, bool, uint32_t) { return ESP_OK; }
esp_err_t esp_ble_confirm_reply(esp_bd_addr_t, bool) { return ESP_OK; }

esp_err_t esp_ble_set_encryption(esp_bd_addr_t, esp_ble_sec_act_t) {
  fake_ble::state().encryption_requests++;
  return ESP_OK;
}
