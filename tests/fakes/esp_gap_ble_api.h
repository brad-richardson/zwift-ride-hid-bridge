// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "esp_bt_defs.h"
#include "esp_err.h"
#include "esp_gatt_defs.h"

enum esp_gap_ble_cb_event_t {
  ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT = 0,
  ESP_GAP_BLE_ADV_START_COMPLETE_EVT,
  ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT,
  ESP_GAP_BLE_SEC_REQ_EVT,
  ESP_GAP_BLE_PASSKEY_NOTIF_EVT,
  ESP_GAP_BLE_PASSKEY_REQ_EVT,
  ESP_GAP_BLE_AUTH_CMPL_EVT,
  ESP_GAP_BLE_NC_REQ_EVT,
};

enum esp_ble_sm_param_t {
  ESP_BLE_SM_AUTHEN_REQ_MODE = 0,
  ESP_BLE_SM_IOCAP_MODE,
  ESP_BLE_SM_SET_INIT_KEY,
  ESP_BLE_SM_SET_RSP_KEY,
  ESP_BLE_SM_MAX_KEY_SIZE,
};

using esp_ble_auth_req_t = uint8_t;
enum { ESP_LE_AUTH_REQ_SC_BOND = 0x0D };

using esp_ble_io_cap_t = uint8_t;
enum { ESP_IO_CAP_NONE = 3 };

enum { ESP_BLE_ENC_KEY_MASK = 0x01, ESP_BLE_ID_KEY_MASK = 0x02 };

enum esp_ble_sec_act_t { ESP_BLE_SEC_ENCRYPT_NO_MITM = 3 };

union esp_ble_gap_cb_param_t {
  struct {
    esp_bt_status_t status;
  } adv_start_cmpl;
  struct {
    esp_bt_status_t status;
  } adv_stop_cmpl;
  union {
    struct {
      esp_bd_addr_t bd_addr;
    } ble_req;
    struct {
      esp_bd_addr_t bd_addr;
      uint32_t passkey;
    } key_notif;
    struct {
      esp_bd_addr_t bd_addr;
      bool success;
      uint8_t fail_reason;
    } auth_cmpl;
  } ble_security;
};

esp_err_t esp_ble_gap_set_device_name(const char *name);
esp_err_t esp_ble_gap_set_security_param(esp_ble_sm_param_t param_type, void *value,
                                         uint8_t len);
esp_err_t esp_ble_gap_disconnect(esp_bd_addr_t remote_device);
esp_err_t esp_ble_gap_stop_advertising();
esp_err_t esp_ble_gap_security_rsp(esp_bd_addr_t bd_addr, bool accept);
esp_err_t esp_ble_passkey_reply(esp_bd_addr_t bd_addr, bool accept, uint32_t passkey);
esp_err_t esp_ble_confirm_reply(esp_bd_addr_t bd_addr, bool confirm);
esp_err_t esp_ble_set_encryption(esp_bd_addr_t bd_addr, esp_ble_sec_act_t sec_act);
