// SPDX-License-Identifier: GPL-3.0-only
#include "zwift_ride_hid.h"

#include <cinttypes>
#include <cstdio>
#include <cstring>

#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::zwift_ride_hid {

namespace {

static const char *const TAG = "zwift_ride_hid";
static constexpr uint32_t HAPTIC_MIN_INTERVAL_MS = 1000;
static constexpr uint32_t DIAGNOSTIC_MAX_RATE_MS = 1000;
static constexpr uint32_t ADVERTISEMENT_AGE_REFRESH_MS = 5000;

bool reports_equal(const KeyboardReport &left, const KeyboardReport &right) {
  return std::memcmp(&left, &right, sizeof(KeyboardReport)) == 0;
}

}  // namespace

void ZwiftRideHid::setup() {
  if (this->ble_parent_ == nullptr || this->ble_tracker_ == nullptr ||
      this->parent() == nullptr) {
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
  this->ble_tracker_->add_scanner_state_listener(this);
  this->selected_ride_address_ = this->parent()->get_address();
  this->auto_discover_ride_ = this->selected_ride_address_ == 0;
  this->ride_address_selected_ = !this->auto_discover_ride_;
  this->selected_ride_address_type_ = this->parent()->get_remote_addr_type();
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
  // This one override fills two identical virtual slots: Component::loop() and
  // BLEClientNode::loop(). ESPHome's application loop calls the first, and
  // BLEClient::loop() calls the second for every registered node, so the body
  // would run twice per iteration whenever the Ride client is not idle.
  // Upstream BLE client nodes dodge this by leaving loop() empty; this
  // component needs a real one, so it only runs from its own component slot.
  // App has no current component before the main loop starts, and skipping the
  // setup-phase calls would only delay HID service construction by one
  // iteration, so a null current component is treated as ours.
  const Component *current_component = App.get_current_component();
  if (current_component != nullptr && current_component != this)
    return;

  if (this->is_failed()) {
    this->update_status_led_();
    return;
  }

  this->ensure_ride_address_();
  this->update_idle_policy_();
  this->update_scanner_policy_();
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

  // Watchdog trips are rare and raised inside RideClient, which has no
  // diagnostics callback. Both counters only ever increase, so one comparison
  // is enough to schedule a publish.
  const uint32_t ride_timeouts = this->ride_client_.setup_timeout_count() +
                                 this->ride_client_.haptic_timeout_count();
  if (ride_timeouts != this->published_ride_timeouts_) {
    this->published_ride_timeouts_ = ride_timeouts;
    this->diagnostics_dirty_ = true;
  }

  const uint32_t diagnostics_now = millis();
  const bool advertising_now =
      this->ride_ready_ || this->idle_policy_.advertising(diagnostics_now);
  if (advertising_now != this->advertising_published_) {
    this->advertising_published_ = advertising_now;
    this->diagnostics_dirty_ = true;
  }
  // The advertisement age only changes meaning while the bridge is waiting for
  // the controllers to sleep, so it is refreshed on a slow cadence there and
  // left to ordinary change-driven publishing everywhere else.
  if (this->ride_idle_suppressed_ &&
      diagnostics_now - this->last_diagnostics_ms_ >=
          ADVERTISEMENT_AGE_REFRESH_MS) {
    this->diagnostics_dirty_ = true;
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
  ESP_LOGCONFIG(TAG,
                "  Scanner policy: continuous only while Ride client is idle");
  const auto &idle = this->idle_policy_.config();
  if (idle.idle_timeout_ms == 0) {
    ESP_LOGCONFIG(TAG, "  Idle disconnect: disabled");
  } else {
    ESP_LOGCONFIG(TAG,
                  "  Idle disconnect: after %" PRIu32 " s without input\n"
                  "  Sleep confirmation: %" PRIu32 " s without advertisements\n"
                  "  Maximum suppression: %" PRIu32 " s",
                  idle.idle_timeout_ms / 1000U, idle.sleep_confirm_ms / 1000U,
                  idle.max_suppression_ms / 1000U);
    ESP_LOGCONFIG(TAG, "  Release HID host when idle: %s",
                  YESNO(this->release_hid_when_idle_));
  }
  ESP_LOGCONFIG(TAG, "  Advertisement capture: %s",
                YESNO(this->debug_advertisements_));
  if (this->ride_address_selected_) {
    ESP_LOGCONFIG(TAG, "  Selected Ride Left: %s", this->parent()->address_str());
  }
  LOG_PIN("  Status LED: ", this->status_led_);
  ESP_LOGCONFIG(TAG, "  Runtime state: %s", bridge_state_name_(this->bridge_state_()));
}

bool ZwiftRideHid::parse_device(
    const esp32_ble_tracker::ESPBTDevice &device) {
  if (this->ota_active_ || this->stopped_ || this->parent() == nullptr)
    return false;

  const uint64_t advertised_address = device.address_uint64();
  if (advertised_address == 0)
    return false;

  // Once an address is locked — by automatic selection or a pinned YAML MAC —
  // the only remaining interest in advertisements is tracking whether the
  // controller is awake, which drives the idle-suppression re-arm.
  if (this->ride_address_selected_) {
    if (advertised_address != this->selected_ride_address_)
      return false;
    if (this->debug_advertisements_) {
      const uint8_t *payload = nullptr;
      size_t payload_length = 0;
      for (const auto &manufacturer_data : device.get_manufacturer_datas()) {
        const auto uuid = manufacturer_data.uuid.get_uuid();
        if (uuid.len == ESP_UUID_LEN_16 && uuid.uuid.uuid16 == kZwiftCompanyId) {
          payload = manufacturer_data.data.data();
          payload_length = manufacturer_data.data.size();
          break;
        }
      }
      this->log_advertisement_(device, payload, payload_length);
    }
    return this->note_ride_advertisement_();
  }

  if (!this->auto_discover_ride_ ||
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

  // Parsed-advertisement listeners run before clients in ESPHome 2026.7.4.
  // Selecting the address here lets the stock BLEClient consume this same
  // scan result, transition to DISCOVERED, and retain ownership of scan stop,
  // connection, GATT callbacks, and reconnect behavior.
  this->selected_ride_address_ = advertised_address;
  this->selected_ride_address_type_ = device.get_address_type();
  this->parent()->set_address(this->selected_ride_address_);
  this->parent()->set_remote_addr_type(this->selected_ride_address_type_);
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

void ZwiftRideHid::log_advertisement_(
    const esp32_ble_tracker::ESPBTDevice &device,
    const uint8_t *manufacturer_payload, size_t manufacturer_payload_length) {
  const uint32_t now = millis();
  // Interval between sightings is the number that decides whether a short
  // sleep_confirmation can ever be safe, so it is logged alongside the payload.
  const uint32_t since_previous =
      this->last_logged_advertisement_ms_ == 0
          ? 0
          : static_cast<uint32_t>(now - this->last_logged_advertisement_ms_);
  this->last_logged_advertisement_ms_ = now;

  char payload_hex[kZwiftRideManufacturerPayloadLength * 3 + 16]{};
  size_t cursor = 0;
  for (size_t i = 0; i < manufacturer_payload_length &&
                     cursor + 3 < sizeof(payload_hex);
       i++) {
    const int written =
        std::snprintf(payload_hex + cursor, sizeof(payload_hex) - cursor,
                      "%02X%s", manufacturer_payload[i],
                      i + 1 == manufacturer_payload_length ? "" : " ");
    if (written <= 0)
      break;
    cursor += static_cast<size_t>(written);
  }

  const auto &scan_result = device.get_scan_result();
  ESP_LOGD(TAG,
           "Ride adv +%" PRIu32 " ms rssi=%d flags=0x%02X adv_len=%u "
           "rsp_len=%u evt=%u mfg=[%s] name='%s'",
           since_previous, device.get_rssi(),
           device.get_ad_flag().has_value()
               ? static_cast<unsigned>(device.get_ad_flag().value())
               : 0xFFU,
           static_cast<unsigned>(scan_result.adv_data_len),
           static_cast<unsigned>(scan_result.scan_rsp_len),
           static_cast<unsigned>(scan_result.search_evt), payload_hex,
           device.get_name().c_str());
}

bool ZwiftRideHid::note_ride_advertisement_() {
  if (!this->idle_policy_.on_advertisement(millis()))
    return false;

  // The controller stopped advertising long enough to count as asleep and has
  // now woken up. Re-enabling here, before the stock client sees this same scan
  // result, lets it connect on this advertisement instead of the next one.
  this->end_suppression_("Ride Left woke and advertised again");
  return true;
}

void ZwiftRideHid::end_suppression_(const char *reason) {
  this->ride_idle_suppressed_ = false;
  this->sleep_confirmed_logged_ = false;
  if (this->parent() != nullptr)
    this->parent()->set_enabled(true);
  // resume() only clears the quiesce gate; advertising stays shut until
  // on_ride_ready() re-opens it after a fresh handshake.
  if (this->release_hid_when_idle_)
    this->hid_keyboard_.resume();
  this->diagnostics_dirty_ = true;
  ESP_LOGI(TAG, "%s; reconnecting", reason);
}

void ZwiftRideHid::request_reconnect() {
  if (!this->ride_idle_suppressed_) {
    ESP_LOGD(TAG, "Reconnect requested while not idle-suppressed; ignoring");
    return;
  }
  this->idle_policy_.request_reconnect(millis());
  this->end_suppression_("Reconnect requested");
}

void ZwiftRideHid::ensure_ride_address_() {
  if (!this->ride_address_selected_ || this->selected_ride_address_ == 0 ||
      this->parent() == nullptr ||
      this->parent()->get_address() == this->selected_ride_address_)
    return;

  // ESPHome clears the client address when it disconnects a client that is
  // still in DISCOVERED. Because automatic selection only runs once per boot,
  // losing the address would strand the bridge scanning until a reboot.
  ESP_LOGW(TAG, "Restoring the selected Ride Left address on the BLE client");
  this->parent()->set_address(this->selected_ride_address_);
  this->parent()->set_remote_addr_type(this->selected_ride_address_type_);
}

void ZwiftRideHid::update_idle_policy_() {
  if (this->parent() == nullptr || this->ota_active_ || this->stopped_)
    return;

  const uint32_t now = millis();
  // A held control is continuous use even though it produces no transitions.
  if (this->ride_ready_ && this->input_state_.active_actions() != 0)
    this->idle_policy_.on_activity(now);

  switch (this->idle_policy_.poll(now, this->ride_ready_)) {
    case RideIdleAction::DISCONNECT:
      this->ride_idle_suppressed_ = true;
      // Disconnecting is asynchronous, so close the input path immediately:
      // on_ride_disconnected() repeats this once the link actually goes away.
      this->ride_session_quiesced_ = true;
      this->ride_ready_ = false;
      this->connect_haptic_pending_ = false;
      this->button_haptic_pending_ = false;
      this->release_all_("Ride idle timeout");
      if (this->release_hid_when_idle_) {
        // A connected keyboard makes iPadOS hide its on-screen keyboard, so an
        // idle bridge would quietly cost the iPad its software keyboard for
        // hours. Releasing the bonded host avoids that; it reconnects
        // automatically once advertising resumes after a fresh handshake.
        this->hid_keyboard_.quiesce();
      } else {
        this->hid_keyboard_.set_advertising_allowed(false);
      }
      // set_enabled(false) disconnects and, unlike a bare disconnect(), also
      // stops auto-connect from immediately reclaiming the still-awake
      // controller.
      this->parent()->set_enabled(false);
      this->sleep_confirmed_logged_ = false;
      this->diagnostics_dirty_ = true;
      ESP_LOGI(TAG,
               "No Ride input for %" PRIu32
               " s; disconnecting so the controllers can sleep",
               this->idle_policy_.config().idle_timeout_ms / 1000U);
      break;

    case RideIdleAction::REARM:
      ESP_LOGW(TAG,
               "Ride Left never stopped advertising within the %" PRIu32
               " s suppression window",
               this->idle_policy_.config().max_suppression_ms / 1000U);
      this->end_suppression_("Suppression cap reached");
      break;

    case RideIdleAction::NONE:
      break;
  }

  // Announce the moment the bridge becomes willing to reconnect. Without this
  // line, "nothing happened" is indistinguishable from a broken re-arm.
  if (this->ride_idle_suppressed_ && this->idle_policy_.sleep_confirmed() &&
      !this->sleep_confirmed_logged_) {
    this->sleep_confirmed_logged_ = true;
    this->diagnostics_dirty_ = true;
    ESP_LOGI(TAG,
             "Ride Left stopped advertising for %" PRIu32
             " s; armed to reconnect on its next advertisement",
             this->idle_policy_.config().sleep_confirm_ms / 1000U);
  }
}

void ZwiftRideHid::update_scanner_policy_() {
  if (this->ble_tracker_ == nullptr || this->parent() == nullptr ||
      this->ble_parent_ == nullptr || !this->ble_parent_->is_active())
    return;

  const bool scan_wanted = this->scanner_wanted_();
  this->ble_tracker_->set_scan_continuous(scan_wanted);

  const auto scanner_state = this->ble_tracker_->get_scanner_state();
  if (scan_wanted) {
    // Direct start is safe only after the stock Ride client has completely
    // returned to IDLE. This also resumes an OTA abort when no later client
    // state transition remains to wake ESPHome's scanner state machine.
    if (scanner_state == esp32_ble_tracker::ScannerState::IDLE)
      this->ble_tracker_->start_scan();
    return;
  }

  // stop_scan() also clears ESPHome's continuous flag. If a start command is
  // already in flight, leave STARTING alone; on_scanner_state() stops the late
  // RUNNING transition. Likewise, STOPPING is allowed to finish before an
  // IDLE client can request another scan.
  if (scanner_state == esp32_ble_tracker::ScannerState::RUNNING ||
      scanner_state == esp32_ble_tracker::ScannerState::FAILED)
    this->ble_tracker_->stop_scan();
}

bool ZwiftRideHid::scanner_wanted_() {
  return !this->ota_active_ && !this->stopped_ &&
         this->parent() != nullptr &&
         this->parent()->state() == esp32_ble_tracker::ClientState::IDLE;
}

void ZwiftRideHid::on_scanner_state(
    esp32_ble_tracker::ScannerState state) {
  if (state != esp32_ble_tracker::ScannerState::RUNNING ||
      this->ble_tracker_ == nullptr || this->ble_parent_ == nullptr ||
      !this->ble_parent_->is_active() || this->scanner_wanted_())
    return;

  // A scan-start command may already be in flight when OTA/shutdown or a Ride
  // connection makes scanning undesirable. Stopping from the confirmed
  // RUNNING transition closes that race even if the normal component loop is
  // paused by the update path.
  this->ble_tracker_->stop_scan();
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
  this->ride_idle_suppressed_ = false;
  this->idle_policy_.reset(millis());
  this->diagnostics_dirty_ = true;
}

void ZwiftRideHid::on_ride_ready() {
  if (this->ota_active_ || this->stopped_ || this->ride_session_quiesced_)
    return;
  this->ride_ready_ = true;
  this->ride_idle_suppressed_ = false;
  this->idle_policy_.on_session_ready(millis());
  this->node_state = esp32_ble_tracker::ClientState::ESTABLISHED;
  this->hid_keyboard_.set_advertising_allowed(true);
  if (this->ever_ride_ready_) {
    this->reconnect_count_++;
  } else {
    this->ever_ride_ready_ = true;
  }
  this->connect_haptic_pending_ = this->connect_haptic_;
  this->diagnostics_dirty_ = true;
  ESP_LOGI(TAG,
           "Ride Left handshake and input subscription ready; HID advertising "
           "enabled");
}

void ZwiftRideHid::on_ride_disconnected() {
  this->ride_session_quiesced_ = true;
  this->ride_ready_ = false;
  this->connect_haptic_pending_ = false;
  this->button_haptic_pending_ = false;
  this->release_all_("Ride disconnected");
  this->hid_keyboard_.set_advertising_allowed(false);
  this->diagnostics_dirty_ = true;
  if (this->ride_idle_suppressed_) {
    ESP_LOGI(TAG,
             "Ride Left released after the idle timeout; waiting for the "
             "controllers to sleep before reconnecting");
  } else {
    ESP_LOGW(TAG,
             "Ride Left disconnected; all keyboard keys released and HID "
             "advertising disabled");
  }
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
    // Only real press/release edges count as use. The controllers notify
    // continuously, and lever noise below the press threshold is not a rider.
    this->idle_policy_.on_activity(millis());
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
  // OTA and shutdown take over the Ride link. Dropping suppression here means
  // an aborted update can never resume into a bridge that is still refusing to
  // reconnect; update_idle_policy_() re-arms the timer on the next handshake.
  this->ride_idle_suppressed_ = false;
  this->idle_policy_.reset(millis());
  // OTA and shutdown set their flags before entering here. Submit the scanner
  // stop synchronously; on_scanner_state() catches an in-flight STARTING race.
  this->update_scanner_policy_();
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
    // An idle disconnect may have disabled the client before the update began.
    // quiesce_() already cleared suppression, so restore the client itself.
    if (this->parent() != nullptr)
      this->parent()->set_enabled(true);
    this->diagnostics_dirty_ = true;
    ESP_LOGW(TAG,
             "OTA stopped before completion; Ride scanning will resume after "
             "the client returns to idle, with HID advertising gated until a "
             "fresh handshake");
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
  if (this->ride_idle_suppressed_)
    return BridgeState::RIDE_IDLE;
  if (this->ride_client_.state() != RideClientState::DISCONNECTED)
    return BridgeState::CONNECTING;
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
    case BridgeState::CONNECTING:
      return "connecting";
    case BridgeState::RIDE_IDLE:
      return "ride_idle_sleeping";
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

void ZwiftRideHid::publish_diagnostics_() {
  const uint32_t now = millis();
  const BridgeState state = this->bridge_state_();
  if (!this->diagnostics_dirty_ && state == this->published_state_)
    return;
  if (now - this->last_diagnostics_ms_ < DIAGNOSTIC_MAX_RATE_MS)
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
  if (this->idle_disconnect_count_sensor_ != nullptr)
    this->idle_disconnect_count_sensor_->publish_state(
        this->idle_policy_.idle_disconnect_count());
  if (this->setup_timeout_count_sensor_ != nullptr)
    this->setup_timeout_count_sensor_->publish_state(
        this->ride_client_.setup_timeout_count());
  if (this->haptic_timeout_count_sensor_ != nullptr)
    this->haptic_timeout_count_sensor_->publish_state(
        this->ride_client_.haptic_timeout_count());
  if (this->ride_advertising_sensor_ != nullptr) {
    // A live session is proof the controller is present, and the scanner is
    // deliberately off while connected, so reporting the raw "seen recently"
    // value there would read as a fault during a perfectly healthy session.
    this->ride_advertising_sensor_->publish_state(this->ride_ready_ ||
                                                  this->idle_policy_.advertising(now));
  }
  if (this->advertisement_age_sensor_ != nullptr &&
      this->idle_policy_.has_advertisement())
    this->advertisement_age_sensor_->publish_state(
        this->idle_policy_.advertisement_age_ms(now) / 1000.0f);
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
    case BridgeState::CONNECTING:
      on = now % 500U < 100U;
      break;
    case BridgeState::RIDE_IDLE:
      // A rare, brief flash: deliberately asleep rather than searching.
      on = now % 5000U < 100U;
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
