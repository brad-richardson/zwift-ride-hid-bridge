// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>

namespace esphome::zwift_ride_hid {

/** Timing for the idle Ride-disconnect feature. All values are milliseconds.
 *
 * The Ride controllers stay awake as long as a central holds their GATT link,
 * so an unattended bridge drains their batteries. Dropping the link after a
 * quiet period lets them fall back to their own sleep behavior.
 */
struct RideIdleConfig {
  /// Quiet time before the Ride link is dropped. Zero disables the feature.
  uint32_t idle_timeout_ms{15UL * 60UL * 1000UL};
  /// How long the controller advertisement must be absent before a later
  /// advertisement is accepted as a genuine wake rather than the same
  /// still-awake controller that was just disconnected.
  uint32_t sleep_confirm_ms{30UL * 1000UL};
  /// Upper bound on suppression. A controller that never stops advertising
  /// would otherwise keep the bridge offline forever. Zero disables the cap.
  uint32_t max_suppression_ms{60UL * 60UL * 1000UL};
};

enum class RideIdleAction : uint8_t {
  /// Nothing to do this iteration.
  NONE,
  /// The session has been quiet for idle_timeout_ms: drop the Ride link and
  /// stop the stock client from reconnecting.
  DISCONNECT,
  /// Suppression has expired: let the stock client connect again.
  REARM,
};

/** Idle bookkeeping for one Ride session, independent of ESPHome.
 *
 * Every entry point takes the caller's millis() reading so the whole policy is
 * ordinary host-testable code. Elapsed times use unsigned subtraction, which is
 * rollover-safe for any interval shorter than 2^31 ms (about 24.8 days).
 */
class RideIdlePolicy {
 public:
  void set_config(const RideIdleConfig &config) { this->config_ = config; }
  const RideIdleConfig &config() const { return this->config_; }

  /// The Ride handshake completed. Starts a fresh quiet window.
  void on_session_ready(uint32_t now);

  /** Abandon any suppression and start a fresh quiet window.
   *
   * Used when something outside this policy takes over the Ride link — an OTA,
   * a shutdown, or a BLE stack restart — so the bridge never comes back up
   * still refusing to reconnect. The disconnect counter is preserved.
   */
  void reset(uint32_t now) { this->on_session_ready(now); }

  /// A user input transition was observed, or an input is still held.
  void on_activity(uint32_t now);

  /** A matching Ride Left advertisement was received.
   *
   * Returns true when this advertisement ends suppression, in which case the
   * caller should re-enable its BLE client before the advertisement reaches
   * the stock client so the same scan result can drive the reconnection.
   */
  bool on_advertisement(uint32_t now);

  /// Called every loop with the live session state. See RideIdleAction.
  RideIdleAction poll(uint32_t now, bool session_ready);

  /// True while the bridge is deliberately refusing to reconnect.
  bool suppressed() const { return this->suppressed_; }
  /// True once the controller advertisement has been absent long enough that
  /// the next one will be treated as a wake.
  bool sleep_confirmed() const { return this->sleep_confirmed_; }
  uint32_t idle_disconnect_count() const { return this->idle_disconnect_count_; }

  /// Quiet time so far. Only meaningful while a session is ready.
  uint32_t idle_elapsed_ms(uint32_t now) const {
    return static_cast<uint32_t>(now - this->last_activity_ms_);
  }

 protected:
  void begin_suppression_(uint32_t now);

  RideIdleConfig config_{};
  uint32_t last_activity_ms_{0};
  uint32_t suppressed_since_ms_{0};
  uint32_t last_advertisement_ms_{0};
  uint32_t idle_disconnect_count_{0};
  bool suppressed_{false};
  bool sleep_confirmed_{false};
};

}  // namespace esphome::zwift_ride_hid
