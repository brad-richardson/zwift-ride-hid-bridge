// SPDX-License-Identifier: GPL-3.0-only
#include "zwift_ride_hid.h"

#include <cinttypes>
#include <cstdio>
#include <cstring>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::zwift_ride_hid {

namespace {

static const char *const TAG = "zwift_ride_hid";
static constexpr uint32_t HAPTIC_MIN_INTERVAL_MS = 1000;
static constexpr uint32_t DIAGNOSTIC_MAX_RATE_MS = 1000;

bool reports_equal(const KeyboardReport &left, const KeyboardReport &right) {
  return std::memcmp(&left, &right, sizeof(KeyboardReport)) == 0;
}

}  // namespace

void ZwiftRideHid::setup() {
  if (this->ble_parent_ == nullptr || this->parent() == nullptr) {
    ESP_LOGE(TAG, "BLE parents are not configured");
    this->mark_failed();
    return;
  }

  if (this->status_led_ != nullptr) {
    this->status_led_->setup();
    this->status_led_->digital_write(false);
  }

  KeymapProfile profile;
  if (!keymap_profile_from_name(this->profile_name_.c_str(), &profile)) {
    ESP_LOGE(TAG, "Unknown keymap profile: %s", this->profile_name_.c_str());
    this->mark_failed();
    return;
  }
  this->keymap_ = &keymap_for_profile(profile);
  if (!this->input_state_.set_thresholds(this->press_threshold_, this->release_threshold_)) {
    ESP_LOGE(TAG, "Invalid analog thresholds");
    this->mark_failed();
    return;
  }

  this->ride_client_.set_ble_client_parent(this->parent());
  this->ride_client_.set_listener(this);
  this->selected_ride_address_ = this->parent()->get_address();
  this->auto_discover_ride_ = this->selected_ride_address_ == 0;
  this->ride_address_selected_ = !this->auto_discover_ride_;
  this->hid_keyboard_.set_parent(this->ble_parent_);
  if (!this->hid_keyboard_.begin(this->hid_name_)) {
    ESP_LOGE(TAG, "Could not schedule the HID keyboard service");
    this->mark_failed();
    return;
  }

#ifdef USE_OTA_STATE_LISTENER
  ota::get_global_ota_callback()->add_global_state_listener(this);
#endif

  clear_keyboard_report(&this->pending_report_);
  this->published_state_ = BridgeState::STARTING;
  this->diagnostics_dirty_ = true;
  if (this->auto_discover_ride_) {
    ESP_LOGI(TAG,
             "Bridge initialized; auto-discovering Ride Left and waiting for "
             "a HID host");
  } else {
    ESP_LOGI(TAG, "Bridge initialized; using pinned Ride Left %s and waiting "
                  "for a HID host",
             this->parent()->address_str());
  }
}

float ZwiftRideHid::get_setup_priority() const { return setup_priority::AFTER_BLUETOOTH; }

void ZwiftRideHid::loop() {
  if (this->is_failed()) {
    this->update_status_led_();
    return;
  }

  this->ride_client_.loop();
  this->hid_keyboard_.loop();

  const bool hid_ready = this->hid_keyboard_.ready();
  if (hid_ready != this->hid_ready_last_) {
    this->hid_ready_last_ = hid_ready;
    this->diagnostics_dirty_ = true;
    if (hid_ready) {
      ESP_LOGI(TAG, "Encrypted HID keyboard host ready");
      this->queue_current_report_();
    } else {
      ESP_LOGI(TAG, "HID keyboard host disconnected or notifications disabled");
      this->release_all_("HID host unavailable");
    }
  }

  if (!this->ota_active_ && !this->stopped_) {
    this->submit_pending_report_();

    const uint32_t now = millis();
    if ((this->connect_haptic_pending_ || this->button_haptic_pending_) &&
        now - this->last_haptic_ms_ >= HAPTIC_MIN_INTERVAL_MS) {
      // Rate-limit attempts as well as successful writes. A controller/stack
      // error must not turn a pending optional buzz into a main-loop write and
      // log storm.
      this->last_haptic_ms_ = now;
      if (this->ride_client_.vibrate()) {
        this->connect_haptic_pending_ = false;
        this->button_haptic_pending_ = false;
      }
    }
  }

  this->publish_diagnostics_();
  this->update_status_led_();
}

