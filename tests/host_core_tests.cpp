// SPDX-License-Identifier: GPL-3.0-only
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "components/zwift_ride_hid/input_state.h"
#include "components/zwift_ride_hid/keymap.h"
#include "components/zwift_ride_hid/ride_protocol.h"

namespace bridge = esphome::zwift_ride_hid;

namespace {

int failures = 0;
int tests_run = 0;

#define EXPECT_TRUE(condition)                                                                     \
  do {                                                                                             \
    if (!(condition)) {                                                                            \
      std::cerr << __FILE__ << ':' << __LINE__ << ": expected true: " #condition << '\n';        \
      failures++;                                                                                  \
    }                                                                                              \
  } while (false)

#define EXPECT_FALSE(condition) EXPECT_TRUE(!(condition))

#define EXPECT_EQ(expected, actual)                                                                \
  do {                                                                                             \
    const auto expected_value = (expected);                                                        \
    const auto actual_value = (actual);                                                            \
    if (!(expected_value == actual_value)) {                                                       \
      std::cerr << __FILE__ << ':' << __LINE__ << ": values differ: " #expected " != " #actual \
                << '\n';                                                                           \
      failures++;                                                                                  \
    }                                                                                              \
  } while (false)

void append_varint(std::vector<uint8_t> *bytes, uint64_t value) {
  do {
    uint8_t next = static_cast<uint8_t>(value & 0x7FU);
    value >>= 7U;
    if (value != 0) {
      next |= 0x80U;
    }
    bytes->push_back(next);
  } while (value != 0);
}

void append_tag(std::vector<uint8_t> *bytes, uint32_t field, uint8_t wire_type) {
  append_varint(bytes, (static_cast<uint64_t>(field) << 3U) | wire_type);
}

uint32_t encode_zigzag32(int32_t value) {
  const uint32_t bits = static_cast<uint32_t>(value);
  return (bits << 1U) ^ (0U - (bits >> 31U));
}

std::vector<uint8_t> analog_record(uint32_t location, int32_t value,
                                   bool add_unknown_field = false) {
  std::vector<uint8_t> record;
  append_tag(&record, 1, 0);
  append_varint(&record, location);
  if (add_unknown_field) {
    append_tag(&record, 7, 5);
    record.insert(record.end(), {1, 2, 3, 4});
  }
  append_tag(&record, 2, 0);
  append_varint(&record, encode_zigzag32(value));
  return record;
}

std::vector<uint8_t> packet_prefix(uint32_t pressed_buttons) {
  std::vector<uint8_t> packet{bridge::kRideInputCommand};
  append_tag(&packet, 1, 0);
  append_varint(&packet, static_cast<uint32_t>(~pressed_buttons));
  return packet;
}

std::vector<uint8_t> direct_packet(
    uint32_t pressed_buttons,
    std::initializer_list<std::pair<uint32_t, int32_t>> analog_values) {
  auto packet = packet_prefix(pressed_buttons);
  for (const auto &entry : analog_values) {
    const auto record = analog_record(entry.first, entry.second);
    append_tag(&packet, 3, 2);
    append_varint(&packet, record.size());
    packet.insert(packet.end(), record.begin(), record.end());
  }
  return packet;
}

std::vector<uint8_t> grouped_packet(
    uint32_t pressed_buttons,
    std::initializer_list<std::pair<uint32_t, int32_t>> analog_values) {
  auto packet = packet_prefix(pressed_buttons);
  std::vector<uint8_t> group;
  for (const auto &entry : analog_values) {
    const auto record = analog_record(entry.first, entry.second, true);
    append_tag(&group, 1, 2);
    append_varint(&group, record.size());
    group.insert(group.end(), record.begin(), record.end());
  }
  append_tag(&packet, 2, 2);
  append_varint(&packet, group.size());
  packet.insert(packet.end(), group.begin(), group.end());
  return packet;
}

bridge::RideInputPacket semantic_packet(uint32_t pressed_buttons) {
  bridge::RideInputPacket packet{};
  packet.pressed_buttons = pressed_buttons & bridge::kKnownButtonMask;
  packet.wire_button_map = ~pressed_buttons;
  return packet;
}

void set_analog(bridge::RideInputPacket *packet, uint8_t channel, int32_t value) {
  packet->analog[channel] = value;
  packet->analog_present_mask |= static_cast<uint8_t>(1U << channel);
}

bool report_is_empty(const bridge::KeyboardReport &report) {
  if (report.modifiers != 0 || report.reserved != 0) {
    return false;
  }
  for (uint8_t key : report.keys) {
    if (key != bridge::hid_usage::NONE) {
      return false;
    }
  }
  return true;
}

void test_direct_layout_and_inverse_mask() {
  const uint32_t pressed = 0x000001UL | 0x000010UL | 0x004000UL;
  auto packet_bytes = direct_packet(pressed, {{0, -42}, {1, 55}, {2, -100}, {3, 100}});

  bridge::RideInputPacket packet{};
  EXPECT_EQ(bridge::RideDecodeStatus::OK,
            bridge::decode_ride_notification(packet_bytes.data(), packet_bytes.size(), &packet));
  EXPECT_EQ(pressed, packet.pressed_buttons);
  EXPECT_EQ(static_cast<uint32_t>(~pressed), packet.wire_button_map);
  EXPECT_EQ(0x0FU, packet.analog_present_mask);
  EXPECT_EQ(-42, packet.analog[0]);
  EXPECT_EQ(55, packet.analog[1]);
  EXPECT_EQ(-100, packet.analog[2]);
  EXPECT_EQ(100, packet.analog[3]);

  // Reserved bit 7 is active-low too, but must never become a semantic press.
  auto reserved = packet_prefix(0x000010UL | 0x000080UL);
  EXPECT_EQ(bridge::RideDecodeStatus::OK,
            bridge::decode_ride_notification(reserved.data(), reserved.size(), &packet));
  EXPECT_EQ(0x000010UL, packet.pressed_buttons);
}

void test_grouped_layout_unknown_fields_and_zigzag_extremes() {
  const uint32_t pressed = 0x000020UL | 0x020000UL;
  auto packet_bytes = grouped_packet(pressed, {{0, INT32_MIN}, {1, INT32_MAX}, {4, 77}});

  // Unknown fixed32 and a bounded unknown protobuf group must be skipped.
  append_tag(&packet_bytes, 9, 5);
  packet_bytes.insert(packet_bytes.end(), {9, 8, 7, 6});
  append_tag(&packet_bytes, 10, 3);
  append_tag(&packet_bytes, 1, 0);
  append_varint(&packet_bytes, 123);
  append_tag(&packet_bytes, 10, 4);

  bridge::RideInputPacket packet{};
  EXPECT_EQ(bridge::RideDecodeStatus::OK,
            bridge::decode_ride_notification(packet_bytes.data(), packet_bytes.size(), &packet));
  EXPECT_EQ(pressed, packet.pressed_buttons);
  EXPECT_EQ(0x03U, packet.analog_present_mask);
  EXPECT_EQ(INT32_MIN, packet.analog[0]);
  EXPECT_EQ(INT32_MAX, packet.analog[1]);
}

void test_decoder_rejects_bad_input_transactionally() {
  bridge::RideInputPacket output{};
  output.wire_button_map = 0x12345678UL;
  output.pressed_buttons = 0x87654321UL;
  output.analog[0] = 999;
  output.analog_present_mask = 0x0FU;

  const auto verify_unchanged = [&output]() {
    EXPECT_EQ(0x12345678UL, output.wire_button_map);
    EXPECT_EQ(0x87654321UL, output.pressed_buttons);
    EXPECT_EQ(999, output.analog[0]);
    EXPECT_EQ(0x0FU, output.analog_present_mask);
  };

  EXPECT_EQ(bridge::RideDecodeStatus::NULL_ARGUMENT,
            bridge::decode_ride_notification(nullptr, 1, &output));
  EXPECT_EQ(bridge::RideDecodeStatus::NULL_ARGUMENT,
            bridge::decode_ride_notification(reinterpret_cast<const uint8_t *>("#"), 1, nullptr));
  EXPECT_EQ(bridge::RideDecodeStatus::EMPTY_PACKET,
            bridge::decode_ride_notification(reinterpret_cast<const uint8_t *>("#"), 0, &output));
  const uint8_t other_command[] = {0x22};
  EXPECT_EQ(bridge::RideDecodeStatus::NOT_INPUT_PACKET,
            bridge::decode_ride_notification(other_command, sizeof(other_command), &output));

  const uint8_t truncated_varint[] = {0x23, 0x08, 0x80};
  EXPECT_EQ(bridge::RideDecodeStatus::MALFORMED_PROTOBUF,
            bridge::decode_ride_notification(truncated_varint, sizeof(truncated_varint), &output));

  const uint8_t truncated_record[] = {0x23, 0x08, 0x00, 0x1A, 0x04, 0x08, 0x00, 0x10};
  EXPECT_EQ(bridge::RideDecodeStatus::MALFORMED_PROTOBUF,
            bridge::decode_ride_notification(truncated_record, sizeof(truncated_record), &output));

  const uint8_t record_missing_value[] = {0x23, 0x08, 0x00, 0x1A, 0x02, 0x08, 0x00};
  EXPECT_EQ(bridge::RideDecodeStatus::MALFORMED_PROTOBUF,
            bridge::decode_ride_notification(record_missing_value, sizeof(record_missing_value),
                                             &output));

  const uint8_t bad_known_wire_type[] = {0x23, 0x0D, 0, 0, 0, 0};
  EXPECT_EQ(bridge::RideDecodeStatus::MALFORMED_PROTOBUF,
            bridge::decode_ride_notification(bad_known_wire_type, sizeof(bad_known_wire_type),
                                             &output));

  const uint8_t missing_buttons[] = {0x23, 0x20, 0x01};
  EXPECT_EQ(bridge::RideDecodeStatus::MISSING_BUTTON_MAP,
            bridge::decode_ride_notification(missing_buttons, sizeof(missing_buttons), &output));

  std::vector<uint8_t> huge(bridge::kMaxRideNotificationLength + 1, 0);
  huge[0] = bridge::kRideInputCommand;
  EXPECT_EQ(bridge::RideDecodeStatus::PACKET_TOO_LARGE,
            bridge::decode_ride_notification(huge.data(), huge.size(), &output));

  auto oversized_button = std::vector<uint8_t>{0x23, 0x08};
  append_varint(&oversized_button, UINT64_C(1) << 32U);
  EXPECT_EQ(bridge::RideDecodeStatus::MALFORMED_PROTOBUF,
            bridge::decode_ride_notification(oversized_button.data(), oversized_button.size(),
                                             &output));
  verify_unchanged();

  EXPECT_EQ(std::string("malformed_protobuf"),
            std::string(bridge::ride_decode_status_name(
                bridge::RideDecodeStatus::MALFORMED_PROTOBUF)));
}

void test_button_edges_chords_and_release_all() {
  bridge::InputState state;
  auto packet = semantic_packet(0x000010UL | 0x000020UL);
  auto edges = state.apply(packet);
  EXPECT_EQ(bridge::action_mask(bridge::InputAction::BUTTON_A) |
                bridge::action_mask(bridge::InputAction::BUTTON_B),
            edges.pressed);
  EXPECT_EQ(0U, edges.released);
  EXPECT_TRUE(state.active(bridge::InputAction::BUTTON_A));
  EXPECT_TRUE(state.active(bridge::InputAction::BUTTON_B));

  edges = state.apply(packet);
  EXPECT_FALSE(edges.changed());

  packet = semantic_packet(0x000020UL | 0x000002UL);
  edges = state.apply(packet);
  EXPECT_EQ(bridge::action_mask(bridge::InputAction::DPAD_UP), edges.pressed);
  EXPECT_EQ(bridge::action_mask(bridge::InputAction::BUTTON_A), edges.released);
  EXPECT_TRUE(state.active(bridge::InputAction::BUTTON_B));
  EXPECT_TRUE(state.active(bridge::InputAction::DPAD_UP));

  edges = state.release_all();
  EXPECT_EQ(0U, edges.pressed);
  EXPECT_EQ(bridge::action_mask(bridge::InputAction::BUTTON_B) |
                bridge::action_mask(bridge::InputAction::DPAD_UP),
            edges.released);
  EXPECT_EQ(0U, state.active_actions());
  EXPECT_FALSE(state.has_analog(0));
}

void test_every_documented_button_bit_independently() {
  struct ExpectedButton {
    uint32_t ride_mask;
    bridge::InputAction action;
  };

  const ExpectedButton expected_buttons[] = {
      {0x000001UL, bridge::InputAction::DPAD_LEFT},
      {0x000002UL, bridge::InputAction::DPAD_UP},
      {0x000004UL, bridge::InputAction::DPAD_RIGHT},
      {0x000008UL, bridge::InputAction::DPAD_DOWN},
      {0x000010UL, bridge::InputAction::BUTTON_A},
      {0x000020UL, bridge::InputAction::BUTTON_B},
      {0x000040UL, bridge::InputAction::BUTTON_Y},
      {0x000100UL, bridge::InputAction::BUTTON_Z},
      {0x000200UL, bridge::InputAction::LEFT_SIDE_UPPER},
      {0x000400UL, bridge::InputAction::LEFT_SIDE_MIDDLE},
      {0x000800UL, bridge::InputAction::LEFT_SIDE_LOWER},
      {0x001000UL, bridge::InputAction::LEFT_POWER},
      {0x002000UL, bridge::InputAction::RIGHT_SIDE_UPPER},
      {0x004000UL, bridge::InputAction::RIGHT_SIDE_MIDDLE},
      {0x010000UL, bridge::InputAction::RIGHT_SIDE_LOWER},
      {0x020000UL, bridge::InputAction::RIGHT_POWER},
  };

  uint32_t accumulated_known_mask = 0;
  for (const auto &expected : expected_buttons) {
    accumulated_known_mask |= expected.ride_mask;
    bridge::InputState state;
    bridge::RideInputPacket decoded{};

    // Idle is the all-ones inverse bitmap and must not synthesize an action.
    const auto idle = packet_prefix(0);
    EXPECT_EQ(bridge::RideDecodeStatus::OK,
              bridge::decode_ride_notification(idle.data(), idle.size(), &decoded));
    EXPECT_EQ(UINT32_MAX, decoded.wire_button_map);
    EXPECT_EQ(0U, decoded.pressed_buttons);
    EXPECT_FALSE(state.apply(decoded).changed());
    EXPECT_EQ(0U, state.active_actions());

    const auto pressed = packet_prefix(expected.ride_mask);
    EXPECT_EQ(bridge::RideDecodeStatus::OK,
              bridge::decode_ride_notification(pressed.data(), pressed.size(), &decoded));
    EXPECT_EQ(expected.ride_mask, decoded.pressed_buttons);
    auto edges = state.apply(decoded);
    EXPECT_EQ(bridge::action_mask(expected.action), edges.pressed);
    EXPECT_EQ(0U, edges.released);
    EXPECT_EQ(bridge::action_mask(expected.action), state.active_actions());

    // Returning to an all-ones snapshot releases exactly the one action.
    EXPECT_EQ(bridge::RideDecodeStatus::OK,
              bridge::decode_ride_notification(idle.data(), idle.size(), &decoded));
    edges = state.apply(decoded);
    EXPECT_EQ(0U, edges.pressed);
    EXPECT_EQ(bridge::action_mask(expected.action), edges.released);
    EXPECT_EQ(0U, state.active_actions());
  }
  EXPECT_EQ(bridge::kKnownButtonMask, accumulated_known_mask);
}

void test_mixed_chord_partial_releases() {
  bridge::InputState state;
  const uint32_t first_ride_chord = 0x000002UL | 0x000020UL | 0x000200UL | 0x020000UL;
  const bridge::ActionMask first_action_chord =
      bridge::action_mask(bridge::InputAction::DPAD_UP) |
      bridge::action_mask(bridge::InputAction::BUTTON_B) |
      bridge::action_mask(bridge::InputAction::LEFT_SIDE_UPPER) |
      bridge::action_mask(bridge::InputAction::RIGHT_POWER);

  auto edges = state.apply(semantic_packet(first_ride_chord));
  EXPECT_EQ(first_action_chord, edges.pressed);
  EXPECT_EQ(0U, edges.released);
  EXPECT_EQ(first_action_chord, state.active_actions());

  // Release two non-adjacent members while the direction and left shifter stay held.
  const uint32_t held_ride_chord = 0x000002UL | 0x000200UL;
  const bridge::ActionMask held_action_chord =
      bridge::action_mask(bridge::InputAction::DPAD_UP) |
      bridge::action_mask(bridge::InputAction::LEFT_SIDE_UPPER);
  edges = state.apply(semantic_packet(held_ride_chord));
  EXPECT_EQ(0U, edges.pressed);
  EXPECT_EQ(bridge::action_mask(bridge::InputAction::BUTTON_B) |
                bridge::action_mask(bridge::InputAction::RIGHT_POWER),
            edges.released);
  EXPECT_EQ(held_action_chord, state.active_actions());

  // Add two different controls without retriggering either held member.
  const uint32_t second_ride_chord =
      held_ride_chord | 0x000040UL | 0x010000UL;
  const bridge::ActionMask added_actions =
      bridge::action_mask(bridge::InputAction::BUTTON_Y) |
      bridge::action_mask(bridge::InputAction::RIGHT_SIDE_LOWER);
  edges = state.apply(semantic_packet(second_ride_chord));
  EXPECT_EQ(added_actions, edges.pressed);
  EXPECT_EQ(0U, edges.released);
  EXPECT_EQ(held_action_chord | added_actions, state.active_actions());

  // A final idle snapshot releases the complete remaining chord at once.
  edges = state.apply(semantic_packet(0));
  EXPECT_EQ(0U, edges.pressed);
  EXPECT_EQ(held_action_chord | added_actions, edges.released);
  EXPECT_EQ(0U, state.active_actions());
}

void expect_all_frame_prefixes_are_bounded(
    const std::vector<uint8_t> &frame, size_t complete_button_prefix_length,
    uint32_t expected_pressed_buttons, int32_t expected_analog) {
  for (size_t prefix_length = 0; prefix_length <= frame.size(); prefix_length++) {
    bridge::RideInputPacket output{};
    output.wire_button_map = 0x12345678UL;
    output.pressed_buttons = 0x87654321UL;
    output.analog[0] = 999;
    output.analog_present_mask = 0x0FU;

    const auto status =
        bridge::decode_ride_notification(frame.data(), prefix_length, &output);
    const bool is_complete_protobuf =
        prefix_length == complete_button_prefix_length || prefix_length == frame.size();
    if (is_complete_protobuf) {
      EXPECT_EQ(bridge::RideDecodeStatus::OK, status);
      EXPECT_EQ(expected_pressed_buttons, output.pressed_buttons);
      if (prefix_length == frame.size()) {
        EXPECT_TRUE(output.has_analog(0));
        EXPECT_EQ(expected_analog, output.analog[0]);
      } else {
        EXPECT_EQ(0U, output.analog_present_mask);
      }
    } else {
      EXPECT_TRUE(status != bridge::RideDecodeStatus::OK);
      EXPECT_EQ(0x12345678UL, output.wire_button_map);
      EXPECT_EQ(0x87654321UL, output.pressed_buttons);
      EXPECT_EQ(999, output.analog[0]);
      EXPECT_EQ(0x0FU, output.analog_present_mask);
    }
  }
}

void test_every_direct_and_grouped_frame_prefix() {
  const uint32_t pressed = 0x000001UL | 0x000020UL | 0x004000UL;
  const size_t complete_button_prefix_length = packet_prefix(pressed).size();

  const auto direct = direct_packet(pressed, {{0, -73}});
  EXPECT_TRUE(direct.size() > complete_button_prefix_length);
  expect_all_frame_prefixes_are_bounded(direct, complete_button_prefix_length,
                                        pressed, -73);

  const auto grouped = grouped_packet(pressed, {{0, 91}});
  EXPECT_TRUE(grouped.size() > complete_button_prefix_length);
  expect_all_frame_prefixes_are_bounded(grouped, complete_button_prefix_length,
                                        pressed, 91);
}

void test_analog_hysteresis_and_sign_change() {
  bridge::InputState state;
  EXPECT_FALSE(state.set_thresholds(20, 20));
  EXPECT_FALSE(state.set_thresholds(0, 0));
  EXPECT_EQ(35, state.press_threshold());
  EXPECT_EQ(20, state.release_threshold());
  EXPECT_TRUE(state.set_thresholds(35, 0));
  EXPECT_EQ(0, state.release_threshold());
  EXPECT_TRUE(state.set_thresholds(35, 20));

  auto packet = semantic_packet(0);
  set_analog(&packet, 0, 34);
  EXPECT_FALSE(state.apply(packet).changed());
  EXPECT_FALSE(state.active(bridge::InputAction::LEFT_LEVER_POSITIVE));

  set_analog(&packet, 0, 35);
  auto edges = state.apply(packet);
  EXPECT_EQ(bridge::action_mask(bridge::InputAction::LEFT_LEVER_POSITIVE), edges.pressed);

  set_analog(&packet, 0, 21);
  EXPECT_FALSE(state.apply(packet).changed());
  set_analog(&packet, 0, 20);
  edges = state.apply(packet);
  EXPECT_EQ(bridge::action_mask(bridge::InputAction::LEFT_LEVER_POSITIVE), edges.released);

  set_analog(&packet, 0, -35);
  edges = state.apply(packet);
  EXPECT_EQ(bridge::action_mask(bridge::InputAction::LEFT_LEVER_NEGATIVE), edges.pressed);

  // A direct sign jump releases the old direction and presses the new one in
  // the same transaction/report rebuild.
  set_analog(&packet, 0, 40);
  edges = state.apply(packet);
  EXPECT_EQ(bridge::action_mask(bridge::InputAction::LEFT_LEVER_POSITIVE), edges.pressed);
  EXPECT_EQ(bridge::action_mask(bridge::InputAction::LEFT_LEVER_NEGATIVE), edges.released);

  // An omitted channel retains its held state.
  auto no_analog = semantic_packet(0x000010UL);
  edges = state.apply(no_analog);
  EXPECT_TRUE(state.active(bridge::InputAction::LEFT_LEVER_POSITIVE));
  EXPECT_TRUE(state.active(bridge::InputAction::BUTTON_A));
  EXPECT_EQ(40, state.analog(0));

  // Diagnostic channels are preserved without synthesizing logical actions.
  set_analog(&no_analog, 2, 100);
  state.apply(no_analog);
  EXPECT_TRUE(state.has_analog(2));
  EXPECT_EQ(100, state.analog(2));
}

void test_default_and_diagnostic_keymaps() {
  const auto &map = bridge::keymap_for_profile(bridge::KeymapProfile::DELTA_EMULATOR);
  EXPECT_EQ(std::string("delta_emulator"), std::string(map.name));
  EXPECT_EQ(bridge::hid_usage::X,
            map.usages[static_cast<uint8_t>(bridge::InputAction::BUTTON_A)]);
  EXPECT_EQ(bridge::hid_usage::Z,
            map.usages[static_cast<uint8_t>(bridge::InputAction::BUTTON_B)]);
  EXPECT_EQ(bridge::hid_usage::S,
            map.usages[static_cast<uint8_t>(bridge::InputAction::BUTTON_Z)]);
  EXPECT_EQ(bridge::hid_usage::A,
            map.usages[static_cast<uint8_t>(bridge::InputAction::BUTTON_Y)]);
  EXPECT_EQ(bridge::hid_usage::TAB,
            map.usages[static_cast<uint8_t>(bridge::InputAction::LEFT_POWER)]);
  EXPECT_EQ(bridge::hid_usage::RETURN,
            map.usages[static_cast<uint8_t>(bridge::InputAction::RIGHT_POWER)]);
  EXPECT_EQ(bridge::hid_usage::ESCAPE,
            map.usages[static_cast<uint8_t>(bridge::InputAction::RIGHT_SIDE_LOWER)]);

  bridge::KeymapProfile profile{};
  EXPECT_TRUE(bridge::keymap_profile_from_name("delta_emulator", &profile));
  EXPECT_EQ(bridge::KeymapProfile::DELTA_EMULATOR, profile);
  EXPECT_TRUE(bridge::keymap_profile_from_name("diagnostic_all_inputs", &profile));
  EXPECT_EQ(bridge::KeymapProfile::DIAGNOSTIC_ALL_INPUTS, profile);
  EXPECT_FALSE(bridge::keymap_profile_from_name("delta_n64", &profile));

  const auto &diagnostic =
      bridge::keymap_for_profile(bridge::KeymapProfile::DIAGNOSTIC_ALL_INPUTS);
  for (uint8_t outer = 0; outer < bridge::kInputActionCount; outer++) {
    EXPECT_TRUE(diagnostic.usages[outer] != bridge::hid_usage::NONE);
    for (uint8_t inner = static_cast<uint8_t>(outer + 1);
         inner < bridge::kInputActionCount; inner++) {
      EXPECT_TRUE(diagnostic.usages[outer] != diagnostic.usages[inner]);
    }
  }
}

void test_report_holds_chords_duplicates_and_release() {
  const auto &map = bridge::keymap_for_profile(bridge::KeymapProfile::DELTA_EMULATOR);
  bridge::KeyboardReport report{};
  const bridge::ActionMask chord =
      bridge::action_mask(bridge::InputAction::DPAD_LEFT) |
      bridge::action_mask(bridge::InputAction::BUTTON_A) |
      bridge::action_mask(bridge::InputAction::BUTTON_B);
  EXPECT_EQ(bridge::KeyboardReportStatus::OK,
            bridge::build_keyboard_report(chord, map, &report));
  EXPECT_EQ(bridge::hid_usage::ARROW_LEFT, report.keys[0]);
  EXPECT_EQ(bridge::hid_usage::X, report.keys[1]);
  EXPECT_EQ(bridge::hid_usage::Z, report.keys[2]);

  bridge::Keymap duplicate_map = map;
  duplicate_map.usages[static_cast<uint8_t>(bridge::InputAction::BUTTON_B)] =
      bridge::hid_usage::X;
  EXPECT_EQ(bridge::KeyboardReportStatus::OK,
            bridge::build_keyboard_report(
                bridge::action_mask(bridge::InputAction::BUTTON_A) |
                    bridge::action_mask(bridge::InputAction::BUTTON_B),
                duplicate_map, &report));
  EXPECT_EQ(bridge::hid_usage::X, report.keys[0]);
  EXPECT_EQ(bridge::hid_usage::NONE, report.keys[1]);

  // Releasing one of two actions mapped to X must retain X for the held action.
  EXPECT_EQ(bridge::KeyboardReportStatus::OK,
            bridge::build_keyboard_report(
                bridge::action_mask(bridge::InputAction::BUTTON_B), duplicate_map, &report));
  EXPECT_EQ(bridge::hid_usage::X, report.keys[0]);

  bridge::InputState state;
  state.apply(semantic_packet(0x000010UL | 0x000020UL));
  state.release_all();
  EXPECT_EQ(bridge::KeyboardReportStatus::OK,
            bridge::build_keyboard_report(state.active_actions(), map, &report));
  EXPECT_TRUE(report_is_empty(report));

  report.modifiers = 0xFF;
  report.keys[0] = 0xFF;
  bridge::clear_keyboard_report(&report);
  EXPECT_TRUE(report_is_empty(report));
}

void test_report_modifiers_and_six_key_overflow() {
  const auto &default_map =
      bridge::keymap_for_profile(bridge::KeymapProfile::DELTA_EMULATOR);
  bridge::Keymap modifier_map = default_map;
  modifier_map.usages[static_cast<uint8_t>(bridge::InputAction::BUTTON_A)] =
      bridge::hid_usage::LEFT_CONTROL;
  modifier_map.usages[static_cast<uint8_t>(bridge::InputAction::BUTTON_B)] =
      bridge::hid_usage::RIGHT_GUI;

  bridge::KeyboardReport report{};
  EXPECT_EQ(bridge::KeyboardReportStatus::OK,
            bridge::build_keyboard_report(
                bridge::action_mask(bridge::InputAction::BUTTON_A) |
                    bridge::action_mask(bridge::InputAction::BUTTON_B),
                modifier_map, &report));
  EXPECT_EQ(0x81U, report.modifiers);
  EXPECT_EQ(bridge::hid_usage::NONE, report.keys[0]);

  bridge::ActionMask seven_unique = 0;
  for (uint8_t action = 0; action < 7; action++) {
    seven_unique |= static_cast<bridge::ActionMask>(1UL << action);
  }
  EXPECT_EQ(bridge::KeyboardReportStatus::SIX_KEY_ROLLOVER,
            bridge::build_keyboard_report(seven_unique, default_map, &report));
  for (uint8_t key : report.keys) {
    EXPECT_EQ(bridge::hid_usage::ERROR_ROLLOVER, key);
  }

  // A duplicate among seven actions leaves six distinct keys and must not
  // falsely report rollover.
  bridge::Keymap duplicate_map = default_map;
  duplicate_map.usages[6] = duplicate_map.usages[5];
  EXPECT_EQ(bridge::KeyboardReportStatus::OK,
            bridge::build_keyboard_report(seven_unique, duplicate_map, &report));

  EXPECT_EQ(bridge::KeyboardReportStatus::NULL_ARGUMENT,
            bridge::build_keyboard_report(0, default_map, nullptr));
}

using TestFunction = void (*)();

void run_test(const char *name, TestFunction function) {
  const int failures_before = failures;
  function();
  tests_run++;
  std::cout << (failures == failures_before ? "PASS " : "FAIL ") << name << '\n';
}

}  // namespace

int main() {
  run_test("direct layout and inverse mask", test_direct_layout_and_inverse_mask);
  run_test("grouped layout, unknown fields, and ZigZag", test_grouped_layout_unknown_fields_and_zigzag_extremes);
  run_test("malformed input is transactional", test_decoder_rejects_bad_input_transactionally);
  run_test("button edges, chords, and release-all", test_button_edges_chords_and_release_all);
  run_test("all 16 documented button bits", test_every_documented_button_bit_independently);
  run_test("mixed chords and partial releases", test_mixed_chord_partial_releases);
  run_test("every direct and grouped frame prefix", test_every_direct_and_grouped_frame_prefix);
  run_test("analog hysteresis and sign changes", test_analog_hysteresis_and_sign_change);
  run_test("default and diagnostic keymaps", test_default_and_diagnostic_keymaps);
  run_test("report holds, duplicates, and release", test_report_holds_chords_duplicates_and_release);
  run_test("report modifiers and 6KRO overflow", test_report_modifiers_and_six_key_overflow);

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed across " << tests_run << " tests\n";
    return EXIT_FAILURE;
  }
  std::cout << "All " << tests_run << " host tests passed\n";
  return EXIT_SUCCESS;
}
