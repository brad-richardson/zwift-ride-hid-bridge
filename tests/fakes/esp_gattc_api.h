// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "esp_err.h"
#include "esp_gatt_defs.h"

enum esp_gattc_cb_event_t {
  ESP_GATTC_REG_EVT = 0,
  ESP_GATTC_OPEN_EVT,
  ESP_GATTC_CLOSE_EVT,
  ESP_GATTC_SEARCH_CMPL_EVT,
  ESP_GATTC_SEARCH_RES_EVT,
  ESP_GATTC_WRITE_CHAR_EVT,
  ESP_GATTC_NOTIFY_EVT,
  ESP_GATTC_WRITE_DESCR_EVT,
  ESP_GATTC_CFG_MTU_EVT,
  ESP_GATTC_CONNECT_EVT,
  ESP_GATTC_DISCONNECT_EVT,
  ESP_GATTC_REG_FOR_NOTIFY_EVT,
};

union esp_ble_gattc_cb_param_t {
  struct {
    esp_gatt_status_t status;
    uint16_t conn_id;
    esp_bd_addr_t remote_bda;
  } open;
  struct {
    esp_gatt_status_t status;
    uint16_t conn_id;
    esp_bd_addr_t remote_bda;
    int reason;
  } close;
  struct {
    uint16_t conn_id;
    esp_bd_addr_t remote_bda;
    uint8_t link_role;
  } connect;
  struct {
    esp_gatt_status_t status;
    uint16_t conn_id;
    esp_bd_addr_t remote_bda;
    int reason;
  } disconnect;
  struct {
    esp_gatt_status_t status;
    uint16_t conn_id;
  } search_cmpl;
  struct {
    esp_gatt_status_t status;
    uint16_t conn_id;
    uint16_t handle;
    uint16_t offset;
  } write;
  struct {
    esp_gatt_status_t status;
    uint16_t handle;
  } reg_for_notify;
  struct {
    uint16_t conn_id;
    esp_bd_addr_t remote_bda;
    uint16_t handle;
    uint16_t value_len;
    uint8_t *value;
    bool is_notify;
  } notify;
};

esp_gatt_status_t esp_ble_gattc_get_service(esp_gatt_if_t gattc_if, uint16_t conn_id,
                                            esp_bt_uuid_t *svc_uuid,
                                            esp_gattc_service_elem_t *result,
                                            uint16_t *count, uint16_t offset);

esp_gatt_status_t esp_ble_gattc_get_char_by_uuid(esp_gatt_if_t gattc_if, uint16_t conn_id,
                                                 uint16_t start_handle, uint16_t end_handle,
                                                 esp_bt_uuid_t char_uuid,
                                                 esp_gattc_char_elem_t *result, uint16_t *count);

esp_gatt_status_t esp_ble_gattc_get_descr_by_char_handle(esp_gatt_if_t gattc_if, uint16_t conn_id,
                                                         uint16_t char_handle,
                                                         esp_bt_uuid_t descr_uuid,
                                                         esp_gattc_descr_elem_t *result,
                                                         uint16_t *count);

esp_err_t esp_ble_gattc_write_char(esp_gatt_if_t gattc_if, uint16_t conn_id, uint16_t handle,
                                   uint16_t value_len, uint8_t *value,
                                   esp_gatt_write_type_t write_type, esp_gatt_auth_req_t auth_req);

esp_err_t esp_ble_gattc_register_for_notify(esp_gatt_if_t gattc_if, esp_bd_addr_t server_bda,
                                            uint16_t handle);