void ZwiftRideHid::dump_config() {
  ESP_LOGCONFIG(TAG, "Zwift Ride HID Bridge:");
  ESP_LOGCONFIG(TAG, "  HID name: %s", this->hid_name_.c_str());
  ESP_LOGCONFIG(TAG, "  Mapping profile: %s", this->profile_name_.c_str());
  ESP_LOGCONFIG(TAG, "  Lever thresholds: press=%u, release=%u", this->press_threshold_,
                this->release_threshold_);
  ESP_LOGCONFIG(TAG, "  Publish raw lever values: %s", YESNO(this->expose_raw_));
  ESP_LOGCONFIG(TAG, "  Connect haptic: %s", YESNO(this->connect_haptic_));
  ESP_LOGCONFIG(TAG, "  Button haptic: %s", YESNO(this->button_haptic_));
  ESP_LOGCONFIG(TAG, "  Debug capture: %s", YESNO(this->debug_capture_));
  ESP_LOGCONFIG(TAG, "  Ride selection: %s",
                this->auto_discover_ride_ ? "automatic" : "pinned address");
  if (this->ride_address_selected_) {
    ESP_LOGCONFIG(TAG, "  Selected Ride Left: %s", this->parent()->address_str());
  }
  LOG_PIN("  Status LED: ", this->status_led_);
  ESP_LOGCONFIG(TAG, "  Runtime state: %s", bridge_state_name_(this->bridge_state_()));
}

bool ZwiftRideHid::parse_device(
    const esp32_ble_tracker::ESPBTDevice &device) {
  if (!this->auto_discover_ride_ || this->ride_address_selected_ ||
      this->ota_active_ || this->stopped_ || this->parent() == nullptr ||
      this->parent()->state() != esp32_ble_tracker::ClientState::IDLE)
    return false;

  const auto ride_service =
      esp32_ble::ESPBTUUID::from_uint16(kZwiftRideServiceUuid);
  bool advertises_ride_service = false;
  for (const auto &uuid : device.get_service_uuids()) {
    if (uuid == ride_service) {
      advertises_ride_service = true;
      break;
    }
  }
  bool is_ride_left = false;
  size_t manufacturer_payload_length = 0;
  for (const auto &manufacturer_data : device.get_manufacturer_datas()) {
    const auto uuid = manufacturer_data.uuid.get_uuid();
    if (uuid.len != ESP_UUID_LEN_16)
      continue;
    if (is_zwift_ride_left_advertisement(
            advertises_ride_service, uuid.uuid.uuid16,
            manufacturer_data.data.data(), manufacturer_data.data.size())) {
      is_ride_left = true;
      manufacturer_payload_length = manufacturer_data.data.size();
      break;
    }
  }
  if (!is_ride_left)
    return false;

  const uint64_t discovered_address = device.address_uint64();
  if (discovered_address == 0)
    return false;

  // Parsed-advertisement listeners run before clients in ESPHome 2026.7.4.
  // Selecting the address here lets the stock BLEClient consume this same
  // scan result, transition to DISCOVERED, and retain ownership of scan stop,
  // connection, GATT callbacks, and reconnect behavior.
  this->selected_ride_address_ = discovered_address;
  this->parent()->set_address(this->selected_ride_address_);
  this->parent()->set_remote_addr_type(device.get_address_type());
  this->ride_address_selected_ = true;

  char address[MAC_ADDRESS_PRETTY_BUFFER_SIZE];
  ESP_LOGI(TAG, "Auto-discovered Ride Left %s (name='%s', RSSI=%d)",
           device.address_str_to(address), device.get_name().c_str(),
           device.get_rssi());
  if (manufacturer_payload_length !=
      kZwiftRideManufacturerPayloadLength) {
    ESP_LOGW(TAG,
             "Ride Left manufacturer payload has unexpected length %u "
             "(expected %u); accepting the known device ID",
             static_cast<unsigned>(manufacturer_payload_length),
             static_cast<unsigned>(kZwiftRideManufacturerPayloadLength));
  }
  return true;
}

