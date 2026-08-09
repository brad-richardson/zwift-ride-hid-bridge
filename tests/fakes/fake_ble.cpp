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
