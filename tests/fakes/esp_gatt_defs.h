// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>

#include "esp_bt_defs.h"

using esp_gatt_if_t = uint8_t;
enum { ESP_GATT_IF_NONE = 0xFF };

enum esp_gatt_status_t {
  ESP_GATT_OK = 0x0,
  ESP_GATT_INVALID_HANDLE = 0x01,
  ESP_GATT_NOT_FOUND = 0x0a,
  ESP_GATT_NO_RESOURCES = 0x80,
  ESP_GATT_ERROR = 0x85,
  ESP_GATT_ALREADY_OPEN = 0x8b,
  ESP_GATT_REQ_NOT_SUPPORTED = 0x06,
  ESP_GATT_CONN_TIMEOUT = 0x93,
};

// Characteristic properties, as a bit mask.
using esp_gatt_char_prop_t = uint8_t;
enum {
  ESP_GATT_CHAR_PROP_BIT_BROADCAST = 1 << 0,
  ESP_GATT_CHAR_PROP_BIT_READ = 1 << 1,
  ESP_GATT_CHAR_PROP_BIT_WRITE_NR = 1 << 2,
  ESP_GATT_CHAR_PROP_BIT_WRITE = 1 << 3,
  ESP_GATT_CHAR_PROP_BIT_NOTIFY = 1 << 4,
  ESP_GATT_CHAR_PROP_BIT_INDICATE = 1 << 5,
};

using esp_gatt_perm_t = uint16_t;
enum {
  ESP_GATT_PERM_READ = 1 << 0,
  ESP_GATT_PERM_READ_ENCRYPTED = 1 << 1,
  ESP_GATT_PERM_WRITE = 1 << 4,
  ESP_GATT_PERM_WRITE_ENCRYPTED = 1 << 5,
};

enum esp_gatt_auth_req_t {
  ESP_GATT_AUTH_REQ_NONE = 0,
  ESP_GATT_AUTH_REQ_SIGNED_MITM = 4,
};

enum esp_gatt_write_type_t {
  ESP_GATT_WRITE_TYPE_NO_RSP = 1,
  ESP_GATT_WRITE_TYPE_RSP = 2,
};

enum esp_attr_write_flag_t { ESP_GATT_AUTO_RSP = 1, ESP_GATT_RSP_BY_APP = 0 };

// Assigned numbers the HID server builds its database from.
enum {
  ESP_GATT_UUID_CHAR_CLIENT_CONFIG = 0x2902,
  ESP_GATT_UUID_RPT_REF_DESCR = 0x2908,
  ESP_GATT_UUID_EXT_RPT_REF_DESCR = 0x2907,
  ESP_GATT_UUID_DEVICE_INFO_SVC = 0x180A,
  ESP_GATT_UUID_BATTERY_SERVICE_SVC = 0x180F,
  ESP_GATT_UUID_HID_SVC = 0x1812,
  ESP_GATT_UUID_BATTERY_LEVEL = 0x2A19,
  ESP_GATT_UUID_MANU_NAME = 0x2A29,
  ESP_GATT_UUID_PNP_ID = 0x2A50,
  ESP_GATT_UUID_HID_INFORMATION = 0x2A4A,
  ESP_GATT_UUID_HID_REPORT_MAP = 0x2A4B,
  ESP_GATT_UUID_HID_CONTROL_POINT = 0x2A4C,
  ESP_GATT_UUID_HID_REPORT = 0x2A4D,
  ESP_GATT_UUID_HID_PROTO_MODE = 0x2A4E,
  ESP_GATT_UUID_HID_BT_KB_INPUT = 0x2A22,
  ESP_GATT_UUID_HID_BT_KB_OUTPUT = 0x2A32,
};

struct esp_gatt_id_t {
  esp_bt_uuid_t uuid;
  uint8_t inst_id;
};

struct esp_gatt_srvc_id_t {
  esp_gatt_id_t id;
  bool is_primary;
};

struct esp_attr_value_t {
  uint16_t attr_max_len;
  uint16_t attr_len;
  uint8_t *attr_value;
};

struct esp_attr_control_t {
  uint8_t auto_rsp;
};

struct esp_gattc_service_elem_t {
  bool is_primary;
  uint16_t start_handle;
  uint16_t end_handle;
  esp_bt_uuid_t uuid;
};

struct esp_gattc_char_elem_t {
  uint16_t char_handle;
  esp_gatt_char_prop_t properties;
  esp_bt_uuid_t uuid;
};

struct esp_gattc_descr_elem_t {
  uint16_t handle;
  esp_bt_uuid_t uuid;
};
