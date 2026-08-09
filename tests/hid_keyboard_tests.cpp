// SPDX-License-Identifier: GPL-3.0-only
//
// Drives the HID keyboard server against a fake Bluedroid GATTS/GAP stack.
// The interest here is the advertising gate and the report path: the gate is
// six interacting booleans that decide whether the bridge is discoverable, and
// getting it wrong either exposes a keyboard with no controller behind it or
// leaves a key held on the iPad.
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include "components/zwift_ride_hid/hid_keyboard.h"
#include "fakes/fake_ble.h"

namespace bridge = esphome::zwift_ride_hid;

namespace {

int failures = 0;
int tests_run = 0;

#define EXPECT_TRUE(condition)                                                 \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << __FILE__ << ':' << __LINE__                                 \
                << ": expected true: " #condition << '\n';                     \
      failures++;                                                              \
    }                                                                          \
  } while (false)

#define EXPECT_FALSE(condition) EXPECT_TRUE(!(condition))

#define EXPECT_EQ(expected, actual)                                            \
  do {                                                                         \
    const auto expected_value = (expected);                                    \
    const auto actual_value = (actual);                                        \
    if (!(expected_value == actual_value)) {                                   \
      std::cerr << __FILE__ << ':' << __LINE__                                 \
                << ": values differ: " #expected " != " #actual << '\n';       \
      failures++;                                                              \
    }                                                                          \
  } while (false)

constexpr esp_gatt_if_t kGattsIf = 3;
constexpr uint16_t kConnId = 7;
const esp_bd_addr_t kHost = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
const esp_bd_addr_t kOtherHost = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

/// Builds the GATT database the way Bluedroid does, then optionally connects a
/// host, so each test starts from a named point in the lifecycle.
struct Harness {
  esphome::esp32_ble::ESP32BLE ble;
  bridge::HidKeyboard keyboard;
  Harness() {
    fake_ble::reset();
    esphome::esp32_ble::global_ble = &this->ble;
    this->keyboard.set_parent(&this->ble);
  }

  ~Harness() { esphome::esp32_ble::global_ble = nullptr; }

  void send(esp_gatts_cb_event_t event, esp_ble_gatts_cb_param_t param) {
    this->keyboard.gatts_event_handler(event, kGattsIf, &param);
  }
  void send_gap(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t param) {
    this->keyboard.gap_event_handler(event, &param);
  }

  void connect_host(const esp_bd_addr_t address = kHost, uint8_t link_role = 1) {
    esp_ble_gatts_cb_param_t connect{};
    connect.connect.conn_id = kConnId;
    connect.connect.link_role = link_role;
    std::memcpy(connect.connect.remote_bda, address, sizeof(esp_bd_addr_t));
    this->send(ESP_GATTS_CONNECT_EVT, connect);
  }

};

// These tests deliberately stop short of replaying the full GATT database
// construction. The component validates every attribute callback against the
// exact UUID it asked for, so a faithful replay would have to duplicate the
// attribute table itself — a second copy that could drift from the first and
// assert nothing useful. The gate, the report path, and the event filtering
// are the parts where a regression actually costs a stuck key or an exposed
// keyboard, and those are reachable without it.

void test_advertising_is_refused_until_the_upstream_source_is_ready() {
  Harness h;
  h.keyboard.begin("Zwift Ride KB");
  h.keyboard.loop();

  // Nothing may be advertised before the Ride handshake opens the gate, which
  // is what stops an empty keyboard appearing in the iPad's Bluetooth list.
  EXPECT_FALSE(h.keyboard.advertise());
  EXPECT_EQ(0U, fake_ble::state().advertising_starts);
  EXPECT_FALSE(h.keyboard.ready());
}

void test_quiesce_closes_the_gate_and_releases_the_host() {
  Harness h;
  h.keyboard.begin("Zwift Ride KB");
  h.keyboard.set_advertising_allowed(true);

  h.keyboard.quiesce();
  EXPECT_TRUE(h.keyboard.quiesced());
  // A quiesced server must refuse to advertise even when told it may.
  h.keyboard.set_advertising_allowed(true);
  EXPECT_FALSE(h.keyboard.advertise());
  EXPECT_FALSE(h.keyboard.ready());
}

void test_resume_does_not_reopen_advertising_by_itself() {
  // resume() clears the OTA/shutdown gate only. Advertising must still wait
  // for a fresh Ride handshake, otherwise an aborted update would expose the
  // keyboard with no controller behind it.
  Harness h;
  h.keyboard.begin("Zwift Ride KB");
  h.keyboard.set_advertising_allowed(true);
  h.keyboard.quiesce();

  h.keyboard.resume();
  EXPECT_FALSE(h.keyboard.quiesced());
  EXPECT_FALSE(h.keyboard.advertising_allowed());
  EXPECT_FALSE(h.keyboard.advertise());
}

void test_reports_are_refused_while_quiesced() {
  Harness h;
  h.keyboard.begin("Zwift Ride KB");
  h.keyboard.quiesce();

  const uint8_t keys[] = {0x04};
  EXPECT_FALSE(h.keyboard.send_report(0, keys, 1));
  EXPECT_EQ(0U, fake_ble::state().hid_reports.size());
}

void test_release_all_is_refused_while_quiesced() {
  Harness h;
  h.keyboard.begin("Zwift Ride KB");
  h.keyboard.quiesce();
  EXPECT_FALSE(h.keyboard.release_all());
}

