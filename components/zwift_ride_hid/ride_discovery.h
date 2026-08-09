// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome::zwift_ride_hid {

// Bluetooth SIG assigned numbers identify both values as Zwift-owned. The
// manufacturer payload's first byte distinguishes the otherwise identically
// named Ride controllers: 7 is Right and 8 is Left.
static constexpr uint16_t kZwiftRideServiceUuid = 0xFC82;
static constexpr uint16_t kZwiftCompanyId = 0x094A;
static constexpr uint8_t kZwiftRideLeftDeviceId = 8;
static constexpr size_t kZwiftRideManufacturerPayloadLength = 3;

constexpr bool is_zwift_ride_left_manufacturer_data(uint16_t company_id,
                                                     const uint8_t *payload,
                                                     size_t payload_length) {
  return company_id == kZwiftCompanyId && payload != nullptr &&
         payload_length >= 1 && payload[0] == kZwiftRideLeftDeviceId;
}

constexpr bool is_zwift_ride_left_advertisement(bool advertises_ride_service,
                                                uint16_t company_id,
                                                const uint8_t *payload,
                                                size_t payload_length) {
  return advertises_ride_service &&
         is_zwift_ride_left_manufacturer_data(company_id, payload,
                                              payload_length);
}

}  // namespace esphome::zwift_ride_hid
