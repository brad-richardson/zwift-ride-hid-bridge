// SPDX-License-Identifier: GPL-3.0-only
//
// Logging is discarded: behaviour is asserted through the recorded ESP-IDF
// calls and the component's own accessors, not through log text.
//
// The arguments are still passed to a sink template so they count as used.
// Expanding to a plain no-op would make every variable that exists only to be
// logged look dead under -Wunused, which would mean editing production code to
// satisfy a limitation of the test harness.
#pragma once

namespace fake_log {

template<typename... Ts> inline void sink(const Ts &...) {}

}  // namespace fake_log

#define ESP_LOGE(tag, ...) ::fake_log::sink(tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, ...) ::fake_log::sink(tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, ...) ::fake_log::sink(tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, ...) ::fake_log::sink(tag, ##__VA_ARGS__)
#define ESP_LOGV(tag, ...) ::fake_log::sink(tag, ##__VA_ARGS__)
#define ESP_LOGCONFIG(tag, ...) ::fake_log::sink(tag, ##__VA_ARGS__)

#define YESNO(b) ((b) ? "YES" : "NO")
#define TRUEFALSE(b) ((b) ? "true" : "false")
