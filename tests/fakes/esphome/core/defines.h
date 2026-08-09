// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The component guards its ESP32-only sources on these. The build normally
// passes them on the command line, so only define them if it did not.
#ifndef USE_ESP32
#define USE_ESP32
#endif
#ifndef USE_ESP32_FRAMEWORK_ESP_IDF
#define USE_ESP32_FRAMEWORK_ESP_IDF
#endif
