// SPDX-License-Identifier: GPL-3.0-only
//
// Drives RideClient's handshake sequencer against a fake ESP-IDF GATT client.
// The production source is compiled unmodified; only the stack beneath it is
// replaced, so these tests exercise the real event ordering, the real
// watchdogs, and the real teardown paths.
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "components/zwift_ride_hid/ride_client.h"
#include "fakes/fake_ble.h"

namespace bridge = esphome::zwift_ride_hid;
namespace espbt = esphome::esp32_ble_tracker;

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

constexpr uint16_t kConnId = 4;
const esp_bd_addr_t kRideAddress = {0xE1, 0x38, 0xED, 0x70, 0xFA, 0x9F};
const esp_bd_addr_t kOtherAddress = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

/// Records what the component reported back up to ZwiftRideHid.
struct RecordingListener : bridge::RideClientListener {
  uint32_t ready_count{0};
  uint32_t disconnected_count{0};
  std::vector<std::vector<uint8_t>> notifications;
  std::vector<std::vector<uint8_t>> sync_responses;

  void on_ride_ready() override { this->ready_count++; }
  void on_ride_disconnected() override { this->disconnected_count++; }
  void on_ride_notification(const uint8_t *data, uint16_t length) override {
    this->notifications.emplace_back(data, data + length);
  }
  void on_ride_sync_response(const uint8_t *data, uint16_t length) override {
    this->sync_responses.emplace_back(data, data + length);
  }
};

/// One client plus its listener, wired the way ZwiftRideHid wires them.
struct Harness {
  esphome::ble_client::BLEClient client;
  RecordingListener listener;
  bridge::RideClient ride;

  Harness() {
    fake_ble::reset();
    this->client.set_remote_bda(kRideAddress);
    this->client.set_state(espbt::ClientState::ESTABLISHED);
    this->ride.set_ble_client_parent(&this->client);
    this->ride.set_listener(&this->listener);
  }

  void send(esp_gattc_cb_event_t event, esp_ble_gattc_cb_param_t param) {
    this->ride.gattc_event_handler(event, 1, &param);
  }

  void connect() {
    esp_ble_gattc_cb_param_t param{};
    param.connect.conn_id = kConnId;
    std::memcpy(param.connect.remote_bda, kRideAddress, sizeof(esp_bd_addr_t));
    this->send(ESP_GATTC_CONNECT_EVT, param);
  }

  void complete_discovery(esp_gatt_status_t status = ESP_GATT_OK) {
    esp_ble_gattc_cb_param_t param{};
    param.search_cmpl.conn_id = kConnId;
    param.search_cmpl.status = status;
    this->send(ESP_GATTC_SEARCH_CMPL_EVT, param);
  }

  void write_complete(uint16_t handle, esp_gatt_status_t status = ESP_GATT_OK) {
    esp_ble_gattc_cb_param_t param{};
    param.write.conn_id = kConnId;
    param.write.handle = handle;
    param.write.status = status;
    this->send(ESP_GATTC_WRITE_CHAR_EVT, param);
  }

  void notify_registered(uint16_t handle, esp_gatt_status_t status = ESP_GATT_OK) {
    esp_ble_gattc_cb_param_t param{};
    param.reg_for_notify.handle = handle;
    param.reg_for_notify.status = status;
    this->send(ESP_GATTC_REG_FOR_NOTIFY_EVT, param);
  }

  void descriptor_written(uint16_t handle, esp_gatt_status_t status = ESP_GATT_OK) {
    esp_ble_gattc_cb_param_t param{};
    param.write.conn_id = kConnId;
    param.write.handle = handle;
    param.write.status = status;
    this->send(ESP_GATTC_WRITE_DESCR_EVT, param);
  }

  /// Run the whole handshake to READY the way real hardware does.
  void reach_ready() {
    const auto &s = fake_ble::state();
    this->connect();
    this->complete_discovery();
    this->write_complete(s.sync_rx_char.handle);
    this->notify_registered(s.async_char.handle);
    this->descriptor_written(s.async_char.cccd_handle);
    this->notify_registered(s.sync_tx_char.handle);
    this->descriptor_written(s.sync_tx_char.cccd_handle);
  }
};

