// SPDX-License-Identifier: GPL-3.0-only
#include "ride_client.h"

#ifdef USE_ESP32

#include <cinttypes>

#include "esphome/components/esp32_ble/ble_uuid.h"
#include "esphome/components/esp32_ble_client/ble_client_base.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::zwift_ride_hid {

namespace {

static const char* const TAG = "zwift_ride.client";

static constexpr uint16_t ZWIFT_RIDE_SERVICE_UUID = 0xFC82;
static constexpr char ASYNC_UUID[] = "00000002-19CA-4651-86E5-FA29DCDD09D1";
static constexpr char SYNC_RX_UUID[] = "00000003-19CA-4651-86E5-FA29DCDD09D1";
static constexpr char SYNC_TX_UUID[] = "00000004-19CA-4651-86E5-FA29DCDD09D1";
static constexpr uint8_t RIDE_ON[] = {'R', 'i', 'd', 'e', 'O', 'n'};

// Unsigned subtraction is rollover-safe as long as the interval is shorter
// than 2^31 milliseconds. All watchdog intervals below are only seconds long.
constexpr bool timeout_elapsed(uint32_t now, uint32_t started_at,
                               uint32_t timeout_ms) {
  return static_cast<uint32_t>(now - started_at) >= timeout_ms;
}

// Compile-time coverage for the millis() wrap boundary: UINT32_MAX-4 to 4 is
// nine elapsed ticks, not a negative duration.
static_assert(timeout_elapsed(4, UINT32_MAX - 4, 9));
static_assert(!timeout_elapsed(4, UINT32_MAX - 4, 10));

esp_bt_uuid_t uuid128(const char* value) {
  return esp32_ble::ESPBTUUID::from_raw(value).get_uuid();
}

}  // namespace

const char* ride_client_state_to_string(RideClientState state) {
  switch (state) {
    case RideClientState::DISCONNECTED:
      return "DISCONNECTED";
    case RideClientState::DISCOVERING:
      return "DISCOVERING";
    case RideClientState::SENDING_HANDSHAKE:
      return "SENDING_HANDSHAKE";
    case RideClientState::SUBSCRIBING_ASYNC:
      return "SUBSCRIBING_ASYNC";
    case RideClientState::SUBSCRIBING_SYNC_TX:
      return "SUBSCRIBING_SYNC_TX";
    case RideClientState::READY:
      return "READY";
    case RideClientState::ERROR:
      return "ERROR";
    default:
      return "UNKNOWN";
  }
}

