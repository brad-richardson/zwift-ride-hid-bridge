// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "esp_err.h"
#include "esp_gatt_defs.h"

enum esp_gatts_cb_event_t {
  ESP_GATTS_REG_EVT = 0,
  ESP_GATTS_READ_EVT,
  ESP_GATTS_WRITE_EVT,
  ESP_GATTS_MTU_EVT,
  ESP_GATTS_CONF_EVT,
  ESP_GATTS_UNREG_EVT,
  ESP_GATTS_CREATE_EVT,
  ESP_GATTS_ADD_INCL_SRVC_EVT,
  ESP_GATTS_ADD_CHAR_EVT,
  ESP_GATTS_ADD_CHAR_DESCR_EVT,
  ESP_GATTS_DELETE_EVT,
  ESP_GATTS_START_EVT,
  ESP_GATTS_STOP_EVT,
  ESP_GATTS_CONNECT_EVT,
  ESP_GATTS_DISCONNECT_EVT,
  ESP_GATTS_CONGEST_EVT,
};

union esp_ble_gatts_cb_param_t {
  struct {
    esp_gatt_status_t status;
    uint16_t app_id;
  } reg;
  struct {
    esp_gatt_status_t status;
    uint16_t service_handle;
    esp_gatt_srvc_id_t service_id;
  } create;
  struct {
    esp_gatt_status_t status;
    uint16_t attr_handle;
    uint16_t service_handle;
  } add_incl_srvc;
  struct {
    esp_gatt_status_t status;
    uint16_t attr_handle;
    uint16_t service_handle;
    esp_bt_uuid_t char_uuid;
  } add_char;
  struct {
    esp_gatt_status_t status;
    uint16_t attr_handle;
    uint16_t service_handle;
    esp_bt_uuid_t descr_uuid;
  } add_char_descr;
  struct {
    esp_gatt_status_t status;
    uint16_t service_handle;
  } start;
  struct {
    uint16_t conn_id;
    esp_bd_addr_t remote_bda;
    uint8_t link_role;
  } connect;
  struct {
    uint16_t conn_id;
    esp_bd_addr_t remote_bda;
    int reason;
  } disconnect;
  struct {
    uint16_t conn_id;
    uint32_t trans_id;
    esp_bd_addr_t bda;
    uint16_t handle;
    uint16_t offset;
    bool need_rsp;
    bool is_prep;
    uint16_t len;
    uint8_t *value;
  } write;
  struct {
    uint16_t conn_id;
    uint16_t mtu;
  } mtu;
  struct {
    uint16_t conn_id;
    bool congested;
  } congest;
  struct {
    esp_gatt_status_t status;
    uint16_t conn_id;
    uint16_t handle;
  } conf;
};

esp_err_t esp_ble_gatts_app_register(uint16_t app_id);
esp_err_t esp_ble_gatts_create_service(esp_gatt_if_t gatts_if, esp_gatt_srvc_id_t *service_id,
                                       uint16_t num_handle);
esp_err_t esp_ble_gatts_add_included_service(uint16_t service_handle, uint16_t included_handle);
esp_err_t esp_ble_gatts_add_char(uint16_t service_handle, esp_bt_uuid_t *char_uuid,
                                 esp_gatt_perm_t perm, esp_gatt_char_prop_t property,
                                 esp_attr_value_t *char_val, esp_attr_control_t *control);
esp_err_t esp_ble_gatts_add_char_descr(uint16_t service_handle, esp_bt_uuid_t *descr_uuid,
                                       esp_gatt_perm_t perm, esp_attr_value_t *char_descr_val,
                                       esp_attr_control_t *control);
esp_err_t esp_ble_gatts_start_service(uint16_t service_handle);
esp_err_t esp_ble_gatts_send_indicate(esp_gatt_if_t gatts_if, uint16_t conn_id, uint16_t handle,
                                      uint16_t value_len, uint8_t *value, bool need_confirm);
esp_err_t esp_ble_gatts_set_attr_value(uint16_t attr_handle, uint16_t length,
                                       const uint8_t *value);
