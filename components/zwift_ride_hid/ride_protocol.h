// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome::zwift_ride_hid {

constexpr uint8_t kRideInputCommand = 0x23;
constexpr uint32_t kKnownButtonMask = 0x00037F7FUL;
constexpr size_t kMaxRideNotificationLength = 128;
constexpr uint8_t kAnalogChannelCount = 4;

enum class RideDecodeStatus : uint8_t {
  OK = 0,
  NULL_ARGUMENT,
  EMPTY_PACKET,
  NOT_INPUT_PACKET,
  PACKET_TOO_LARGE,
  MALFORMED_PROTOBUF,
  MISSING_BUTTON_MAP,
};

struct RideInputPacket {
  uint32_t wire_button_map{0xFFFFFFFFUL};
  uint32_t pressed_buttons{0};
  int32_t analog[kAnalogChannelCount]{0, 0, 0, 0};
  uint8_t analog_present_mask{0};

  bool has_analog(uint8_t channel) const {
    return channel < kAnalogChannelCount && (this->analog_present_mask & (1U << channel)) != 0;
  }
};

// Decodes one complete BLE notification. The decoder is allocation-free and
// rejects packets larger than kMaxRideNotificationLength. `output` is only
// modified after the entire packet has passed validation.
RideDecodeStatus decode_ride_notification(const uint8_t *data, size_t length,
                                           RideInputPacket *output);

const char *ride_decode_status_name(RideDecodeStatus status);

}  // namespace esphome::zwift_ride_hid