void RideClient::gattc_event_handler(esp_gattc_cb_event_t event,
                                     esp_gatt_if_t gattc_if,
                                     esp_ble_gattc_cb_param_t* param) {
  if (this->client_ == nullptr || param == nullptr) return;

  switch (event) {
    case ESP_GATTC_CONNECT_EVT:
      if (!this->client_->check_addr(param->connect.remote_bda)) return;
      this->gattc_if_ = gattc_if;
      this->begin_connection_(param->connect.conn_id);
      break;

    case ESP_GATTC_OPEN_EVT:
      if (!this->client_->check_addr(param->open.remote_bda)) return;
      if (param->open.status != ESP_GATT_OK &&
          param->open.status != ESP_GATT_ALREADY_OPEN) {
        this->report_disconnected_();
      } else if (this->state_ == RideClientState::DISCONNECTED) {
        // CONNECT_EVT normally arrives first, but accepting OPEN_EVT makes the
        // helper tolerant of event-order differences across IDF patch releases.
        this->gattc_if_ = gattc_if;
        this->begin_connection_(param->open.conn_id);
      }
      break;

    case ESP_GATTC_SEARCH_CMPL_EVT:
      if (!this->connection_matches_(param->search_cmpl.conn_id)) return;
      if (param->search_cmpl.status != ESP_GATT_OK) {
        this->fail_setup_("service discovery", param->search_cmpl.status);
        return;
      }
      this->gattc_if_ = gattc_if;
      if (!this->discover_handles_(gattc_if, param->search_cmpl.conn_id))
        return;
      this->send_handshake_();
      break;

    case ESP_GATTC_WRITE_CHAR_EVT:
      if (!this->connection_matches_(param->write.conn_id) ||
          param->write.handle != this->sync_rx_handle_)
        return;
      if (this->state_ == RideClientState::SENDING_HANDSHAKE) {
        if (param->write.status != ESP_GATT_OK) {
          this->fail_setup_("RideOn write", param->write.status);
          return;
        }
        this->subscribe_async_();
      } else if (this->haptic_write_pending_) {
        this->haptic_write_pending_ = false;
        this->haptic_write_started_at_ = 0;
        if (param->write.status != ESP_GATT_OK)
          ESP_LOGW(TAG, "Haptic write failed, status=%d", param->write.status);
      }
      break;

    case ESP_GATTC_REG_FOR_NOTIFY_EVT:
      // BLEClientBase runs before BLEClientNode handlers in ESPHome 2026.7.4.
      // For its default V1 connection type it has now submitted the matching
      // CCCD write; wait for WRITE_DESCR_EVT before advancing the sequence.
      if (param->reg_for_notify.handle == this->async_handle_ &&
          this->state_ == RideClientState::SUBSCRIBING_ASYNC) {
        if (param->reg_for_notify.status != ESP_GATT_OK)
          this->fail_setup_("async notification registration",
                            param->reg_for_notify.status);
      } else if (param->reg_for_notify.handle == this->sync_tx_handle_ &&
                 this->state_ == RideClientState::SUBSCRIBING_SYNC_TX &&
                 param->reg_for_notify.status != ESP_GATT_OK) {
        ESP_LOGW(TAG, "Optional Sync-TX registration failed, status=%d",
                 param->reg_for_notify.status);
        this->mark_ready_();
      }
      break;

    case ESP_GATTC_WRITE_DESCR_EVT:
      if (!this->connection_matches_(param->write.conn_id)) return;
      if (param->write.handle == this->async_cccd_handle_ &&
          this->state_ == RideClientState::SUBSCRIBING_ASYNC) {
        if (param->write.status != ESP_GATT_OK) {
          this->fail_setup_("async CCCD write", param->write.status);
          return;
        }
        this->async_enabled_ = true;
        this->subscribe_sync_tx_or_ready_();
      } else if (param->write.handle == this->sync_tx_cccd_handle_ &&
                 this->state_ == RideClientState::SUBSCRIBING_SYNC_TX) {
        if (param->write.status != ESP_GATT_OK)
          ESP_LOGW(TAG, "Optional Sync-TX CCCD write failed, status=%d",
                   param->write.status);
        this->mark_ready_();
      }
      break;

    case ESP_GATTC_NOTIFY_EVT:
      if (!this->connection_matches_(param->notify.conn_id)) return;
      // The value belongs to Bluedroid and is only valid for this callback. No
      // vector/string is constructed here and normal frames are not logged.
      if (param->notify.handle == this->async_handle_ && this->async_enabled_) {
        if (this->listener_ != nullptr)
          this->listener_->on_ride_notification(param->notify.value,
                                                param->notify.value_len);
      } else if (param->notify.handle == this->sync_tx_handle_) {
        if (this->listener_ != nullptr)
          this->listener_->on_ride_sync_response(param->notify.value,
                                                 param->notify.value_len);
      }
      break;

    case ESP_GATTC_DISCONNECT_EVT:
      if (this->client_->check_addr(param->disconnect.remote_bda))
        this->report_disconnected_();
      break;

    case ESP_GATTC_CLOSE_EVT:
      if (this->client_->check_addr(param->close.remote_bda))
        this->report_disconnected_();
      break;

    default:
      break;
  }
}

