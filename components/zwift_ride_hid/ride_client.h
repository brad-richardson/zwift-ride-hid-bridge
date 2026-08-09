// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>

#include "esphome/components/ble_client/ble_client.h"

#ifdef USE_ESP32

namespace esphome::zwift_ride_hid {

/** Connection progress owned by RideClient.
 *
 * ESPHome's BLEClient remains the owner of scanning, connection setup, service
 * discovery, and the process-global Bluedroid callbacks. This state machine
 * starts at ESP_GATTC_CONNECT_EVT and only sequences the Ride protocol work.
 */
enum class RideClientState : uint8_t {
  DISCONNECTED,
  DISCOVERING,
  SENDING_HANDSHAKE,
  SUBSCRIBING_ASYNC,
  SUBSCRIBING_SYNC_TX,
  READY,
  ERROR,
};

const char* ride_client_state_to_string(RideClientState state);

/** Allocation-free callbacks into the composing component.
 *
 * All methods run in ESPHome's BLE event dispatch context. Implementations must
 * copy a frame if they need it after the callback returns and should defer slow
 * work to their component loop.
 */
class RideClientListener {
 public:
  virtual ~RideClientListener() = default;

  virtual void on_ride_ready() {}
  virtual void on_ride_disconnected() {}
  virtual void on_ride_notification(const uint8_t* data, uint16_t length) {}
  virtual void on_ride_sync_response(const uint8_t* data, uint16_t length) {}
};

/** Minimal Zwift Ride GATT protocol helper for ESPHome 2026.7.x.
 *
 * This class intentionally is not a BLEClientNode. ZwiftRideHid owns the node
 * and forwards its gattc_event_handler() calls here, which avoids a second node
 * independently declaring service discovery complete.
 */
class RideClient final {
 public:
  static constexpr uint8_t DEFAULT_HAPTIC_PATTERN = 0x20;
  // Published reverse engineering reports 123 patterns. Treat those as the
  // zero-based range 0..122 until hardware captures prove a wider range.
  static constexpr uint8_t MAX_HAPTIC_PATTERN = 122;
  static constexpr uint32_t DISCOVERY_TIMEOUT_MS = 30000;
  static constexpr uint32_t GATT_OPERATION_TIMEOUT_MS = 10000;
  static constexpr uint32_t HAPTIC_WRITE_TIMEOUT_MS = 5000;

  void set_ble_client_parent(ble_client::BLEClient* client) {
    this->client_ = client;
  }
  void set_listener(RideClientListener* listener) {
    this->listener_ = listener;
  }

  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t* param);

  /** Recover from GATT completion events that never arrive.
   *
   * The composing BLEClientNode must call this from its loop(). A setup timeout
   * asks ESPHome's BLE client to disconnect so auto-connect can perform a clean
   * service discovery; a haptic timeout only releases the single-write gate.
   */
  void loop();

  /** Send one of the controller's fixed haptic patterns.
   *
   * Returns false if the Ride session is not ready, the pattern is outside the
   * bounded known range, another haptic write is in flight, or ESP-IDF rejects
   * the write. This never queues or allocates.
   */
  bool vibrate(uint8_t pattern = DEFAULT_HAPTIC_PATTERN);

  RideClientState state() const { return this->state_; }
  bool ready() const { return this->state_ == RideClientState::READY; }
  bool has_sync_tx() const { return this->sync_tx_handle_ != 0; }
  uint32_t setup_timeout_count() const { return this->setup_timeout_count_; }
  uint32_t haptic_timeout_count() const { return this->haptic_timeout_count_; }

 protected:
  bool discover_handles_(esp_gatt_if_t gattc_if, uint16_t conn_id);
  bool find_service_(esp_gatt_if_t gattc_if, uint16_t conn_id,
                     uint16_t& start_handle, uint16_t& end_handle);
  bool find_characteristic_(esp_gatt_if_t gattc_if, uint16_t conn_id,
                            uint16_t start_handle, uint16_t end_handle,
                            const char* uuid, uint16_t& handle,
                            esp_gatt_char_prop_t& properties);
  bool find_cccd_(esp_gatt_if_t gattc_if, uint16_t conn_id,
                  uint16_t char_handle, uint16_t& descriptor_handle);

  bool send_handshake_();
  bool subscribe_async_();
  void subscribe_sync_tx_or_ready_();
  bool register_for_notify_(uint16_t handle);
  void mark_ready_();
  void fail_setup_(const char* operation, int status);
  void begin_connection_(uint16_t conn_id);
  void report_disconnected_();
  void reset_connection_();
  void enter_state_(RideClientState state);
  uint32_t state_timeout_ms_() const;
  bool connection_matches_(uint16_t conn_id) const;

  ble_client::BLEClient* client_{nullptr};
  RideClientListener* listener_{nullptr};

  RideClientState state_{RideClientState::DISCONNECTED};
  esp_gatt_if_t gattc_if_{ESP_GATT_IF_NONE};
  uint16_t conn_id_{0xFFFF};
  uint16_t async_handle_{0};
  uint16_t async_cccd_handle_{0};
  uint16_t sync_rx_handle_{0};
  uint16_t sync_tx_handle_{0};
  uint16_t sync_tx_cccd_handle_{0};
  bool async_enabled_{false};
  bool haptic_write_pending_{false};
  bool disconnect_reported_{true};
  uint32_t state_started_at_{0};
  uint32_t haptic_write_started_at_{0};
  uint32_t setup_timeout_count_{0};
  uint32_t haptic_timeout_count_{0};

  uint8_t haptic_command_[11]{0x12,
                              0x12,
                              0x08,
                              0x0A,
                              0x06,
                              0x08,
                              0x02,
                              0x10,
                              0x00,
                              0x18,
                              DEFAULT_HAPTIC_PATTERN};
};

}  // namespace esphome::zwift_ride_hid

#endif  // USE_ESP32
