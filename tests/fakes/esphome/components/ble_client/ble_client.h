// SPDX-License-Identifier: GPL-3.0-only
//
// Host stand-in for ESPHome's BLE client. RideClient only reaches into it for
// address matching, connection state, and disconnect requests, so those are
// the parts modelled faithfully; tests set the state directly.
#pragma once

#include <cstdint>
#include <cstring>

#include "esp_bt_defs.h"
#include "esp_gatt_defs.h"
#include "esp_gattc_api.h"
#include "fake_ble.h"

namespace esphome::esp32_ble_tracker {

enum class ClientState {
  INIT,
  DISCONNECTING,
  IDLE,
  SEARCHING,
  DISCOVERED,
  READY_TO_CONNECT,
  CONNECTING,
  CONNECTED,
  ESTABLISHED,
};

}  // namespace esphome::esp32_ble_tracker

namespace esphome::ble_client {

namespace espbt = esphome::esp32_ble_tracker;

class BLEClient {
 public:
  bool check_addr(esp_bd_addr_t &address) {
    return std::memcmp(address, this->remote_bda_, sizeof(esp_bd_addr_t)) == 0;
  }
  uint8_t *get_remote_bda() { return this->remote_bda_; }
  espbt::ClientState state() const { return this->state_; }
  const char *address_str() const { return "AA:BB:CC:DD:EE:FF"; }

  void disconnect() {
    fake_ble::state().client_disconnect_calls++;
    this->disconnect_requested_ = true;
  }

  // --- test control --------------------------------------------------------
  void set_state(espbt::ClientState state) { this->state_ = state; }
  void set_remote_bda(const esp_bd_addr_t address) {
    std::memcpy(this->remote_bda_, address, sizeof(esp_bd_addr_t));
  }
  bool disconnect_requested() const { return this->disconnect_requested_; }
  void clear_disconnect_requested() { this->disconnect_requested_ = false; }

 protected:
  esp_bd_addr_t remote_bda_{0xE1, 0x38, 0xED, 0x70, 0xFA, 0x9F};
  espbt::ClientState state_{espbt::ClientState::ESTABLISHED};
  bool disconnect_requested_{false};
};

}  // namespace esphome::ble_client