void RideClient::loop() {
  if (this->client_ == nullptr) return;

  const uint32_t now = millis();

  if (this->haptic_write_pending_ &&
      timeout_elapsed(now, this->haptic_write_started_at_,
                      HAPTIC_WRITE_TIMEOUT_MS)) {
    this->haptic_write_pending_ = false;
    this->haptic_write_started_at_ = 0;
    this->haptic_timeout_count_++;
    ESP_LOGW(TAG, "Haptic completion timed out; write gate released");
  }

  const uint32_t timeout_ms = this->state_timeout_ms_();
  if (timeout_ms == 0 ||
      !timeout_elapsed(now, this->state_started_at_, timeout_ms))
    return;

  const RideClientState stalled_state = this->state_;
  this->setup_timeout_count_++;
  ESP_LOGW(TAG, "Ride setup timed out in %s after %" PRIu32 " ms; reconnecting",
           ride_client_state_to_string(stalled_state), timeout_ms);
  this->fail_setup_("Ride setup watchdog", ESP_GATT_CONN_TIMEOUT);
}

bool RideClient::discover_handles_(esp_gatt_if_t gattc_if, uint16_t conn_id) {
  uint16_t service_start = 0;
  uint16_t service_end = 0;
  if (!this->find_service_(gattc_if, conn_id, service_start, service_end)) {
    this->fail_setup_("FC82 service lookup", ESP_GATT_NOT_FOUND);
    return false;
  }

  esp_gatt_char_prop_t async_properties{};
  if (!this->find_characteristic_(gattc_if, conn_id, service_start, service_end,
                                  ASYNC_UUID, this->async_handle_,
                                  async_properties)) {
    this->fail_setup_("async characteristic lookup", ESP_GATT_NOT_FOUND);
    return false;
  }
  if ((async_properties & ESP_GATT_CHAR_PROP_BIT_NOTIFY) == 0) {
    this->fail_setup_("async characteristic is not notify-capable",
                      ESP_GATT_REQ_NOT_SUPPORTED);
    return false;
  }
  if (!this->find_cccd_(gattc_if, conn_id, this->async_handle_,
                        this->async_cccd_handle_)) {
    this->fail_setup_("async CCCD lookup", ESP_GATT_NOT_FOUND);
    return false;
  }

  esp_gatt_char_prop_t rx_properties{};
  if (!this->find_characteristic_(gattc_if, conn_id, service_start, service_end,
                                  SYNC_RX_UUID, this->sync_rx_handle_,
                                  rx_properties)) {
    this->fail_setup_("Sync-RX characteristic lookup", ESP_GATT_NOT_FOUND);
    return false;
  }
  if ((rx_properties & ESP_GATT_CHAR_PROP_BIT_WRITE_NR) == 0) {
    this->fail_setup_("Sync-RX does not support write-without-response",
                      ESP_GATT_REQ_NOT_SUPPORTED);
    return false;
  }

  // Sync-TX is useful for protocol responses but not required for keypad input.
  esp_gatt_char_prop_t tx_properties{};
  if (this->find_characteristic_(gattc_if, conn_id, service_start, service_end,
                                 SYNC_TX_UUID, this->sync_tx_handle_,
                                 tx_properties)) {
    if ((tx_properties & (ESP_GATT_CHAR_PROP_BIT_NOTIFY |
                          ESP_GATT_CHAR_PROP_BIT_INDICATE)) == 0 ||
        !this->find_cccd_(gattc_if, conn_id, this->sync_tx_handle_,
                          this->sync_tx_cccd_handle_)) {
      ESP_LOGW(TAG,
               "Optional Sync-TX is not subscribable; continuing without it");
      this->sync_tx_handle_ = 0;
      this->sync_tx_cccd_handle_ = 0;
    }
  }

  ESP_LOGD(TAG, "Ride service found (async=0x%04X, rx=0x%04X, tx=0x%04X)",
           this->async_handle_, this->sync_rx_handle_, this->sync_tx_handle_);
  return true;
}

