// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>

using esp_err_t = int;

enum {
  ESP_OK = 0,
  ESP_FAIL = -1,
  ESP_ERR_INVALID_STATE = 0x103,
  ESP_ERR_INVALID_ARG = 0x102,
  ESP_ERR_NO_MEM = 0x101,
};

const char *esp_err_to_name(esp_err_t error);
