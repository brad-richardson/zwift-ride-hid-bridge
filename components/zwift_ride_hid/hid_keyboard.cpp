// SPDX-License-Identifier: GPL-3.0-only
#include "hid_keyboard.h"

#include <algorithm>
#include <cstring>

#include <esp_bt_defs.h>
#include <esp_err.h>

#include "esphome/components/esp32_ble/ble_uuid.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::zwift_ride_hid {
namespace {

static const char *const TAG = "zwift_ride_hid.keyboard";

// The keyboard report descriptor and GATT layout are reduced, modified ports
// from markusg1234/ESPHome-espidf_ble_keyboard at commit
// 21274b03dd424927e35cd67bbb7c9af848daaef5 (GPL-3.0), which in turn follows
// Espressif's Apache-2.0 ble_hid_device_demo. This port removes stack/NVS
// initialisation, global callback registration, mouse/media reports, web UI,
// and multi-host state. It instead consumes ESPHome 2026.7.4 broker events.
//
// Report ID 1 is carried by the Report Reference descriptor, so the eight-byte
// GATT notification itself is the ordinary boot-compatible keyboard payload:
// modifier, reserved, and six key usages.
static uint8_t HID_REPORT_MAP[] = {
    0x05, 0x01, // Usage Page (Generic Desktop)
    0x09, 0x06, // Usage (Keyboard)
    0xA1, 0x01, // Collection (Application)
    0x85, 0x01, //   Report ID (1)
    0x05, 0x07, //   Usage Page (Keyboard/Keypad)
    0x19, 0xE0, //   Usage Minimum (Left Control)
    0x29, 0xE7, //   Usage Maximum (Right GUI)
    0x15, 0x00, //   Logical Minimum (0)
    0x25, 0x01, //   Logical Maximum (1)
    0x75, 0x01, //   Report Size (1)
    0x95, 0x08, //   Report Count (8)
    0x81, 0x02, //   Input (Data, Variable, Absolute)
    0x95, 0x01, //   Report Count (1)
    0x75, 0x08, //   Report Size (8)
    0x81, 0x01, //   Input (Constant)
    0x95, 0x05, //   Report Count (5)
    0x75, 0x01, //   Report Size (1)
    0x05, 0x08, //   Usage Page (LEDs)
    0x19, 0x01, //   Usage Minimum (Num Lock)
    0x29, 0x05, //   Usage Maximum (Kana)
    0x91, 0x02, //   Output (Data, Variable, Absolute)
    0x95, 0x01, //   Report Count (1)
    0x75, 0x03, //   Report Size (3)
    0x91, 0x01, //   Output (Constant)
    0x95, 0x06, //   Report Count (6)
    0x75, 0x08, //   Report Size (8)
    0x15, 0x00, //   Logical Minimum (0)
    0x25, 0x73, //   Logical Maximum (F24)
    0x05, 0x07, //   Usage Page (Keyboard/Keypad)
    0x19, 0x00, //   Usage Minimum (No event)
    0x29, 0x73, //   Usage Maximum (F24)
    0x81, 0x00, //   Input (Data, Array, Absolute)
    0xC0,       // End Collection
};

static uint8_t PNP_ID_VALUE[7] = {
    0x01,       // Vendor ID source: Bluetooth SIG
    0xE5, 0x02, // Espressif Bluetooth company identifier
    0xB2, 0xA1, // Product ID (project-local)
    0x00, 0x01, // Product version 1.0
};
static uint8_t MANUFACTURER_VALUE[] = "zwift-ride-hid";
// HOGP 1.0 requires BAS even for this USB-powered bridge. Until a board battery
// sensor is configured, 100 means the bridge endpoint is externally powered;
// it is deliberately not presented as either Ride controller's battery level.
static uint8_t BATTERY_LEVEL_VALUE = 100;
static uint16_t BATTERY_CCCD_VALUE = 0;

static uint8_t HID_INFO_VALUE[4] = {
    0x11,
    0x01, // HID version 1.11
    0x00, // Country code: not localised
    0x03, // Remote wake + normally connectable
};
static uint8_t HID_CONTROL_VALUE = 0;
static uint8_t PROTOCOL_MODE_VALUE = 1;
// External Report Reference descriptors contain the referenced
// characteristic UUID in little-endian byte order.
static uint8_t BATTERY_LEVEL_REFERENCE[2] = {0x19, 0x2A};
static uint8_t BOOT_INPUT_VALUE[8] = {};
static uint16_t BOOT_INPUT_CCCD_VALUE = 0;
static uint8_t BOOT_OUTPUT_VALUE[1] = {};
static uint8_t REPORT_INPUT_VALUE[8] = {};
static uint16_t REPORT_INPUT_CCCD_VALUE = 0;
static uint8_t REPORT_INPUT_REFERENCE[2] = {0x01, 0x01}; // ID 1, input
static uint8_t REPORT_OUTPUT_VALUE[1] = {};
static uint8_t REPORT_OUTPUT_REFERENCE[2] = {0x01, 0x02}; // ID 1, output

constexpr uint16_t UUID_DIS_SERVICE = ESP_GATT_UUID_DEVICE_INFO_SVC;
constexpr uint16_t UUID_BAS_SERVICE = ESP_GATT_UUID_BATTERY_SERVICE_SVC;
constexpr uint16_t UUID_HID_SERVICE = ESP_GATT_UUID_HID_SVC;
constexpr uint16_t UUID_PNP_ID = ESP_GATT_UUID_PNP_ID;
constexpr uint16_t UUID_MANUFACTURER = ESP_GATT_UUID_MANU_NAME;
constexpr uint16_t UUID_BATTERY_LEVEL = ESP_GATT_UUID_BATTERY_LEVEL;
constexpr uint16_t UUID_HID_INFORMATION = ESP_GATT_UUID_HID_INFORMATION;
constexpr uint16_t UUID_HID_REPORT_MAP = ESP_GATT_UUID_HID_REPORT_MAP;
constexpr uint16_t UUID_HID_CONTROL_POINT = ESP_GATT_UUID_HID_CONTROL_POINT;
constexpr uint16_t UUID_HID_PROTOCOL_MODE = ESP_GATT_UUID_HID_PROTO_MODE;
constexpr uint16_t UUID_BOOT_KEYBOARD_INPUT = ESP_GATT_UUID_HID_BT_KB_INPUT;
constexpr uint16_t UUID_BOOT_KEYBOARD_OUTPUT = ESP_GATT_UUID_HID_BT_KB_OUTPUT;
constexpr uint16_t UUID_HID_REPORT = ESP_GATT_UUID_HID_REPORT;
constexpr uint16_t UUID_CCCD = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
constexpr uint16_t UUID_REPORT_REFERENCE = ESP_GATT_UUID_RPT_REF_DESCR;
constexpr uint16_t UUID_EXTERNAL_REPORT_REFERENCE =
    ESP_GATT_UUID_EXT_RPT_REF_DESCR;

constexpr esp_gatt_perm_t PERMISSION_READ = ESP_GATT_PERM_READ;
constexpr esp_gatt_perm_t PERMISSION_READ_ENCRYPTED =
    ESP_GATT_PERM_READ_ENCRYPTED;
constexpr esp_gatt_perm_t PERMISSION_WRITE_ENCRYPTED =
    ESP_GATT_PERM_WRITE_ENCRYPTED;
constexpr esp_gatt_perm_t PERMISSION_READ_WRITE_ENCRYPTED =
    static_cast<esp_gatt_perm_t>(ESP_GATT_PERM_READ_ENCRYPTED |
                                 ESP_GATT_PERM_WRITE_ENCRYPTED);

constexpr esp_gatt_char_prop_t PROPERTY_READ = ESP_GATT_CHAR_PROP_BIT_READ;
constexpr esp_gatt_char_prop_t PROPERTY_WRITE_NR =
    ESP_GATT_CHAR_PROP_BIT_WRITE_NR;
constexpr esp_gatt_char_prop_t PROPERTY_READ_NOTIFY =
    static_cast<esp_gatt_char_prop_t>(ESP_GATT_CHAR_PROP_BIT_READ |
                                      ESP_GATT_CHAR_PROP_BIT_NOTIFY);
constexpr esp_gatt_char_prop_t PROPERTY_READ_WRITE_NR =
    static_cast<esp_gatt_char_prop_t>(ESP_GATT_CHAR_PROP_BIT_READ |
                                      ESP_GATT_CHAR_PROP_BIT_WRITE_NR);
constexpr esp_gatt_char_prop_t PROPERTY_READ_WRITE_BOTH =
    static_cast<esp_gatt_char_prop_t>(ESP_GATT_CHAR_PROP_BIT_READ |
                                      ESP_GATT_CHAR_PROP_BIT_WRITE |
                                      ESP_GATT_CHAR_PROP_BIT_WRITE_NR);

constexpr uint16_t DIS_HANDLE_COUNT =
    5; // service + two declaration/value pairs
constexpr uint16_t BAS_HANDLE_COUNT = 4; // service + declaration/value + CCCD
constexpr uint16_t HID_HANDLE_COUNT =
    23; // service + BAS include + eight characteristics + five descriptors
constexpr uint32_t GATT_CONSTRUCTION_TIMEOUT_MS = 10000;

bool uuid_is_16(const esp_bt_uuid_t &uuid, uint16_t expected) {
  return uuid.len == ESP_UUID_LEN_16 && uuid.uuid.uuid16 == expected;
}

bool time_reached(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

} // namespace

bool HidKeyboard::begin(const std::string &name) {
  if (name.empty() || name.size() > 20) {
    ESP_LOGE(TAG, "HID name must contain 1..20 bytes (got %u)",
             static_cast<unsigned>(name.size()));
    return false;
  }
  if (this->begin_requested_ && this->name_ != name) {
    ESP_LOGE(TAG, "Cannot change HID name after begin()");
    return false;
  }

  this->name_ = name;
  this->begin_requested_ = true;
  this->quiesced_ = false;
  if (this->parent_ == nullptr)
    this->parent_ = esp32_ble::global_ble;
  if (this->parent_ != nullptr) {
    // begin() normally runs before ESPHome enables Bluedroid, so the parent
    // can apply this name as part of its one stack bring-up. The owning
    // component configures appearance before setup; doing so here could
    // initialise shared advertising before the HID services are complete.
    this->parent_->set_name(this->name_.c_str());
  }
  return true;
}

void HidKeyboard::loop() {
  if (!this->begin_requested_ || this->failed_)
    return;
  if (this->app_registration_requested_ && !this->services_ready_ &&
      this->construction_deadline_ms_ != 0 &&
      time_reached(millis(), this->construction_deadline_ms_)) {
    // Never issue a second app-register or attribute-build operation: the
    // original request may have completed inside Bluedroid even if ESPHome's
    // bounded callback broker dropped its completion event.
    ESP_LOGE(TAG, "Timed out while constructing the BLE HID GATT database");
    this->construction_deadline_ms_ = 0;
    this->failed_ = true;
    return;
  }
  if (!this->app_registration_requested_) {
    const uint32_t now = millis();
    if (time_reached(now, this->next_register_attempt_ms_))
      this->try_register_();
    return;
  }
  if (this->pending_report_ && this->ready() && !this->congested_) {
    const uint32_t now = millis();
    if (time_reached(now, this->next_report_attempt_ms_))
      this->send_current_report_();
  }
  if (this->advertising_desired_ && this->services_ready_ &&
      !this->connected_ && !this->quiesced_ && !this->failed_) {
    const uint32_t now = millis();
    if (this->advertising_stop_requested_ &&
        time_reached(now, this->next_advertising_attempt_ms_)) {
      // A stop completion can be dropped by ESPHome's bounded broker queue.
      // Only recover once advertising is desired again: while quiesced there
      // is nothing useful (or safe) to restart.
      ESP_LOGW(TAG, "Timed out waiting for BLE advertising to stop; resuming");
      this->advertising_stop_requested_ = false;
      this->advertising_active_ = false;
    }
    if (this->advertising_start_requested_ &&
        time_reached(now, this->next_advertising_attempt_ms_)) {
      // A dropped broker completion must not leave the bridge undiscoverable.
      ESP_LOGW(TAG, "Timed out waiting for BLE advertising to start; retrying");
      this->advertising_start_requested_ = false;
      this->advertising_active_ = false;
    }
    if (!this->advertising_active_ && !this->advertising_start_requested_ &&
        !this->advertising_stop_requested_ &&
        time_reached(now, this->next_advertising_attempt_ms_))
      this->advertise();
  }
}

bool HidKeyboard::try_register_() {
  if (this->parent_ == nullptr)
    this->parent_ = esp32_ble::global_ble;
  if (this->parent_ == nullptr || !this->parent_->is_active())
    return false;

  // Covers the unusual case where begin() was invoked after ESPHome's BLE
  // setup. This changes no callback ownership and performs no blocking work.
  esp_err_t error = esp_ble_gap_set_device_name(this->name_.c_str());
  if (error != ESP_OK) {
    ESP_LOGW(TAG, "Could not set HID device name: %s", esp_err_to_name(error));
    this->next_register_attempt_ms_ = millis() + 1000;
    return false;
  }
  if (!this->configure_security_()) {
    this->next_register_attempt_ms_ = millis() + 1000;
    return false;
  }

  error = esp_ble_gatts_app_register(APP_ID);
  if (error != ESP_OK) {
    ESP_LOGW(TAG, "GATTS app registration request failed: %s",
             esp_err_to_name(error));
    this->next_register_attempt_ms_ = millis() + 1000;
    return false;
  }
  this->app_registration_requested_ = true;
  this->refresh_construction_watchdog_();
  ESP_LOGD(TAG, "GATTS app registration requested (app_id=0x%04X)", APP_ID);
  return true;
}

void HidKeyboard::refresh_construction_watchdog_() {
  this->construction_deadline_ms_ = millis() + GATT_CONSTRUCTION_TIMEOUT_MS;
}

bool HidKeyboard::configure_security_() {
  // Just Works Secure Connections bonding: suitable for an input-only bridge,
  // and accepted by iOS/iPadOS. Bond keys remain managed by Bluedroid's own NVS
  // integration, which ESPHome initialises before enabling Bluetooth.
  esp_ble_auth_req_t auth_request = ESP_LE_AUTH_REQ_SC_BOND;
  esp_ble_io_cap_t io_capability = ESP_IO_CAP_NONE;
  uint8_t key_size = 16;
  uint8_t initiator_keys = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
  uint8_t responder_keys = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;

  const struct {
    esp_ble_sm_param_t parameter;
    void *value;
    uint8_t length;
  } settings[] = {
      {ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_request, sizeof(auth_request)},
      {ESP_BLE_SM_IOCAP_MODE, &io_capability, sizeof(io_capability)},
      {ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(key_size)},
      {ESP_BLE_SM_SET_INIT_KEY, &initiator_keys, sizeof(initiator_keys)},
      {ESP_BLE_SM_SET_RSP_KEY, &responder_keys, sizeof(responder_keys)},
  };

  for (const auto &setting : settings) {
    const esp_err_t error = esp_ble_gap_set_security_param(
        setting.parameter, setting.value, setting.length);
    if (error != ESP_OK) {
      ESP_LOGW(TAG, "Could not apply BLE security parameter %u: %s",
               static_cast<unsigned>(setting.parameter),
               esp_err_to_name(error));
      return false;
    }
  }
  return true;
}

const HidKeyboard::AttributeSpec *
HidKeyboard::current_attributes_(size_t *count) const {
  static const AttributeSpec dis_attributes[] = {
      {AttributeKind::CHARACTERISTIC, UUID_PNP_ID, PERMISSION_READ,
       PROPERTY_READ, sizeof(PNP_ID_VALUE), sizeof(PNP_ID_VALUE), PNP_ID_VALUE,
       HandleSlot::NONE},
      {AttributeKind::CHARACTERISTIC, UUID_MANUFACTURER, PERMISSION_READ,
       PROPERTY_READ, sizeof(MANUFACTURER_VALUE) - 1,
       sizeof(MANUFACTURER_VALUE) - 1, MANUFACTURER_VALUE, HandleSlot::NONE},
  };
  static const AttributeSpec bas_attributes[] = {
      {AttributeKind::CHARACTERISTIC, UUID_BATTERY_LEVEL,
       PERMISSION_READ_ENCRYPTED, PROPERTY_READ_NOTIFY,
       sizeof(BATTERY_LEVEL_VALUE), sizeof(BATTERY_LEVEL_VALUE),
       &BATTERY_LEVEL_VALUE, HandleSlot::BATTERY_VALUE},
      {AttributeKind::DESCRIPTOR, UUID_CCCD, PERMISSION_READ_WRITE_ENCRYPTED, 0,
       sizeof(BATTERY_CCCD_VALUE), sizeof(BATTERY_CCCD_VALUE),
       reinterpret_cast<uint8_t *>(&BATTERY_CCCD_VALUE),
       HandleSlot::BATTERY_CCCD},
  };
  static const AttributeSpec hid_attributes[] = {
      {AttributeKind::CHARACTERISTIC, UUID_HID_INFORMATION,
       PERMISSION_READ_ENCRYPTED, PROPERTY_READ, sizeof(HID_INFO_VALUE),
       sizeof(HID_INFO_VALUE), HID_INFO_VALUE, HandleSlot::NONE},
      {AttributeKind::CHARACTERISTIC, UUID_HID_REPORT_MAP,
       PERMISSION_READ_ENCRYPTED, PROPERTY_READ, sizeof(HID_REPORT_MAP),
       sizeof(HID_REPORT_MAP), HID_REPORT_MAP, HandleSlot::NONE},
      {AttributeKind::DESCRIPTOR, UUID_EXTERNAL_REPORT_REFERENCE,
       PERMISSION_READ_ENCRYPTED, 0, sizeof(BATTERY_LEVEL_REFERENCE),
       sizeof(BATTERY_LEVEL_REFERENCE), BATTERY_LEVEL_REFERENCE,
       HandleSlot::NONE},
      {AttributeKind::CHARACTERISTIC, UUID_HID_CONTROL_POINT,
       PERMISSION_WRITE_ENCRYPTED, PROPERTY_WRITE_NR, sizeof(HID_CONTROL_VALUE),
       sizeof(HID_CONTROL_VALUE), &HID_CONTROL_VALUE,
       HandleSlot::HID_CONTROL_POINT},
      {AttributeKind::CHARACTERISTIC, UUID_HID_PROTOCOL_MODE,
       PERMISSION_READ_WRITE_ENCRYPTED, PROPERTY_READ_WRITE_NR,
       sizeof(PROTOCOL_MODE_VALUE), sizeof(PROTOCOL_MODE_VALUE),
       &PROTOCOL_MODE_VALUE, HandleSlot::PROTOCOL_MODE},
      {AttributeKind::CHARACTERISTIC, UUID_BOOT_KEYBOARD_INPUT,
       PERMISSION_READ_ENCRYPTED, PROPERTY_READ_NOTIFY,
       sizeof(BOOT_INPUT_VALUE), sizeof(BOOT_INPUT_VALUE), BOOT_INPUT_VALUE,
       HandleSlot::BOOT_INPUT},
      {AttributeKind::DESCRIPTOR, UUID_CCCD, PERMISSION_READ_WRITE_ENCRYPTED, 0,
       sizeof(BOOT_INPUT_CCCD_VALUE), sizeof(BOOT_INPUT_CCCD_VALUE),
       reinterpret_cast<uint8_t *>(&BOOT_INPUT_CCCD_VALUE),
       HandleSlot::BOOT_INPUT_CCCD},
      {AttributeKind::CHARACTERISTIC, UUID_BOOT_KEYBOARD_OUTPUT,
       PERMISSION_READ_WRITE_ENCRYPTED, PROPERTY_READ_WRITE_BOTH,
       sizeof(BOOT_OUTPUT_VALUE), sizeof(BOOT_OUTPUT_VALUE), BOOT_OUTPUT_VALUE,
       HandleSlot::BOOT_OUTPUT},
      {AttributeKind::CHARACTERISTIC, UUID_HID_REPORT,
       PERMISSION_READ_ENCRYPTED, PROPERTY_READ_NOTIFY,
       sizeof(REPORT_INPUT_VALUE), sizeof(REPORT_INPUT_VALUE),
       REPORT_INPUT_VALUE, HandleSlot::REPORT_INPUT},
      {AttributeKind::DESCRIPTOR, UUID_CCCD, PERMISSION_READ_WRITE_ENCRYPTED, 0,
       sizeof(REPORT_INPUT_CCCD_VALUE), sizeof(REPORT_INPUT_CCCD_VALUE),
       reinterpret_cast<uint8_t *>(&REPORT_INPUT_CCCD_VALUE),
       HandleSlot::REPORT_INPUT_CCCD},
      {AttributeKind::DESCRIPTOR, UUID_REPORT_REFERENCE,
       PERMISSION_READ_ENCRYPTED, 0, sizeof(REPORT_INPUT_REFERENCE),
       sizeof(REPORT_INPUT_REFERENCE), REPORT_INPUT_REFERENCE,
       HandleSlot::NONE},
      {AttributeKind::CHARACTERISTIC, UUID_HID_REPORT,
       PERMISSION_READ_WRITE_ENCRYPTED, PROPERTY_READ_WRITE_BOTH,
       sizeof(REPORT_OUTPUT_VALUE), sizeof(REPORT_OUTPUT_VALUE),
       REPORT_OUTPUT_VALUE, HandleSlot::REPORT_OUTPUT},
      {AttributeKind::DESCRIPTOR, UUID_REPORT_REFERENCE,
       PERMISSION_READ_ENCRYPTED, 0, sizeof(REPORT_OUTPUT_REFERENCE),
       sizeof(REPORT_OUTPUT_REFERENCE), REPORT_OUTPUT_REFERENCE,
       HandleSlot::NONE},
  };

  switch (this->service_stage_) {
  case ServiceStage::DIS:
    *count = sizeof(dis_attributes) / sizeof(dis_attributes[0]);
    return dis_attributes;
  case ServiceStage::BAS:
    *count = sizeof(bas_attributes) / sizeof(bas_attributes[0]);
    return bas_attributes;
  case ServiceStage::HID:
    *count = sizeof(hid_attributes) / sizeof(hid_attributes[0]);
    return hid_attributes;
  case ServiceStage::COMPLETE:
    *count = 0;
    return nullptr;
  }
  *count = 0;
  return nullptr;
}

uint16_t HidKeyboard::current_service_uuid_() const {
  switch (this->service_stage_) {
  case ServiceStage::DIS:
    return UUID_DIS_SERVICE;
  case ServiceStage::BAS:
    return UUID_BAS_SERVICE;
  case ServiceStage::HID:
    return UUID_HID_SERVICE;
  case ServiceStage::COMPLETE:
    return 0;
  }
  return 0;
}

uint16_t HidKeyboard::current_service_handle_count_() const {
  switch (this->service_stage_) {
  case ServiceStage::DIS:
    return DIS_HANDLE_COUNT;
  case ServiceStage::BAS:
    return BAS_HANDLE_COUNT;
  case ServiceStage::HID:
    return HID_HANDLE_COUNT;
  case ServiceStage::COMPLETE:
    return 0;
  }
  return 0;
}

uint16_t HidKeyboard::current_service_handle_() const {
  switch (this->service_stage_) {
  case ServiceStage::DIS:
    return this->dis_service_handle_;
  case ServiceStage::BAS:
    return this->bas_service_handle_;
  case ServiceStage::HID:
    return this->hid_service_handle_;
  case ServiceStage::COMPLETE:
    return 0;
  }
  return 0;
}

void HidKeyboard::set_current_service_handle_(uint16_t handle) {
  switch (this->service_stage_) {
  case ServiceStage::DIS:
    this->dis_service_handle_ = handle;
    break;
  case ServiceStage::BAS:
    this->bas_service_handle_ = handle;
    break;
  case ServiceStage::HID:
    this->hid_service_handle_ = handle;
    break;
  case ServiceStage::COMPLETE:
    break;
  }
}

bool HidKeyboard::create_current_service_() {
  const uint16_t uuid = this->current_service_uuid_();
  const uint16_t handle_count = this->current_service_handle_count_();
  if (uuid == 0 || handle_count == 0 || this->gatts_if_ == ESP_GATT_IF_NONE)
    return false;

  this->pending_service_id_ = {};
  this->pending_service_id_.is_primary = true;
  this->pending_service_id_.id.inst_id =
      static_cast<uint8_t>(this->service_stage_);
  this->pending_service_id_.id.uuid.len = ESP_UUID_LEN_16;
  this->pending_service_id_.id.uuid.uuid.uuid16 = uuid;
  const esp_err_t error = esp_ble_gatts_create_service(
      this->gatts_if_, &this->pending_service_id_, handle_count);
  if (error != ESP_OK) {
    this->mark_failed_("create service", error);
    return false;
  }
  return true;
}

bool HidKeyboard::add_battery_include_() {
  if (this->service_stage_ != ServiceStage::HID ||
      this->hid_service_handle_ == 0 || this->bas_service_handle_ == 0)
    return false;
  const esp_err_t error = esp_ble_gatts_add_included_service(
      this->hid_service_handle_, this->bas_service_handle_);
  if (error != ESP_OK) {
    this->mark_failed_("include Battery Service", error);
    return false;
  }
  return true;
}

bool HidKeyboard::add_next_attribute_() {
  size_t count = 0;
  const AttributeSpec *attributes = this->current_attributes_(&count);
  if (attributes == nullptr || this->attribute_index_ >= count)
    return this->start_current_service_();

  const AttributeSpec &attribute = attributes[this->attribute_index_];
  this->pending_uuid_ = {};
  this->pending_uuid_.len = ESP_UUID_LEN_16;
  this->pending_uuid_.uuid.uuid16 = attribute.uuid;
  this->pending_attr_value_ = {
      .attr_max_len = attribute.max_length,
      .attr_len = attribute.length,
      .attr_value = attribute.value,
  };
  this->pending_attr_control_.auto_rsp = ESP_GATT_AUTO_RSP;

  esp_err_t error;
  if (attribute.kind == AttributeKind::CHARACTERISTIC) {
    error = esp_ble_gatts_add_char(
        this->current_service_handle_(), &this->pending_uuid_,
        attribute.permissions, attribute.properties, &this->pending_attr_value_,
        &this->pending_attr_control_);
  } else {
    error = esp_ble_gatts_add_char_descr(
        this->current_service_handle_(), &this->pending_uuid_,
        attribute.permissions, &this->pending_attr_value_,
        &this->pending_attr_control_);
  }
  if (error != ESP_OK) {
    this->mark_failed_(attribute.kind == AttributeKind::CHARACTERISTIC
                           ? "add characteristic"
                           : "add descriptor",
                       error);
    return false;
  }
  return true;
}

bool HidKeyboard::start_current_service_() {
  const esp_err_t error =
      esp_ble_gatts_start_service(this->current_service_handle_());
  if (error != ESP_OK) {
    this->mark_failed_("start service", error);
    return false;
  }
  return true;
}

bool HidKeyboard::advance_service_() {
  switch (this->service_stage_) {
  case ServiceStage::DIS:
    this->service_stage_ = ServiceStage::BAS;
    break;
  case ServiceStage::BAS:
    this->service_stage_ = ServiceStage::HID;
    break;
  case ServiceStage::HID:
    this->service_stage_ = ServiceStage::COMPLETE;
    this->services_ready_ = true;
    this->construction_deadline_ms_ = 0;
    ESP_LOGI(TAG, "BLE HID services ready");
    return this->advertise();
  case ServiceStage::COMPLETE:
    return true;
  }
  this->attribute_index_ = 0;
  return this->create_current_service_();
}

void HidKeyboard::set_attribute_handle_(HandleSlot slot, uint16_t handle) {
  switch (slot) {
  case HandleSlot::NONE:
    break;
  case HandleSlot::BATTERY_VALUE:
    this->battery_value_handle_ = handle;
    break;
  case HandleSlot::BATTERY_CCCD:
    this->battery_cccd_handle_ = handle;
    break;
  case HandleSlot::PROTOCOL_MODE:
    this->protocol_mode_handle_ = handle;
    break;
  case HandleSlot::BOOT_INPUT:
    this->boot_input_handle_ = handle;
    break;
  case HandleSlot::BOOT_INPUT_CCCD:
    this->boot_input_cccd_handle_ = handle;
    break;
  case HandleSlot::BOOT_OUTPUT:
    this->boot_output_handle_ = handle;
    break;
  case HandleSlot::REPORT_INPUT:
    this->report_input_handle_ = handle;
    break;
  case HandleSlot::REPORT_INPUT_CCCD:
    this->report_input_cccd_handle_ = handle;
    break;
  case HandleSlot::REPORT_OUTPUT:
    this->report_output_handle_ = handle;
    break;
  case HandleSlot::HID_CONTROL_POINT:
    this->hid_control_point_handle_ = handle;
    break;
  }
}

bool HidKeyboard::is_our_gatts_event_(
    esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
    const esp_ble_gatts_cb_param_t *param) const {
  if (param == nullptr)
    return false;
  if (event == ESP_GATTS_REG_EVT)
    return param->reg.app_id == APP_ID;
  return this->registered_ && this->gatts_if_ != ESP_GATT_IF_NONE &&
         gatts_if == this->gatts_if_;
}

void HidKeyboard::gatts_event_handler(esp_gatts_cb_event_t event,
                                      esp_gatt_if_t gatts_if,
                                      esp_ble_gatts_cb_param_t *param) {
  if (!this->is_our_gatts_event_(event, gatts_if, param))
    return;
  if (this->failed_ && event != ESP_GATTS_UNREG_EVT)
    return;

  switch (event) {
  case ESP_GATTS_REG_EVT:
    if (param->reg.status != ESP_GATT_OK) {
      ESP_LOGE(TAG, "GATTS app registration failed: status=%u",
               static_cast<unsigned>(param->reg.status));
      this->failed_ = true;
      return;
    }
    this->gatts_if_ = gatts_if;
    this->registered_ = true;
    this->refresh_construction_watchdog_();
    this->service_stage_ = ServiceStage::DIS;
    this->attribute_index_ = 0;
    this->create_current_service_();
    return;

  case ESP_GATTS_CREATE_EVT:
    if (param->create.status != ESP_GATT_OK ||
        !uuid_is_16(param->create.service_id.id.uuid,
                    this->current_service_uuid_())) {
      ESP_LOGE(TAG, "Unexpected/failed service creation event (status=%u)",
               static_cast<unsigned>(param->create.status));
      this->failed_ = true;
      return;
    }
    this->refresh_construction_watchdog_();
    this->set_current_service_handle_(param->create.service_handle);
    this->attribute_index_ = 0;
    if (this->service_stage_ == ServiceStage::HID)
      this->add_battery_include_();
    else
      this->add_next_attribute_();
    return;

  case ESP_GATTS_ADD_INCL_SRVC_EVT:
    if (this->service_stage_ != ServiceStage::HID ||
        param->add_incl_srvc.status != ESP_GATT_OK ||
        param->add_incl_srvc.service_handle != this->hid_service_handle_) {
      ESP_LOGE(TAG, "Unexpected/failed Battery Service include (status=%u)",
               static_cast<unsigned>(param->add_incl_srvc.status));
      this->failed_ = true;
      return;
    }
    this->refresh_construction_watchdog_();
    this->add_next_attribute_();
    return;

  case ESP_GATTS_ADD_CHAR_EVT:
  case ESP_GATTS_ADD_CHAR_DESCR_EVT: {
    size_t count = 0;
    const AttributeSpec *attributes = this->current_attributes_(&count);
    if (attributes == nullptr || this->attribute_index_ >= count) {
      ESP_LOGE(
          TAG,
          "Unexpected attribute callback after service definition completed");
      this->failed_ = true;
      return;
    }
    const AttributeSpec &expected = attributes[this->attribute_index_];
    const bool characteristic = event == ESP_GATTS_ADD_CHAR_EVT;
    const esp_gatt_status_t status =
        characteristic ? param->add_char.status : param->add_char_descr.status;
    const uint16_t service_handle = characteristic
                                        ? param->add_char.service_handle
                                        : param->add_char_descr.service_handle;
    const uint16_t attribute_handle = characteristic
                                          ? param->add_char.attr_handle
                                          : param->add_char_descr.attr_handle;
    const esp_bt_uuid_t &uuid = characteristic
                                    ? param->add_char.char_uuid
                                    : param->add_char_descr.descr_uuid;
    if (status != ESP_GATT_OK ||
        service_handle != this->current_service_handle_() ||
        characteristic != (expected.kind == AttributeKind::CHARACTERISTIC) ||
        !uuid_is_16(uuid, expected.uuid)) {
      ESP_LOGE(
          TAG,
          "Unexpected/failed GATT attribute callback (status=%u uuid=0x%04X)",
          static_cast<unsigned>(status), expected.uuid);
      this->failed_ = true;
      return;
    }
    this->refresh_construction_watchdog_();
    this->set_attribute_handle_(expected.handle_slot, attribute_handle);
    this->attribute_index_++;
    this->add_next_attribute_();
    return;
  }

  case ESP_GATTS_START_EVT:
    if (param->start.service_handle != this->current_service_handle_())
      return;
    if (param->start.status != ESP_GATT_OK) {
      ESP_LOGE(TAG, "GATT service start failed: status=%u",
               static_cast<unsigned>(param->start.status));
      this->failed_ = true;
      return;
    }
    this->refresh_construction_watchdog_();
    this->advance_service_();
    return;

  case ESP_GATTS_CONNECT_EVT:
    // GATTS receives physical-connection events for registered apps even
    // when this ESP is the central. Only a peripheral/slave-role link is an
    // iPad (HID host) connection; the master-role Ride link is not ours.
    if (param->connect.link_role != 1)
      return;
    // A different ESPHome feature could have started the shared advertiser
    // while this database was still being built, or a connect may race with
    // quiesce(). Never let a host cache or use a partial/suppressed HID DB.
    if (!this->services_ready_ || this->quiesced_) {
      ESP_LOGW(TAG, "Rejecting HID host while the service is unavailable");
      esp_ble_gap_disconnect(param->connect.remote_bda);
      return;
    }
    if (this->connected_ && !this->is_hid_peer_(param->connect.remote_bda)) {
      ESP_LOGW(TAG, "Rejecting a second HID host connection");
      esp_ble_gap_disconnect(param->connect.remote_bda);
      return;
    }
    this->connected_ = true;
    this->advertising_desired_ = false;
    this->advertising_active_ =
        false; // Connectable advertising ends on link establishment.
    this->advertising_start_requested_ = false;
    this->encrypted_ = false;
    this->connection_id_ = param->connect.conn_id;
    memcpy(this->peer_address_, param->connect.remote_bda,
           sizeof(esp_bd_addr_t));
    this->mtu_ = 23;
    this->protocol_mode_ = 1;
    this->report_notifications_ = false;
    this->boot_notifications_ = false;
    this->congested_ = false;
    this->suspended_ = false;
    this->pending_report_ = true;
    if (const esp_err_t error = esp_ble_set_encryption(
            this->peer_address_, ESP_BLE_SEC_ENCRYPT_NO_MITM);
        error != ESP_OK) {
      ESP_LOGW(TAG, "Could not request HID-link encryption: %s",
               esp_err_to_name(error));
    }
    ESP_LOGI(TAG,
             "HID host connected; waiting for encryption and notifications");
    return;

  case ESP_GATTS_DISCONNECT_EVT:
    if (!this->connected_ ||
        param->disconnect.conn_id != this->connection_id_ ||
        !this->is_hid_peer_(param->disconnect.remote_bda))
      return;
    ESP_LOGI(TAG, "HID host disconnected (reason=0x%02X)",
             static_cast<unsigned>(param->disconnect.reason));
    this->clear_connection_();
    this->advertise();
    return;

  case ESP_GATTS_WRITE_EVT:
    if (!this->connected_ || param->write.conn_id != this->connection_id_ ||
        !this->is_hid_peer_(param->write.bda) || param->write.is_prep ||
        param->write.offset != 0 || param->write.value == nullptr)
      return;
    if (param->write.handle == this->protocol_mode_handle_ &&
        param->write.len == 1) {
      this->protocol_mode_ = param->write.value[0] == 0 ? 0 : 1;
      PROTOCOL_MODE_VALUE = this->protocol_mode_;
      esp_ble_gatts_set_attr_value(this->protocol_mode_handle_,
                                   sizeof(PROTOCOL_MODE_VALUE),
                                   &PROTOCOL_MODE_VALUE);
      this->pending_report_ = true;
    } else if (param->write.handle == this->report_input_cccd_handle_ &&
               param->write.len == 2) {
      const uint16_t cccd = static_cast<uint16_t>(param->write.value[0]) |
                            (static_cast<uint16_t>(param->write.value[1]) << 8);
      this->report_notifications_ = (cccd & 0x0001U) != 0;
      this->encrypted_ = true; // This descriptor requires encryption.
      this->pending_report_ = true;
    } else if (param->write.handle == this->boot_input_cccd_handle_ &&
               param->write.len == 2) {
      const uint16_t cccd = static_cast<uint16_t>(param->write.value[0]) |
                            (static_cast<uint16_t>(param->write.value[1]) << 8);
      this->boot_notifications_ = (cccd & 0x0001U) != 0;
      this->encrypted_ = true;
      this->pending_report_ = true;
    } else if ((param->write.handle == this->report_output_handle_ ||
                param->write.handle == this->boot_output_handle_) &&
               param->write.len >= 1) {
      this->keyboard_leds_ = param->write.value[0] & 0x1F;
    } else if (param->write.handle == this->hid_control_point_handle_ &&
               param->write.len == 1) {
      this->suspended_ = param->write.value[0] == 0;
      if (!this->suspended_)
        this->pending_report_ = true;
    }
    if (this->ready() && !this->congested_)
      this->send_current_report_();
    return;

  case ESP_GATTS_MTU_EVT:
    if (this->connected_ && param->mtu.conn_id == this->connection_id_)
      this->mtu_ = std::max<uint16_t>(23, param->mtu.mtu);
    return;

  case ESP_GATTS_CONGEST_EVT:
    if (this->connected_ && param->congest.conn_id == this->connection_id_) {
      this->congested_ = param->congest.congested;
      if (!this->congested_ && this->pending_report_)
        this->send_current_report_();
    }
    return;

  case ESP_GATTS_CONF_EVT:
    if (this->connected_ && param->conf.conn_id == this->connection_id_ &&
        param->conf.status != ESP_GATT_OK)
      ESP_LOGW(TAG, "GATT confirmation failed: status=%u",
               static_cast<unsigned>(param->conf.status));
    return;

  case ESP_GATTS_UNREG_EVT:
    this->reset_server_state_();
    return;

  default:
    return;
  }
}

bool HidKeyboard::is_hid_peer_(const esp_bd_addr_t address) const {
  return memcmp(this->peer_address_, address, sizeof(esp_bd_addr_t)) == 0;
}

void HidKeyboard::gap_event_handler(esp_gap_ble_cb_event_t event,
                                    esp_ble_gap_cb_param_t *param) {
  if (param == nullptr)
    return;

  // ESPHome owns advertising configuration/start. It does not expose stop or
  // track these completions, so this local state only closes quiesce/start
  // races; resume always re-enters through ESPHome's advertising_start().
  if (event == ESP_GAP_BLE_ADV_START_COMPLETE_EVT) {
    this->advertising_start_requested_ = false;
    this->advertising_active_ =
        param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS;
    if (!this->advertising_active_) {
      ESP_LOGW(TAG, "BLE advertising start failed (status=%u); retrying",
               static_cast<unsigned>(param->adv_start_cmpl.status));
      this->next_advertising_attempt_ms_ = millis() + 500;
    } else if (!this->advertising_desired_ || this->quiesced_ ||
               this->connected_) {
      this->stop_advertising_();
    }
    return;
  }
  if (event == ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT) {
    const bool stopped = param->adv_stop_cmpl.status == ESP_BT_STATUS_SUCCESS;
    this->advertising_active_ = !stopped;
    this->advertising_start_requested_ = false;
    this->advertising_stop_requested_ = false;
    if (!stopped) {
      ESP_LOGW(TAG, "BLE advertising stop failed (status=%u)",
               static_cast<unsigned>(param->adv_stop_cmpl.status));
      if (!this->advertising_desired_ || this->quiesced_)
        this->stop_advertising_();
    } else if (this->advertising_desired_ && this->services_ready_ &&
               !this->quiesced_ && !this->connected_) {
      // resume() may have raced with the asynchronous stop. Restart only after
      // Bluedroid confirms that the prior advertising instance is gone.
      this->advertise();
    }
    return;
  }
  if (!this->connected_)
    return;

  switch (event) {
  case ESP_GAP_BLE_SEC_REQ_EVT:
    if (this->is_hid_peer_(param->ble_security.ble_req.bd_addr))
      esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
    return;
  case ESP_GAP_BLE_AUTH_CMPL_EVT:
    if (!this->is_hid_peer_(param->ble_security.auth_cmpl.bd_addr))
      return;
    this->encrypted_ = param->ble_security.auth_cmpl.success;
    if (this->encrypted_) {
      ESP_LOGI(TAG, "HID host pairing/encryption complete");
    } else {
      ESP_LOGW(
          TAG, "HID host pairing failed (reason=0x%02X)",
          static_cast<unsigned>(param->ble_security.auth_cmpl.fail_reason));
    }
    return;
  case ESP_GAP_BLE_PASSKEY_REQ_EVT:
    if (this->is_hid_peer_(param->ble_security.ble_req.bd_addr))
      esp_ble_passkey_reply(param->ble_security.ble_req.bd_addr, false, 0);
    return;
  case ESP_GAP_BLE_NC_REQ_EVT:
    if (this->is_hid_peer_(param->ble_security.key_notif.bd_addr))
      esp_ble_confirm_reply(param->ble_security.key_notif.bd_addr, true);
    return;
  case ESP_GAP_BLE_PASSKEY_NOTIF_EVT:
    // IO_CAP_NONE should not enter passkey display, but keep this event
    // peer-filtered so it can never affect the outbound Ride link.
    return;
  default:
    return;
  }
}

bool HidKeyboard::notifications_enabled_() const {
  return this->report_notifications_ || this->boot_notifications_;
}

uint16_t HidKeyboard::selected_input_handle_() const {
  if (this->protocol_mode_ == 0 && this->boot_notifications_)
    return this->boot_input_handle_;
  if (this->protocol_mode_ != 0 && this->report_notifications_)
    return this->report_input_handle_;
  if (this->report_notifications_)
    return this->report_input_handle_;
  if (this->boot_notifications_)
    return this->boot_input_handle_;
  return 0;
}

bool HidKeyboard::ready() const {
  return this->services_ready_ && this->connected_ && this->encrypted_ &&
         this->notifications_enabled_() && !this->quiesced_ &&
         !this->suspended_ && !this->failed_;
}

bool HidKeyboard::send_report(uint8_t modifiers, const uint8_t *key_usages,
                              size_t key_count) {
  if (this->quiesced_ || key_count > MAX_KEYS ||
      (key_count != 0 && key_usages == nullptr))
    return false;

  this->current_report_.fill(0);
  this->current_report_[0] = modifiers;
  if (key_count != 0)
    memcpy(this->current_report_.data() + 2, key_usages, key_count);
  // Reads must never expose an older pressed-key image merely because the
  // connection is not yet encrypted/subscribed (or just lost its CCCD).
  if (this->report_input_handle_ != 0)
    esp_ble_gatts_set_attr_value(this->report_input_handle_,
                                 this->current_report_.size(),
                                 this->current_report_.data());
  if (this->boot_input_handle_ != 0)
    esp_ble_gatts_set_attr_value(this->boot_input_handle_,
                                 this->current_report_.size(),
                                 this->current_report_.data());
  this->pending_report_ = true;
  return this->send_current_report_();
}

bool HidKeyboard::send_report(uint8_t modifiers, uint8_t key0, uint8_t key1,
                              uint8_t key2, uint8_t key3, uint8_t key4,
                              uint8_t key5) {
  const uint8_t keys[MAX_KEYS] = {key0, key1, key2, key3, key4, key5};
  return this->send_report(modifiers, keys, MAX_KEYS);
}

bool HidKeyboard::send_current_report_() {
  if (!this->ready() || this->congested_)
    return false;
  const uint16_t handle = this->selected_input_handle_();
  if (handle == 0 || this->gatts_if_ == ESP_GATT_IF_NONE)
    return false;

  // Keep both readable values current regardless of the selected protocol.
  esp_ble_gatts_set_attr_value(this->report_input_handle_,
                               this->current_report_.size(),
                               this->current_report_.data());
  esp_ble_gatts_set_attr_value(this->boot_input_handle_,
                               this->current_report_.size(),
                               this->current_report_.data());

  const esp_err_t error = esp_ble_gatts_send_indicate(
      this->gatts_if_, this->connection_id_, handle,
      this->current_report_.size(), this->current_report_.data(), false);
  if (error != ESP_OK) {
    this->pending_report_ = true;
    this->next_report_attempt_ms_ = millis() + 20;
    ESP_LOGD(TAG, "Keyboard report deferred: %s", esp_err_to_name(error));
    return false;
  }
  this->pending_report_ = false;
  return true;
}

bool HidKeyboard::release_all() { return this->send_report(0, nullptr, 0); }

bool HidKeyboard::advertise() {
  if (!this->services_ready_ || this->quiesced_ || this->failed_) {
    this->advertising_desired_ = false;
    return false;
  }
  this->advertising_desired_ = true;
  // Connectable advertising stops automatically when the sole HID host
  // connects. Do not restart it while that host owns the server link.
  if (this->connected_)
    return true;
  // Bluedroid rejects overlapping start/stop operations. Their completion
  // callbacks will satisfy the desired state or schedule a retry.
  if (this->advertising_active_ || this->advertising_start_requested_ ||
      this->advertising_stop_requested_)
    return true;
  if (this->parent_ == nullptr)
    this->parent_ = esp32_ble::global_ble;
  if (this->parent_ == nullptr || !this->parent_->is_active()) {
    this->next_advertising_attempt_ms_ = millis() + 250;
    return false;
  }
#ifdef USE_ESP32_BLE_ADVERTISING
  this->advertising_start_requested_ = true;
  this->next_advertising_attempt_ms_ = millis() + 2000;
  if (!this->advertising_uuid_added_) {
    this->parent_->advertising_add_service_uuid(
        esp32_ble::ESPBTUUID::from_uint16(UUID_HID_SERVICE));
    this->advertising_uuid_added_ = true;
  } else {
    this->parent_->advertising_start();
  }
  return true;
#else
  ESP_LOGE(TAG, "ESPHome BLE advertising support was not enabled");
  return false;
#endif
}

void HidKeyboard::quiesce() {
  if (this->quiesced_)
    return;
  // Queue an empty input notification while the encrypted link and its CCCD
  // are still usable. There is intentionally no delay: OTA/shutdown paths must
  // remain non-blocking, and Bluedroid owns transmission of the queued packet.
  this->release_all();
  this->quiesced_ = true;
  this->advertising_desired_ = false;
  this->pending_report_ = false;

  if (this->connected_) {
    const esp_err_t error = esp_ble_gap_disconnect(this->peer_address_);
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE)
      ESP_LOGW(TAG, "Could not initiate HID peer disconnect: %s",
               esp_err_to_name(error));
  }
  this->stop_advertising_();
}