void test_handshake_reaches_ready_in_the_documented_order() {
  Harness h;
  const auto &s = fake_ble::state();

  h.connect();
  EXPECT_EQ(bridge::RideClientState::DISCOVERING, h.ride.state());

  h.complete_discovery();
  // Discovery completing must submit the RideOn handshake, not subscribe yet.
  EXPECT_EQ(bridge::RideClientState::SENDING_HANDSHAKE, h.ride.state());
  EXPECT_EQ(1U, s.gattc_writes.size());
  EXPECT_EQ(s.sync_rx_char.handle, s.gattc_writes[0].handle);
  EXPECT_EQ(std::string("RideOn"),
            std::string(s.gattc_writes[0].value.begin(), s.gattc_writes[0].value.end()));
  // The controller's Sync-RX is write-without-response only.
  EXPECT_EQ(ESP_GATT_WRITE_TYPE_NO_RSP, s.gattc_writes[0].write_type);

  h.write_complete(s.sync_rx_char.handle);
  EXPECT_EQ(bridge::RideClientState::SUBSCRIBING_ASYNC, h.ride.state());
  EXPECT_EQ(1U, s.registered_notify_handles.size());
  EXPECT_EQ(s.async_char.handle, s.registered_notify_handles[0]);

  // Registration alone must not advance: the CCCD write is what confirms it.
  h.notify_registered(s.async_char.handle);
  EXPECT_EQ(bridge::RideClientState::SUBSCRIBING_ASYNC, h.ride.state());
  EXPECT_EQ(0U, h.listener.ready_count);

  h.descriptor_written(s.async_char.cccd_handle);
  EXPECT_EQ(bridge::RideClientState::SUBSCRIBING_SYNC_TX, h.ride.state());

  h.notify_registered(s.sync_tx_char.handle);
  h.descriptor_written(s.sync_tx_char.cccd_handle);
  EXPECT_EQ(bridge::RideClientState::READY, h.ride.state());
  EXPECT_TRUE(h.ride.ready());
  EXPECT_EQ(1U, h.listener.ready_count);
  EXPECT_TRUE(h.ride.has_sync_tx());
}

void test_optional_sync_tx_failure_still_reaches_ready() {
  // Sync-TX carries protocol replies, not input, so losing it must not stop
  // the session; input arrives on the async characteristic either way.
  Harness h;
  auto &s = fake_ble::state();
  s.sync_tx_char.present = false;

  h.connect();
  h.complete_discovery();
  h.write_complete(s.sync_rx_char.handle);
  h.notify_registered(s.async_char.handle);
  h.descriptor_written(s.async_char.cccd_handle);

  EXPECT_EQ(bridge::RideClientState::READY, h.ride.state());
  EXPECT_EQ(1U, h.listener.ready_count);
  EXPECT_FALSE(h.ride.has_sync_tx());
}

void test_missing_service_or_characteristic_fails_and_disconnects() {
  for (int missing = 0; missing < 3; missing++) {
    Harness h;
    auto &s = fake_ble::state();
    if (missing == 0)
      s.service_present = false;
    else if (missing == 1)
      s.async_char.present = false;
    else
      s.sync_rx_char.present = false;

    h.connect();
    h.complete_discovery();

    EXPECT_EQ(bridge::RideClientState::ERROR, h.ride.state());
    EXPECT_EQ(0U, h.listener.ready_count);
    // A failed setup must force a full reconnect so stale handles cannot
    // survive into the next session.
    EXPECT_TRUE(h.client.disconnect_requested());
  }
}

void test_non_notifiable_async_characteristic_is_rejected() {
  Harness h;
  auto &s = fake_ble::state();
  s.async_char.properties = ESP_GATT_CHAR_PROP_BIT_READ;

  h.connect();
  h.complete_discovery();
  EXPECT_EQ(bridge::RideClientState::ERROR, h.ride.state());
  EXPECT_TRUE(h.client.disconnect_requested());
}

void test_events_for_another_peer_are_ignored() {
  Harness h;
  esp_ble_gattc_cb_param_t param{};
  param.connect.conn_id = 9;
  std::memcpy(param.connect.remote_bda, kOtherAddress, sizeof(esp_bd_addr_t));
  h.send(ESP_GATTC_CONNECT_EVT, param);
  EXPECT_EQ(bridge::RideClientState::DISCONNECTED, h.ride.state());

  // And a notification on a stale connection id must not reach the listener.
  h.reach_ready();
  uint8_t payload[] = {0x23, 0x08};
  esp_ble_gattc_cb_param_t notify{};
  notify.notify.conn_id = kConnId + 1;
  notify.notify.handle = fake_ble::state().async_char.handle;
  notify.notify.value = payload;
  notify.notify.value_len = sizeof(payload);
  h.send(ESP_GATTC_NOTIFY_EVT, notify);
  EXPECT_EQ(0U, h.listener.notifications.size());
}

