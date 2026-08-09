// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>

namespace esphome {

/// Controlled by fake_ble::advance() so tests can step time deterministically.
uint32_t millis();

}  // namespace esphome
