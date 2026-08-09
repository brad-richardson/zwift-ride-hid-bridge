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

  /** Advertising rate thresholds, measured as a mean gap between sightings.
   *
   * Ride Left does not stop advertising when released, and it does not
   * advertise in bursts. Measured at 50% scan duty it runs continuously at
   * ~196 ms for about two minutes, drops to a continuous ~640 ms, then stops
   * near three minutes. Those two rates differ by 3.3x, which is the only
   * usable discriminator: the payload never varies, and no gap length
   * separates "winding down" from "asleep".
   *
   * Rate is deliberately measured rather than inferred from gap lengths. At
   * the earlier 9.375% scan duty the same slow phase looked like a ~6 s
   * interval, a tenfold error caused entirely by missed advertisements, and a
   * detector sized against it reconnected into a controller that was merely
   * winding down. Sampling has to resolve the rate for any of this to work.
   *
   * The two thresholds form a hysteresis band around the measured rates, so
   * ordinary jitter cannot flip the state back and forth.
   */
  uint32_t slow_rate_ms{500};
  uint32_t wake_rate_ms{350};

  /// Sightings averaged to obtain that rate. More is steadier but slower to
  /// react; this many spans about 1.4 s of fast advertising.
  uint8_t rate_sample_count{8};

  /** Ignore sightings closer together than this when measuring the rate.
   *
   * One advertising event is often seen twice a few milliseconds apart from
   * different channels, which would otherwise halve the apparent gap and make
   * the slow phase look fast.
   */
  uint32_t burst_min_spacing_ms{100};
};

/// Most sightings the rate estimator ever needs to remember.
static constexpr uint8_t kMaxRateSampleCount = 12;

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
   * Always records the sighting, whether or not the bridge is suppressed, so
   * the advertising diagnostics mean something during a normal session too.
   *
   * Returns true when this advertisement ends suppression, in which case the
   * caller should re-enable its BLE client before the advertisement reaches
   * the stock client so the same scan result can drive the reconnection.
   */
  bool on_advertisement(uint32_t now);

  /** Abandon suppression because something outside the timeout asked for the
   * controllers back — a Home Assistant button, an automation, a service call.
   */
  void request_reconnect(uint32_t now);

  /// Called every loop with the live session state. See RideIdleAction.
  RideIdleAction poll(uint32_t now, bool session_ready);

  /// True while the bridge is deliberately refusing to reconnect.
  bool suppressed() const { return this->suppressed_; }
  /// True once the controller advertisement has been absent long enough that
  /// the next one will be treated as a wake.
  bool sleep_confirmed() const { return this->sleep_confirmed_; }

  /// True once the controller's advertising rate has dropped out of the fast
  /// regime, after which a return to fast advertising is accepted as a wake.
  bool slowed() const { return this->slowed_; }

  /** Mean gap between recent sightings, or 0 before enough have been seen.
   *
   * This is the quantity both thresholds compare against, so exposing it makes
   * a misbehaving re-arm diagnosable from Home Assistant instead of from a log
   * capture.
   */
  uint32_t advertising_rate_ms() const;
  uint32_t idle_disconnect_count() const { return this->idle_disconnect_count_; }

  /// Quiet time so far. Only meaningful while a session is ready.
  uint32_t idle_elapsed_ms(uint32_t now) const {
    return static_cast<uint32_t>(now - this->last_activity_ms_);
  }

  /// True once any matching advertisement has been seen since boot. Until then
  /// the age below is meaningless rather than merely large.
  bool has_advertisement() const { return this->has_advertisement_; }

  /// Time since the controller was last seen advertising.
  uint32_t advertisement_age_ms(uint32_t now) const {
    return static_cast<uint32_t>(now - this->last_advertisement_ms_);
  }

  /** True while the controller counts as awake and broadcasting.
   *
   * Deliberately the same comparison that arms the re-arm, so a diagnostic
   * reading false means the next advertisement will reconnect.
   */
  bool advertising(uint32_t now) const;

 protected:
  void begin_suppression_(uint32_t now);
  void record_rate_sample_(uint32_t now);
  uint8_t rate_capacity_() const;

  RideIdleConfig config_{};
  uint32_t last_activity_ms_{0};
  uint32_t suppressed_since_ms_{0};
  uint32_t last_advertisement_ms_{0};
  uint32_t idle_disconnect_count_{0};
  // Sighting times feeding the rate estimate, oldest first.
  uint32_t rate_times_[kMaxRateSampleCount]{};
  uint8_t rate_length_{0};
  bool suppressed_{false};
  bool sleep_confirmed_{false};
  bool slowed_{false};
  bool has_advertisement_{false};
};

}  // namespace esphome::zwift_ride_hid