void test_notifications_are_routed_by_handle() {
  Harness h;
  h.reach_ready();
  const auto &s = fake_ble::state();

  uint8_t input[] = {0x23, 0x08, 0xFF};
  esp_ble_gattc_cb_param_t notify{};
  notify.notify.conn_id = kConnId;
  notify.notify.handle = s.async_char.handle;
  notify.notify.value = input;
  notify.notify.value_len = sizeof(input);
  h.send(ESP_GATTC_NOTIFY_EVT, notify);

  uint8_t reply[] = {0x19, 0x01};
  notify.notify.handle = s.sync_tx_char.handle;
  notify.notify.value = reply;
  notify.notify.value_len = sizeof(reply);
  h.send(ESP_GATTC_NOTIFY_EVT, notify);

  EXPECT_EQ(1U, h.listener.notifications.size());
  EXPECT_EQ(3U, h.listener.notifications[0].size());
  EXPECT_EQ(1U, h.listener.sync_responses.size());
}

void test_disconnect_is_reported_once() {
  Harness h;
  h.reach_ready();

  esp_ble_gattc_cb_param_t param{};
  std::memcpy(param.disconnect.remote_bda, kRideAddress, sizeof(esp_bd_addr_t));
  h.send(ESP_GATTC_DISCONNECT_EVT, param);
  EXPECT_EQ(1U, h.listener.disconnected_count);
  EXPECT_EQ(bridge::RideClientState::DISCONNECTED, h.ride.state());

  // CLOSE normally follows DISCONNECT. The session is already gone, so it must
  // not produce a second release-all in the owning component.
  esp_ble_gattc_cb_param_t close{};
  std::memcpy(close.close.remote_bda, kRideAddress, sizeof(esp_bd_addr_t));
  h.send(ESP_GATTC_CLOSE_EVT, close);
  EXPECT_EQ(1U, h.listener.disconnected_count);
}

void test_setup_watchdog_recovers_a_stalled_handshake() {
  Harness h;
  h.connect();
  h.complete_discovery();
  EXPECT_EQ(bridge::RideClientState::SENDING_HANDSHAKE, h.ride.state());

  // The write completion never arrives.
  fake_ble::advance(bridge::RideClient::GATT_OPERATION_TIMEOUT_MS - 1);
  h.ride.loop();
  EXPECT_EQ(bridge::RideClientState::SENDING_HANDSHAKE, h.ride.state());
  EXPECT_EQ(0U, h.ride.setup_timeout_count());

  fake_ble::advance(1);
  h.ride.loop();
  EXPECT_EQ(1U, h.ride.setup_timeout_count());
  EXPECT_EQ(bridge::RideClientState::ERROR, h.ride.state());
  EXPECT_TRUE(h.client.disconnect_requested());
}

void test_discovery_watchdog_uses_the_longer_timeout() {
  Harness h;
  h.connect();
  fake_ble::advance(bridge::RideClient::DISCOVERY_TIMEOUT_MS - 1);
  h.ride.loop();
  EXPECT_EQ(bridge::RideClientState::DISCOVERING, h.ride.state());
  fake_ble::advance(1);
  h.ride.loop();
  EXPECT_EQ(1U, h.ride.setup_timeout_count());
}

void test_idle_client_without_a_disconnect_event_ends_the_session() {
  // ESPHome's own ten-second teardown watchdog reaches IDLE through a path
  // that dispatches no node event. Before this check the bridge kept reporting
  // a live session and holding keys until some later connection arrived.
  Harness h;
  h.reach_ready();
  EXPECT_EQ(0U, h.listener.disconnected_count);

  h.client.set_state(espbt::ClientState::IDLE);
  h.ride.loop();

  EXPECT_EQ(1U, h.listener.disconnected_count);
  EXPECT_EQ(bridge::RideClientState::DISCONNECTED, h.ride.state());
  EXPECT_FALSE(h.ride.ready());
}