void ZwiftRideHid::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                       esp_ble_gattc_cb_param_t *param) {
  this->ride_client_.gattc_event_handler(event, gattc_if, param);
  // A new, address-validated Ride connection is the only event that reopens
  // input after an OTA/shutdown quiesce. This prevents a late completion from
  // the old session being accepted after an OTA abort.
  if ((event == ESP_GATTC_CONNECT_EVT || event == ESP_GATTC_OPEN_EVT) &&
      this->ride_client_.state() == RideClientState::DISCOVERING &&
      !this->ota_active_ && !this->stopped_) {
    this->ride_session_quiesced_ = false;
  }
}

void ZwiftRideHid::gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
  // BLEClient forwards ESPHome's single broker callback here. This is the HID
  // server's only GAP subscription, so the global callback is never replaced.
  this->hid_keyboard_.gap_event_handler(event, param);
}

void ZwiftRideHid::gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                       esp_ble_gatts_cb_param_t *param) {
  this->hid_keyboard_.gatts_event_handler(event, gatts_if, param);
}

void ZwiftRideHid::ble_before_disabled_event_handler() {
  this->ride_session_quiesced_ = true;
  this->release_all_("BLE stack stopping");
  this->hid_keyboard_.ble_before_disabled_event_handler();
  this->ride_ready_ = false;
  this->diagnostics_dirty_ = true;
}

void ZwiftRideHid::on_ride_ready() {
  if (this->ota_active_ || this->stopped_ || this->ride_session_quiesced_)
    return;
  this->ride_ready_ = true;
  this->node_state = esp32_ble_tracker::ClientState::ESTABLISHED;
  if (this->ever_ride_ready_) {
    this->reconnect_count_++;
  } else {
    this->ever_ride_ready_ = true;
  }
  this->connect_haptic_pending_ = this->connect_haptic_;
  this->diagnostics_dirty_ = true;
  ESP_LOGI(TAG, "Ride Left handshake and input subscription ready");
}

void ZwiftRideHid::on_ride_disconnected() {
  this->ride_session_quiesced_ = true;
  this->ride_ready_ = false;
  this->connect_haptic_pending_ = false;
  this->button_haptic_pending_ = false;
  this->release_all_("Ride disconnected");
  this->diagnostics_dirty_ = true;
  ESP_LOGW(TAG, "Ride Left disconnected; all keyboard keys released");
}

void ZwiftRideHid::on_ride_notification(const uint8_t *data, uint16_t length) {
  // Disconnect is asynchronous. Ignore any Ride events already queued when
  // OTA or shutdown quiesces the bridge so they cannot restore held state.
  if (this->ota_active_ || this->stopped_ || this->ride_session_quiesced_ ||
      !this->ride_ready_)
    return;
  RideInputPacket packet{};
  const auto status = decode_ride_notification(data, length, &packet);
  if (status == RideDecodeStatus::NOT_INPUT_PACKET) {
    return;
  }
  if (status != RideDecodeStatus::OK) {
    this->invalid_frame_count_++;
    this->diagnostics_dirty_ = true;
    ESP_LOGW(TAG, "Rejected Ride input notification: %s (length=%u)", ride_decode_status_name(status),
             length);
    return;
  }

  if (this->debug_capture_) {
    this->log_capture_(data, length, packet);
  }

  const InputTransitions transitions = this->input_state_.apply(packet);
  if (transitions.changed()) {
    this->queue_current_report_();
    if (this->button_haptic_ && transitions.pressed != 0) {
      this->button_haptic_pending_ = true;
    }
  }
  if (packet.analog_present_mask != 0) {
    this->diagnostics_dirty_ = true;
  }
}

void ZwiftRideHid::on_ride_sync_response(const uint8_t *data, uint16_t length) {
  if (this->ota_active_ || this->stopped_ || !this->debug_capture_)
    return;
  ESP_LOGD(TAG, "Ride Sync-TX response (%u bytes, first=0x%02X)", length,
           length == 0 || data == nullptr ? 0 : data[0]);
}

void ZwiftRideHid::queue_current_report_() {
  if (this->keymap_ == nullptr)
    return;
  KeyboardReport next{};
  const auto status = build_keyboard_report(this->input_state_.active_actions(), *this->keymap_, &next);
  if (status == KeyboardReportStatus::NULL_ARGUMENT) {
    ESP_LOGE(TAG, "Could not build HID keyboard report");
    return;
  }
  if (status == KeyboardReportStatus::SIX_KEY_ROLLOVER) {
    ESP_LOGW(TAG, "More than six non-modifier keys are active; sending HID ErrorRollOver");
  }
  if (!this->report_pending_ || !reports_equal(next, this->pending_report_)) {
    this->pending_report_ = next;
    this->report_pending_ = true;
  }
}