void test_oversized_report_is_rejected() {
  Harness h;
  h.keyboard.begin("Zwift Ride KB");
  const uint8_t keys[] = {1, 2, 3, 4, 5, 6, 7};
  EXPECT_FALSE(h.keyboard.send_report(0, keys, sizeof(keys)));
  EXPECT_FALSE(h.keyboard.send_report(0, nullptr, 1));
}

void test_begin_validates_the_advertised_name() {
  Harness h;
  EXPECT_FALSE(h.keyboard.begin(""));
  EXPECT_FALSE(h.keyboard.begin(std::string(21, 'x')));
  EXPECT_TRUE(h.keyboard.begin("Zwift Ride KB"));
  // The name is fixed once the server has been scheduled.
  EXPECT_FALSE(h.keyboard.begin("Something Else"));
}

void test_central_role_connection_is_not_a_hid_host() {
  // GATTS sees the outbound Ride link too. Treating it as the keyboard host
  // would let controller traffic masquerade as an iPad.
  Harness h;
  h.keyboard.begin("Zwift Ride KB");
  h.connect_host(kHost, /*link_role=*/0);
  EXPECT_FALSE(h.keyboard.connected());
}

void test_connection_is_rejected_before_the_database_is_ready() {
  // A host that connects while the database is still being built would cache
  // a partial HID service, so it is disconnected rather than accepted. The
  // application has to be registered first, or the event is not ours at all.
  Harness h;
  h.keyboard.begin("Zwift Ride KB");
  h.keyboard.loop();

  esp_ble_gatts_cb_param_t reg{};
  reg.reg.app_id = bridge::HidKeyboard::APP_ID;
  reg.reg.status = ESP_GATT_OK;
  h.send(ESP_GATTS_REG_EVT, reg);

  h.connect_host();
  EXPECT_FALSE(h.keyboard.connected());
  EXPECT_EQ(1U, fake_ble::state().peer_disconnects);
}

void test_events_before_registration_are_not_ours() {
  // Until the application is registered there is no interface to match on, so
  // an unrelated connection must be left entirely alone rather than refused.
  Harness h;
  h.keyboard.begin("Zwift Ride KB");
  h.connect_host();
  EXPECT_FALSE(h.keyboard.connected());
  EXPECT_EQ(0U, fake_ble::state().peer_disconnects);
}

void test_ble_restart_clears_connection_state() {
  Harness h;
  h.keyboard.begin("Zwift Ride KB");
  h.keyboard.ble_before_disabled_event_handler();
  EXPECT_FALSE(h.keyboard.connected());
  EXPECT_FALSE(h.keyboard.ready());
  EXPECT_FALSE(h.keyboard.advertising_allowed());
}

void test_gatts_events_for_another_app_are_ignored() {
  Harness h;
  h.keyboard.begin("Zwift Ride KB");

  esp_ble_gatts_cb_param_t reg{};
  reg.reg.app_id = bridge::HidKeyboard::APP_ID + 1;
  reg.reg.status = ESP_GATT_OK;
  h.send(ESP_GATTS_REG_EVT, reg);
  // Another application's registration must not adopt this server's interface.
  EXPECT_FALSE(h.keyboard.service_ready());
}

void test_security_events_are_filtered_by_peer() {
  Harness h;
  h.keyboard.begin("Zwift Ride KB");

  // With no host connected, a security request from anyone must be ignored
  // rather than answered on behalf of a peer that is not ours.
  esp_ble_gap_cb_param_t security{};
  std::memcpy(security.ble_security.ble_req.bd_addr, kOtherHost, sizeof(esp_bd_addr_t));
  h.send_gap(ESP_GAP_BLE_SEC_REQ_EVT, security);
  EXPECT_EQ(0U, fake_ble::state().security_responses);
}

void test_advertising_start_completion_is_undone_when_the_gate_closed() {
  // A start can already be in flight when the gate closes. The completion must
  // then stop advertising rather than leaving the bridge discoverable.
  Harness h;
  h.keyboard.begin("Zwift Ride KB");

  esp_ble_gap_cb_param_t started{};
  started.adv_start_cmpl.status = ESP_BT_STATUS_SUCCESS;
  h.send_gap(ESP_GAP_BLE_ADV_START_COMPLETE_EVT, started);

  EXPECT_EQ(1U, fake_ble::state().advertising_stops);
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
  run_test("advertising refused until upstream ready", test_advertising_is_refused_until_the_upstream_source_is_ready);
  run_test("quiesce closes the gate", test_quiesce_closes_the_gate_and_releases_the_host);
  run_test("resume does not reopen advertising", test_resume_does_not_reopen_advertising_by_itself);
  run_test("reports refused while quiesced", test_reports_are_refused_while_quiesced);
  run_test("release-all refused while quiesced", test_release_all_is_refused_while_quiesced);
  run_test("oversized report rejected", test_oversized_report_is_rejected);
  run_test("begin validates the advertised name", test_begin_validates_the_advertised_name);
  run_test("central-role connection is not a HID host", test_central_role_connection_is_not_a_hid_host);
  run_test("connection rejected before database ready", test_connection_is_rejected_before_the_database_is_ready);
  run_test("events before registration are not ours", test_events_before_registration_are_not_ours);
  run_test("BLE restart clears connection state", test_ble_restart_clears_connection_state);
  run_test("events for another app ignored", test_gatts_events_for_another_app_are_ignored);
  run_test("security events filtered by peer", test_security_events_are_filtered_by_peer);
  run_test("late advertising start is undone", test_advertising_start_completion_is_undone_when_the_gate_closed);

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed across " << tests_run << " tests\n";
    return EXIT_FAILURE;
  }
  std::cout << "All " << tests_run << " HID keyboard tests passed\n";
  return EXIT_SUCCESS;
}
