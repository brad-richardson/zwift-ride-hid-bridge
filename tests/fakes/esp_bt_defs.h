// SPDX-License-Identifier: GPL-3.0-only
//
// Host stand-in for ESP-IDF's Bluetooth base types. Only the declarations the
// component actually uses are present, with the same names, sizes, and
// semantics, so the production sources compile unmodified.
#pragma once

#include <cstdint>
#include <cstring>

using esp_bd_addr_t = uint8_t[6];

enum { ESP_UUID_LEN_16 = 2, ESP_UUID_LEN_32 = 4, ESP_UUID_LEN_128 = 16 };

struct esp_bt_uuid_t {
  uint16_t len;
  union {
    uint16_t uuid16;
    uint32_t uuid32;
    uint8_t uuid128[16];
  } uuid;
};

enum esp_ble_addr_type_t {
  BLE_ADDR_TYPE_PUBLIC = 0,
  BLE_ADDR_TYPE_RANDOM = 1,
  BLE_ADDR_TYPE_RPA_PUBLIC = 2,
  BLE_ADDR_TYPE_RPA_RANDOM = 3,
};

enum esp_bt_status_t {
  ESP_BT_STATUS_SUCCESS = 0,
  ESP_BT_STATUS_FAIL = 1,
};