bool RideClient::find_service_(esp_gatt_if_t gattc_if, uint16_t conn_id,
                               uint16_t& start_handle, uint16_t& end_handle) {
  esp_gattc_service_elem_t result{};
  uint16_t count = 1;
  esp_bt_uuid_t uuid =
      esp32_ble::ESPBTUUID::from_uint16(ZWIFT_RIDE_SERVICE_UUID).get_uuid();
  auto status =
      esp_ble_gattc_get_service(gattc_if, conn_id, &uuid, &result, &count, 0);
  if (status != ESP_GATT_OK || count == 0) return false;
  start_handle = result.start_handle;
  end_handle = result.end_handle;
  return true;
}

bool RideClient::find_characteristic_(esp_gatt_if_t gattc_if, uint16_t conn_id,
                                      uint16_t start_handle,
                                      uint16_t end_handle, const char* uuid,
                                      uint16_t& handle,
                                      esp_gatt_char_prop_t& properties) {
  esp_gattc_char_elem_t result{};
  uint16_t count = 1;
  auto status = esp_ble_gattc_get_char_by_uuid(gattc_if, conn_id, start_handle,
                                               end_handle, uuid128(uuid),
                                               &result, &count);
  if (status != ESP_GATT_OK || count == 0) return false;
  handle = result.char_handle;
  properties = result.properties;
  return true;
}

bool RideClient::find_cccd_(esp_gatt_if_t gattc_if, uint16_t conn_id,
                            uint16_t char_handle, uint16_t& descriptor_handle) {
  esp_bt_uuid_t cccd_uuid{};
  cccd_uuid.len = ESP_UUID_LEN_16;
  cccd_uuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
  esp_gattc_descr_elem_t result{};
  uint16_t count = 1;
  auto status = esp_ble_gattc_get_descr_by_char_handle(
      gattc_if, conn_id, char_handle, cccd_uuid, &result, &count);
  if (status != ESP_GATT_OK || count == 0) return false;
  descriptor_handle = result.handle;
  return true;
}

bool RideClient::send_handshake_() {
  this->enter_state_(RideClientState::SENDING_HANDSHAKE);
  auto status = esp_ble_gattc_write_char(
      this->gattc_if_, this->conn_id_, this->sync_rx_handle_, sizeof(RIDE_ON),
      const_cast<uint8_t*>(RIDE_ON), ESP_GATT_WRITE_TYPE_NO_RSP,
      ESP_GATT_AUTH_REQ_NONE);
  if (status != ESP_OK) {
    this->fail_setup_("RideOn submission", status);
    return false;
  }
  return true;
}

bool RideClient::subscribe_async_() {
  this->enter_state_(RideClientState::SUBSCRIBING_ASYNC);
  if (!this->register_for_notify_(this->async_handle_)) {
    this->fail_setup_("async notification submission", ESP_FAIL);
    return false;
  }
  return true;
}

void RideClient::subscribe_sync_tx_or_ready_() {
  if (this->sync_tx_handle_ == 0) {
    this->mark_ready_();
    return;
  }
  this->enter_state_(RideClientState::SUBSCRIBING_SYNC_TX);
  if (!this->register_for_notify_(this->sync_tx_handle_)) {
    ESP_LOGW(TAG,
             "Optional Sync-TX registration submission failed; continuing "
             "without it");
    this->sync_tx_handle_ = 0;
    this->sync_tx_cccd_handle_ = 0;
    this->mark_ready_();
  }
}

bool RideClient::register_for_notify_(uint16_t handle) {
  auto status = esp_ble_gattc_register_for_notify(
      this->gattc_if_, this->client_->get_remote_bda(), handle);
  return status == ESP_OK;
}