bool ZwiftRideHid::submit_pending_report_() {
  if (!this->report_pending_ || !this->hid_keyboard_.ready())
    return false;
  if (!this->hid_keyboard_.send_report(this->pending_report_.modifiers, this->pending_report_.keys,
                                       kKeyboardReportKeyCount)) {
    return false;
  }
  this->report_pending_ = false;
  this->hid_report_count_++;
  this->diagnostics_dirty_ = true;
  return true;
}

void ZwiftRideHid::release_all_(const char *reason) {
  const bool had_actions = this->input_state_.active_actions() != 0;
  this->input_state_.release_all();
  clear_keyboard_report(&this->pending_report_);
  this->report_pending_ = true;
  // Always overwrite the HID helper's buffered report, even if the link or
  // CCCD has already disappeared. Otherwise a quick reconnect could replay
  // the last non-empty report before the bridge observes that teardown.
  if (this->hid_keyboard_.release_all()) {
    this->report_pending_ = false;
    this->hid_report_count_++;
  }
  if (had_actions) {
    ESP_LOGD(TAG, "Released all keyboard state: %s", reason);
  }
  this->diagnostics_dirty_ = true;
}

void ZwiftRideHid::quiesce_(const char *reason, bool disconnect_ride) {
  this->ride_session_quiesced_ = true;
  this->release_all_(reason);
  this->hid_keyboard_.quiesce();
  // Treat the current Ride session as dead immediately. The physical
  // disconnect is asynchronous, and an aborted OTA must wait for a fresh
  // handshake rather than accepting notifications queued by the old link.
  this->ride_ready_ = false;
  this->connect_haptic_pending_ = false;
  this->button_haptic_pending_ = false;
  if (disconnect_ride && this->parent() != nullptr) {
    this->parent()->disconnect();
  }
  this->diagnostics_dirty_ = true;
}

void ZwiftRideHid::on_shutdown() {
  this->stopped_ = true;
  this->quiesce_("shutdown", true);
  if (this->status_led_ != nullptr)
    this->status_led_->digital_write(false);
}

void ZwiftRideHid::on_safe_shutdown() {
  this->stopped_ = true;
  this->quiesce_("safe shutdown", true);
  if (this->status_led_ != nullptr)
    this->status_led_->digital_write(false);
}

#ifdef USE_OTA_STATE_LISTENER
void ZwiftRideHid::on_ota_global_state(ota::OTAState state, float progress, uint8_t error,
                                       ota::OTAComponent *component) {
  (void) progress;
  (void) error;
  (void) component;
  if (state == ota::OTA_STARTED) {
    this->ota_active_ = true;
    this->quiesce_("OTA started", true);
    ESP_LOGI(TAG, "BLE bridge quiesced for OTA");
  } else if (state == ota::OTA_ERROR || state == ota::OTA_ABORT) {
    this->ota_active_ = false;
    this->hid_keyboard_.resume();
    this->diagnostics_dirty_ = true;
    ESP_LOGW(TAG, "OTA stopped before completion; BLE bridge resumed");
  }
}
#endif

ZwiftRideHid::BridgeState ZwiftRideHid::bridge_state_() const {
  if (this->is_failed() || this->hid_keyboard_.failed() ||
      this->ride_client_.state() == RideClientState::ERROR)
    return BridgeState::ERROR;
  if (this->ota_active_)
    return BridgeState::OTA;
  if (this->stopped_)
    return BridgeState::STOPPED;
  if (this->ride_ready_ && this->hid_ready_last_)
    return BridgeState::READY;
  if (this->ride_ready_)
    return BridgeState::RIDE_READY;
  if (this->hid_ready_last_)
    return BridgeState::HID_READY;
  if (this->hid_keyboard_.service_ready())
    return BridgeState::SCANNING;
  return BridgeState::STARTING;
}