void HidKeyboard::stop() { this->quiesce(); }

void HidKeyboard::resume() {
  this->quiesced_ = false;
  this->pending_report_ = true;
  this->advertising_desired_ = true;
  this->advertise();
}

bool HidKeyboard::stop_advertising_() {
  if (this->advertising_stop_requested_)
    return true;
  if (this->parent_ == nullptr)
    this->parent_ = esp32_ble::global_ble;
  if (this->parent_ == nullptr || !this->parent_->is_active()) {
    this->advertising_active_ = false;
    return true;
  }

  // ESPHome 2026.7.4 exposes advertising_start() but no symmetric stop(). A
  // direct GAP stop is therefore the least invasive route: no callbacks are
  // replaced and no advertising configuration/UUID state is mutated.
  const esp_err_t error = esp_ble_gap_stop_advertising();
  if (error == ESP_OK) {
    this->advertising_stop_requested_ = true;
    this->next_advertising_attempt_ms_ = millis() + 2000;
    return true;
  }
  if (error == ESP_ERR_INVALID_STATE) {
    // Already stopped (including the normal connected state).
    this->advertising_active_ = false;
    return true;
  }
  ESP_LOGW(TAG, "Could not stop BLE advertising: %s", esp_err_to_name(error));
  return false;
}

void HidKeyboard::clear_connection_() {
  this->connected_ = false;
  this->encrypted_ = false;
  this->connection_id_ = 0;
  memset(this->peer_address_, 0, sizeof(esp_bd_addr_t));
  this->mtu_ = 23;
  this->protocol_mode_ = 1;
  PROTOCOL_MODE_VALUE = 1;
  if (this->protocol_mode_handle_ != 0)
    esp_ble_gatts_set_attr_value(this->protocol_mode_handle_,
                                 sizeof(PROTOCOL_MODE_VALUE),
                                 &PROTOCOL_MODE_VALUE);
  this->report_notifications_ = false;
  this->boot_notifications_ = false;
  this->congested_ = false;
  this->suspended_ = false;
  // Never carry a pressed-key image across hosts. Keep one bounded empty
  // report pending so a newly encrypted/subscribed host is synchronised.
  this->current_report_.fill(0);
  this->pending_report_ = true;
  if (this->report_input_handle_ != 0)
    esp_ble_gatts_set_attr_value(this->report_input_handle_,
                                 this->current_report_.size(),
                                 this->current_report_.data());
  if (this->boot_input_handle_ != 0)
    esp_ble_gatts_set_attr_value(this->boot_input_handle_,
                                 this->current_report_.size(),
                                 this->current_report_.data());
}

