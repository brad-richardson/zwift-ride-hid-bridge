// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <cstring>
#include <string>

#include "esp_bt_defs.h"

namespace esphome::esp32_ble {

/// Enough of ESPHome's UUID helper for the Ride client's lookups.
class ESPBTUUID {
 public:
  static ESPBTUUID from_uint16(uint16_t value) {
    ESPBTUUID result;
    result.uuid_.len = ESP_UUID_LEN_16;
    result.uuid_.uuid.uuid16 = value;
    return result;
  }

  /// Parses "00000002-19CA-..." the way ESPHome does: little-endian bytes, so
  /// the leading group's low byte lands at index 12.
  static ESPBTUUID from_raw(const char *text) {
    ESPBTUUID result;
    result.uuid_.len = ESP_UUID_LEN_128;
    std::string hex;
    for (const char *c = text; *c != '\0'; c++) {
      if (*c != '-')
        hex.push_back(*c);
    }
    for (size_t i = 0; i < 16 && (i * 2 + 1) < hex.size(); i++) {
      const std::string byte = hex.substr(i * 2, 2);
      result.uuid_.uuid.uuid128[15 - i] =
          static_cast<uint8_t>(std::stoul(byte, nullptr, 16));
    }
    return result;
  }

  const esp_bt_uuid_t &get_uuid() const { return this->uuid_; }
  bool operator==(const ESPBTUUID &other) const {
    return std::memcmp(&this->uuid_, &other.uuid_, sizeof(esp_bt_uuid_t)) == 0;
  }

 protected:
  esp_bt_uuid_t uuid_{};
};

}  // namespace esphome::esp32_ble