const char *ZwiftRideHid::bridge_state_name_(BridgeState state) {
  switch (state) {
    case BridgeState::STARTING:
      return "starting";
    case BridgeState::SCANNING:
      return "scanning";
    case BridgeState::RIDE_READY:
      return "ride_ready_waiting_for_hid";
    case BridgeState::HID_READY:
      return "hid_ready_waiting_for_ride";
    case BridgeState::READY:
      return "ready";
    case BridgeState::OTA:
      return "ota";
    case BridgeState::ERROR:
      return "error";
    case BridgeState::STOPPED:
      return "stopped";
    default:
      return "unknown";
  }
}

void ZwiftRideHid::publish_diagnostics_(bool force) {
  const uint32_t now = millis();
  const BridgeState state = this->bridge_state_();
  if (!force && !this->diagnostics_dirty_ && state == this->published_state_)
    return;
  if (!force && now - this->last_diagnostics_ms_ < DIAGNOSTIC_MAX_RATE_MS)
    return;

  if (this->ride_connected_sensor_ != nullptr)
    this->ride_connected_sensor_->publish_state(this->ride_ready_);
  if (this->hid_connected_sensor_ != nullptr)
    this->hid_connected_sensor_->publish_state(this->hid_ready_last_);
  if (this->ready_sensor_ != nullptr)
    this->ready_sensor_->publish_state(state == BridgeState::READY);
  if (this->state_text_sensor_ != nullptr)
    this->state_text_sensor_->publish_state(bridge_state_name_(state));
  if (this->reconnect_count_sensor_ != nullptr)
    this->reconnect_count_sensor_->publish_state(this->reconnect_count_);
  if (this->invalid_frame_count_sensor_ != nullptr)
    this->invalid_frame_count_sensor_->publish_state(this->invalid_frame_count_);
  if (this->hid_report_count_sensor_ != nullptr)
    this->hid_report_count_sensor_->publish_state(this->hid_report_count_);
  if (this->expose_raw_) {
    if (this->left_lever_sensor_ != nullptr && this->input_state_.has_analog(0))
      this->left_lever_sensor_->publish_state(this->input_state_.analog(0));
    if (this->right_lever_sensor_ != nullptr && this->input_state_.has_analog(1))
      this->right_lever_sensor_->publish_state(this->input_state_.analog(1));
  }

  this->published_state_ = state;
  this->last_diagnostics_ms_ = now;
  this->diagnostics_dirty_ = false;
}

void ZwiftRideHid::update_status_led_() {
  if (this->status_led_ == nullptr)
    return;
  const uint32_t now = millis();
  bool on = false;
  switch (this->bridge_state_()) {
    case BridgeState::READY:
      on = true;
      break;
    case BridgeState::RIDE_READY: {
      const uint32_t phase = now % 2000U;
      on = phase < 100U || (phase >= 250U && phase < 350U);
      break;
    }
    case BridgeState::OTA:
      on = now % 200U < 100U;
      break;
    case BridgeState::ERROR: {
      const uint32_t phase = now % 2000U;
      on = phase < 100U || (phase >= 250U && phase < 350U) ||
           (phase >= 500U && phase < 600U);
      break;
    }
    case BridgeState::STOPPED:
      on = false;
      break;
    case BridgeState::STARTING:
    case BridgeState::SCANNING:
    case BridgeState::HID_READY:
      on = now % 1500U < 200U;
      break;
  }
  this->status_led_->digital_write(on);
}

void ZwiftRideHid::log_capture_(const uint8_t *data, size_t length,
                                const RideInputPacket &packet) const {
  if (data == nullptr)
    return;
  char hex[kMaxRideNotificationLength * 3 + 1]{};
  const size_t bounded_length = length > kMaxRideNotificationLength ? kMaxRideNotificationLength : length;
  size_t cursor = 0;
  for (size_t i = 0; i < bounded_length && cursor + 3 < sizeof(hex); i++) {
    const int written = std::snprintf(hex + cursor, sizeof(hex) - cursor, "%02X%s", data[i],
                                      i + 1 == bounded_length ? "" : " ");
    if (written <= 0)
      break;
    cursor += static_cast<size_t>(written);
  }
  ESP_LOGD(TAG, "Ride 0x23 mask=0x%06" PRIX32 " analog=[%" PRId32 ",%" PRId32 ",%" PRId32
                ",%" PRId32 "] present=0x%02X raw=%s",
           packet.pressed_buttons, packet.analog[0], packet.analog[1], packet.analog[2], packet.analog[3],
           packet.analog_present_mask, hex);
}

}  // namespace esphome::zwift_ride_hid
