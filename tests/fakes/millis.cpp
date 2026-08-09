// SPDX-License-Identifier: GPL-3.0-only
#include "esphome/core/hal.h"

#include "fake_ble.h"

namespace esphome {

uint32_t millis() { return fake_ble::state().now_ms; }

}  // namespace esphome
