// SPDX-License-Identifier: GPL-3.0-only
#include "ride_protocol.h"

#include <cstddef>
#include <cstdint>

namespace esphome::zwift_ride_hid {
namespace {

constexpr uint8_t kMaxUnknownGroupDepth = 4;

struct Cursor {
  const uint8_t *current;
  const uint8_t *end;

  size_t remaining() const { return static_cast<size_t>(this->end - this->current); }
};

bool read_varint(Cursor *cursor, uint64_t *value) {
  uint64_t result = 0;
  for (uint8_t index = 0; index < 10; index++) {
    if (cursor->current == cursor->end) {
      return false;
    }

    const uint8_t byte = *cursor->current++;
    if (index == 9 && (byte & 0xFEU) != 0) {
      return false;
    }
    result |= static_cast<uint64_t>(byte & 0x7FU) << (index * 7U);
    if ((byte & 0x80U) == 0) {
      *value = result;
      return true;
    }
  }
  return false;
}

bool read_tag(Cursor *cursor, uint32_t *field_number, uint8_t *wire_type) {
  uint64_t tag = 0;
  if (!read_varint(cursor, &tag) || tag > UINT32_MAX) {
    return false;
  }

  *field_number = static_cast<uint32_t>(tag >> 3U);
  *wire_type = static_cast<uint8_t>(tag & 0x07U);
  return *field_number != 0;
}

bool read_slice(Cursor *cursor, Cursor *slice) {
  uint64_t length = 0;
  if (!read_varint(cursor, &length) || length > cursor->remaining()) {
    return false;
  }

  slice->current = cursor->current;
  slice->end = cursor->current + static_cast<size_t>(length);
  cursor->current = slice->end;
  return true;
}

bool skip_field(Cursor *cursor, uint32_t field_number, uint8_t wire_type, uint8_t depth) {
  uint64_t ignored = 0;
  switch (wire_type) {
    case 0:
      return read_varint(cursor, &ignored);
    case 1:
      if (cursor->remaining() < 8) {
        return false;
      }
      cursor->current += 8;
      return true;
    case 2: {
      Cursor ignored_slice{};
      return read_slice(cursor, &ignored_slice);
    }
    case 3: {
      if (depth >= kMaxUnknownGroupDepth) {
        return false;
      }
      while (cursor->current != cursor->end) {
        uint32_t nested_field = 0;
        uint8_t nested_wire = 0;
        if (!read_tag(cursor, &nested_field, &nested_wire)) {
          return false;
        }
        if (nested_wire == 4) {
          return nested_field == field_number;
        }
        if (!skip_field(cursor, nested_field, nested_wire, static_cast<uint8_t>(depth + 1))) {
          return false;
        }
      }
      return false;
    }
    case 4:
      return false;
    case 5:
      if (cursor->remaining() < 4) {
        return false;
      }
      cursor->current += 4;
      return true;
    default:
      return false;
  }
}

int32_t decode_zigzag32(uint32_t encoded) {
  const uint32_t decoded = (encoded >> 1U) ^ (0U - (encoded & 1U));
  return static_cast<int32_t>(decoded);
}

bool decode_analog_record(Cursor record, RideInputPacket *packet) {
  uint32_t location = 0;
  uint32_t encoded_value = 0;
  bool has_location = false;
  bool has_value = false;

  while (record.current != record.end) {
    uint32_t field_number = 0;
    uint8_t wire_type = 0;
    if (!read_tag(&record, &field_number, &wire_type)) {
      return false;
    }

    if (field_number == 1) {
      uint64_t value = 0;
      if (wire_type != 0 || !read_varint(&record, &value) || value > UINT32_MAX) {
        return false;
      }
      location = static_cast<uint32_t>(value);
      has_location = true;
    } else if (field_number == 2) {
      uint64_t value = 0;
      if (wire_type != 0 || !read_varint(&record, &value) || value > UINT32_MAX) {
        return false;
      }
      encoded_value = static_cast<uint32_t>(value);
      has_value = true;
    } else if (!skip_field(&record, field_number, wire_type, 0)) {
      return false;
    }
  }

  if (!has_location || !has_value) {
    return false;
  }

  // Future firmware may add channels. A syntactically valid unknown channel is
  // ignored rather than turning every notification into a fatal protocol error.
  if (location < kAnalogChannelCount) {
    packet->analog[location] = decode_zigzag32(encoded_value);
    packet->analog_present_mask |= static_cast<uint8_t>(1U << location);
  }
  return true;
}

bool decode_grouped_analog(Cursor group, RideInputPacket *packet) {
  while (group.current != group.end) {
    uint32_t field_number = 0;
    uint8_t wire_type = 0;
    if (!read_tag(&group, &field_number, &wire_type)) {
      return false;
    }

    if (field_number == 1) {
      Cursor record{};
      if (wire_type != 2 || !read_slice(&group, &record) || !decode_analog_record(record, packet)) {
        return false;
      }
    } else if (!skip_field(&group, field_number, wire_type, 0)) {
      return false;
    }
  }
  return true;
}

}  // namespace

RideDecodeStatus decode_ride_notification(const uint8_t *data, size_t length,
                                           RideInputPacket *output) {
  if (data == nullptr || output == nullptr) {
    return RideDecodeStatus::NULL_ARGUMENT;
  }
  if (length == 0) {
    return RideDecodeStatus::EMPTY_PACKET;
  }
  if (length > kMaxRideNotificationLength) {
    return RideDecodeStatus::PACKET_TOO_LARGE;
  }
  if (data[0] != kRideInputCommand) {
    return RideDecodeStatus::NOT_INPUT_PACKET;
  }

  RideInputPacket candidate{};
  bool has_button_map = false;
  Cursor payload{data + 1, data + length};
  while (payload.current != payload.end) {
    uint32_t field_number = 0;
    uint8_t wire_type = 0;
    if (!read_tag(&payload, &field_number, &wire_type)) {
      return RideDecodeStatus::MALFORMED_PROTOBUF;
    }

    if (field_number == 1) {
      uint64_t value = 0;
      if (wire_type != 0 || !read_varint(&payload, &value) || value > UINT32_MAX) {
        return RideDecodeStatus::MALFORMED_PROTOBUF;
      }
      candidate.wire_button_map = static_cast<uint32_t>(value);
      has_button_map = true;
    } else if (field_number == 2) {
      Cursor group{};
      if (wire_type != 2 || !read_slice(&payload, &group) ||
          !decode_grouped_analog(group, &candidate)) {
        return RideDecodeStatus::MALFORMED_PROTOBUF;
      }
    } else if (field_number == 3) {
      Cursor record{};
      if (wire_type != 2 || !read_slice(&payload, &record) ||
          !decode_analog_record(record, &candidate)) {
        return RideDecodeStatus::MALFORMED_PROTOBUF;
      }
    } else if (!skip_field(&payload, field_number, wire_type, 0)) {
      return RideDecodeStatus::MALFORMED_PROTOBUF;
    }
  }

  if (!has_button_map) {
    return RideDecodeStatus::MISSING_BUTTON_MAP;
  }

  candidate.pressed_buttons = (~candidate.wire_button_map) & kKnownButtonMask;
  *output = candidate;
  return RideDecodeStatus::OK;
}

const char *ride_decode_status_name(RideDecodeStatus status) {
  switch (status) {
    case RideDecodeStatus::OK:
      return "ok";
    case RideDecodeStatus::NULL_ARGUMENT:
      return "null_argument";
    case RideDecodeStatus::EMPTY_PACKET:
      return "empty_packet";
    case RideDecodeStatus::NOT_INPUT_PACKET:
      return "not_input_packet";
    case RideDecodeStatus::PACKET_TOO_LARGE:
      return "packet_too_large";
    case RideDecodeStatus::MALFORMED_PROTOBUF:
      return "malformed_protobuf";
    case RideDecodeStatus::MISSING_BUTTON_MAP:
      return "missing_button_map";
  }
  return "unknown";
}

}  // namespace esphome::zwift_ride_hid