void HidKeyboard::reset_server_state_() {
  this->clear_connection_();
  this->gatts_if_ = ESP_GATT_IF_NONE;
  this->service_stage_ = ServiceStage::DIS;
  this->attribute_index_ = 0;
  this->dis_service_handle_ = 0;
  this->bas_service_handle_ = 0;
  this->hid_service_handle_ = 0;
  this->battery_value_handle_ = 0;
  this->battery_cccd_handle_ = 0;
  this->protocol_mode_handle_ = 0;
  this->boot_input_handle_ = 0;
  this->boot_input_cccd_handle_ = 0;
  this->boot_output_handle_ = 0;
  this->report_input_handle_ = 0;
  this->report_input_cccd_handle_ = 0;
  this->report_output_handle_ = 0;
  this->hid_control_point_handle_ = 0;
  this->app_registration_requested_ = false;
  this->registered_ = false;
  this->services_ready_ = false;
  this->construction_deadline_ms_ = 0;
  this->advertising_desired_ = false;
  this->advertising_active_ = false;
  this->advertising_start_requested_ = false;
  this->advertising_stop_requested_ = false;
  // ESPHome's BLEAdvertising object survives a disable/enable cycle and keeps
  // its UUID vector. Preserve this flag so the HID UUID is not appended again
  // on every runtime BLE restart.
  this->failed_ = false;
  this->next_register_attempt_ms_ = millis() + 250;
}

void HidKeyboard::ble_before_disabled_event_handler() {
  if (this->ready())
    this->release_all();
  this->reset_server_state_();
}

void HidKeyboard::mark_failed_(const char *operation, esp_err_t error) {
  ESP_LOGE(TAG, "Failed to %s: %s", operation, esp_err_to_name(error));
  this->failed_ = true;
}

} // namespace esphome::zwift_ride_hid