void RideClient::mark_ready_() {
  if (!this->async_enabled_ || this->state_ == RideClientState::READY) return;
  this->enter_state_(RideClientState::READY);
  ESP_LOGI(TAG, "Zwift Ride input session ready");
  if (this->listener_ != nullptr) this->listener_->on_ride_ready();
}

bool RideClient::vibrate(uint8_t pattern) {
  if (!this->ready() || this->sync_rx_handle_ == 0 ||
      this->haptic_write_pending_ || pattern > MAX_HAPTIC_PATTERN)
    return false;

  this->haptic_command_[sizeof(this->haptic_command_) - 1] = pattern;
  auto status = esp_ble_gattc_write_char(
      this->gattc_if_, this->conn_id_, this->sync_rx_handle_,
      sizeof(this->haptic_command_), this->haptic_command_,
      ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
  if (status != ESP_OK) {
    ESP_LOGW(TAG, "Haptic write submission failed, status=%d", status);
    return false;
  }
  this->haptic_write_pending_ = true;
  this->haptic_write_started_at_ = millis();
  return true;
}

void RideClient::fail_setup_(const char* operation, int status) {
  if (this->state_ == RideClientState::ERROR ||
      this->state_ == RideClientState::DISCONNECTED)
    return;
  ESP_LOGE(TAG, "%s failed, status=%d", operation, status);
  this->enter_state_(RideClientState::ERROR);
  this->async_enabled_ = false;
  this->haptic_write_pending_ = false;
  this->haptic_write_started_at_ = 0;
  // Force the ESPHome client back through discovery on its next auto-connect;
  // stale handles must never survive a failed setup attempt.
  this->client_->disconnect();
}

void RideClient::begin_connection_(uint16_t conn_id) {
  this->reset_connection_();
  this->conn_id_ = conn_id;
  this->enter_state_(RideClientState::DISCOVERING);
  this->disconnect_reported_ = false;
  ESP_LOGD(TAG, "Zwift Ride GATT link connected; awaiting service discovery");
}

void RideClient::report_disconnected_() {
  const bool report = !this->disconnect_reported_;
  this->reset_connection_();
  this->disconnect_reported_ = true;
  if (report && this->listener_ != nullptr)
    this->listener_->on_ride_disconnected();
}

void RideClient::reset_connection_() {
  this->state_ = RideClientState::DISCONNECTED;
  this->state_started_at_ = 0;
  this->gattc_if_ = ESP_GATT_IF_NONE;
  this->conn_id_ = 0xFFFF;
  this->async_handle_ = 0;
  this->async_cccd_handle_ = 0;
  this->sync_rx_handle_ = 0;
  this->sync_tx_handle_ = 0;
  this->sync_tx_cccd_handle_ = 0;
  this->async_enabled_ = false;
  this->haptic_write_pending_ = false;
  this->haptic_write_started_at_ = 0;
}

void RideClient::enter_state_(RideClientState state) {
  this->state_ = state;
  switch (state) {
    case RideClientState::DISCOVERING:
    case RideClientState::SENDING_HANDSHAKE:
    case RideClientState::SUBSCRIBING_ASYNC:
    case RideClientState::SUBSCRIBING_SYNC_TX:
      this->state_started_at_ = millis();
      break;
    default:
      this->state_started_at_ = 0;
      break;
  }
}

uint32_t RideClient::state_timeout_ms_() const {
  switch (this->state_) {
    case RideClientState::DISCOVERING:
      return DISCOVERY_TIMEOUT_MS;
    case RideClientState::SENDING_HANDSHAKE:
    case RideClientState::SUBSCRIBING_ASYNC:
    case RideClientState::SUBSCRIBING_SYNC_TX:
      return GATT_OPERATION_TIMEOUT_MS;
    default:
      return 0;
  }
}

bool RideClient::connection_matches_(uint16_t conn_id) const {
  return this->conn_id_ != 0xFFFF && this->conn_id_ == conn_id;
}

}  // namespace esphome::zwift_ride_hid

#endif  // USE_ESP32
