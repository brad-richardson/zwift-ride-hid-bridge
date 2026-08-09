// SPDX-License-Identifier: GPL-3.0-only
//
// Test-visible state behind the ESP-IDF fakes: what the component asked the
// stack to do, and what the stack should answer. Tests drive the component by
// setting up responses, calling its event handlers, and then asserting on the
// recorded calls.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "esp_err.h"
#include "esp_gatt_defs.h"

namespace fake_ble {

struct GattcWrite {
  uint16_t handle;
  std::vector<uint8_t> value;
  esp_gatt_write_type_t write_type;
};

struct DiscoveredCharacteristic {
  uint16_t handle;
  esp_gatt_char_prop_t properties;
  bool present{true};
  uint16_t cccd_handle{0};
  bool cccd_present{true};
};

/// Everything the fakes record or answer with. Reset between tests.
struct State {
  // --- responses the fakes give -------------------------------------------
  bool service_present{true};
  uint16_t service_start{0x0010};
  uint16_t service_end{0x0030};
  /// Keyed by the 128-bit characteristic UUID string the component asks for.
  DiscoveredCharacteristic async_char{0x0012, ESP_GATT_CHAR_PROP_BIT_NOTIFY, true, 0x0013, true};
  DiscoveredCharacteristic sync_rx_char{0x0015, ESP_GATT_CHAR_PROP_BIT_WRITE_NR, true, 0, false};
  DiscoveredCharacteristic sync_tx_char{0x0018, ESP_GATT_CHAR_PROP_BIT_NOTIFY, true, 0x0019, true};
  esp_err_t write_char_result{ESP_OK};
  esp_err_t register_for_notify_result{ESP_OK};

  // --- what the component did (GATT client) --------------------------------
  std::vector<GattcWrite> gattc_writes;
  std::vector<uint16_t> registered_notify_handles;
  uint32_t client_disconnect_calls{0};

  // --- HID server: what the component asked the stack to do ----------------
  esp_err_t gatts_app_register_result{ESP_OK};
  esp_err_t gatts_create_service_result{ESP_OK};
  esp_err_t gatts_add_char_result{ESP_OK};
  esp_err_t gatts_start_service_result{ESP_OK};
  esp_err_t gatts_send_indicate_result{ESP_OK};
  esp_err_t gap_stop_advertising_result{ESP_OK};

  uint32_t app_registers{0};
  uint32_t services_created{0};
  uint32_t services_started{0};
  uint32_t included_services{0};
  uint32_t characteristics_added{0};
  uint32_t descriptors_added{0};
  uint32_t advertising_uuid_adds{0};
  uint32_t advertising_starts{0};
  uint32_t advertising_stops{0};
  uint32_t peer_disconnects{0};
  uint32_t encryption_requests{0};
  uint32_t security_responses{0};
  std::string device_name;

  /// Every notification the HID keyboard pushed to its host, in order.
  std::vector<std::vector<uint8_t>> hid_reports;
  /// Latest value written into each attribute handle.
  std::vector<std::pair<uint16_t, std::vector<uint8_t>>> attribute_writes;

  // --- controllable clock --------------------------------------------------
  uint32_t now_ms{0};
};

State &state();
void reset();

/// Advance the fake millis() clock.
void advance(uint32_t milliseconds);

}  // namespace fake_ble