void test_error_state_is_not_left_stranded() {
  // fail_setup_ asks the client to disconnect, but if that request is refused
  // because the client is already IDLE, nothing else would ever clear ERROR.
  Harness h;
  auto &s = fake_ble::state();
  s.service_present = false;
  h.connect();
  h.complete_discovery();
  EXPECT_EQ(bridge::RideClientState::ERROR, h.ride.state());

  h.client.set_state(espbt::ClientState::IDLE);
  h.ride.loop();
  EXPECT_EQ(bridge::RideClientState::DISCONNECTED, h.ride.state());
  EXPECT_EQ(1U, h.listener.disconnected_count);
}

void test_haptics_are_gated_and_bounded() {
  Harness h;
  const auto &s = fake_ble::state();

  // Not ready: nothing may be written to the controller.
  EXPECT_FALSE(h.ride.vibrate());
  EXPECT_EQ(0U, s.gattc_writes.size());

  h.reach_ready();
  const size_t writes_after_handshake = s.gattc_writes.size();

  EXPECT_TRUE(h.ride.vibrate());
  EXPECT_EQ(writes_after_handshake + 1, s.gattc_writes.size());
  // One write may be in flight at a time.
  EXPECT_FALSE(h.ride.vibrate());

  // Out-of-range patterns are refused rather than sent.
  h.write_complete(s.sync_rx_char.handle);
  EXPECT_FALSE(h.ride.vibrate(bridge::RideClient::MAX_HAPTIC_PATTERN + 1));
  EXPECT_EQ(writes_after_handshake + 1, s.gattc_writes.size());
}

void test_haptic_timeout_releases_the_write_gate() {
  Harness h;
  h.reach_ready();
  EXPECT_TRUE(h.ride.vibrate());

  // The completion never arrives; the gate must not stay shut forever.
  fake_ble::advance(bridge::RideClient::HAPTIC_WRITE_TIMEOUT_MS);
  h.ride.loop();
  EXPECT_EQ(1U, h.ride.haptic_timeout_count());
  EXPECT_TRUE(h.ride.vibrate());
}

void test_reconnect_after_failure_starts_from_a_clean_state() {
  Harness h;
  auto &s = fake_ble::state();
  s.async_char.present = false;
  h.connect();
  h.complete_discovery();
  EXPECT_EQ(bridge::RideClientState::ERROR, h.ride.state());

  // The controller comes back and this time discovery succeeds. Stale handles
  // from the failed attempt must not survive.
  s.async_char.present = true;
  h.client.clear_disconnect_requested();
  h.reach_ready();
  EXPECT_EQ(bridge::RideClientState::READY, h.ride.state());
  EXPECT_EQ(1U, h.listener.ready_count);
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
  run_test("handshake reaches ready in order", test_handshake_reaches_ready_in_the_documented_order);
  run_test("optional Sync-TX failure still reaches ready", test_optional_sync_tx_failure_still_reaches_ready);
  run_test("missing service or characteristic fails", test_missing_service_or_characteristic_fails_and_disconnects);
  run_test("non-notifiable async characteristic rejected", test_non_notifiable_async_characteristic_is_rejected);
  run_test("events for another peer are ignored", test_events_for_another_peer_are_ignored);
  run_test("notifications routed by handle", test_notifications_are_routed_by_handle);
  run_test("disconnect reported once", test_disconnect_is_reported_once);
  run_test("setup watchdog recovers a stall", test_setup_watchdog_recovers_a_stalled_handshake);
  run_test("discovery watchdog uses longer timeout", test_discovery_watchdog_uses_the_longer_timeout);
  run_test("idle client without disconnect event ends session", test_idle_client_without_a_disconnect_event_ends_the_session);
  run_test("error state is not left stranded", test_error_state_is_not_left_stranded);
  run_test("haptics gated and bounded", test_haptics_are_gated_and_bounded);
  run_test("haptic timeout releases the gate", test_haptic_timeout_releases_the_write_gate);
  run_test("reconnect after failure is clean", test_reconnect_after_failure_starts_from_a_clean_state);

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed across " << tests_run << " tests\n";
    return EXIT_FAILURE;
  }
  std::cout << "All " << tests_run << " Ride client tests passed\n";
  return EXIT_SUCCESS;
}
