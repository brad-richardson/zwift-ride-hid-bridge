// SPDX-License-Identifier: GPL-3.0-only
#include "idle_policy.h"

namespace esphome::zwift_ride_hid {
namespace {

// Unsigned subtraction is rollover-safe as long as the interval is shorter
// than 2^31 milliseconds. The configuration schema bounds every interval far
// below that.
constexpr bool timeout_elapsed(uint32_t now, uint32_t started_at,
                               uint32_t timeout_ms) {
  return static_cast<uint32_t>(now - started_at) >= timeout_ms;
}

// Compile-time coverage for the millis() wrap boundary: UINT32_MAX-4 to 4 is
// nine elapsed ticks, not a negative duration.
static_assert(timeout_elapsed(4, UINT32_MAX - 4, 9));
static_assert(!timeout_elapsed(4, UINT32_MAX - 4, 10));

}  // namespace

void RideIdlePolicy::on_session_ready(uint32_t now) {
  this->last_activity_ms_ = now;
  this->suppressed_ = false;
  this->sleep_confirmed_ = false;
  this->slowed_ = false;
  this->burst_length_ = 0;
}

void RideIdlePolicy::on_activity(uint32_t now) { this->last_activity_ms_ = now; }

bool RideIdlePolicy::on_advertisement(uint32_t now) {
  const uint32_t gap = this->has_advertisement_
                           ? static_cast<uint32_t>(now - this->last_advertisement_ms_)
                           : 0;
  const bool had_advertisement = this->has_advertisement_;
  this->last_advertisement_ms_ = now;
  this->has_advertisement_ = true;

  if (!this->suppressed_) {
    // Outside suppression this only feeds the advertising diagnostics.
    this->burst_length_ = 0;
    return false;
  }

  // A gap this long proves the controller has left fast advertising. It stays
  // latched: the ramp toward sleep only widens from here.
  if (had_advertisement && gap >= this->config_.slow_gap_ms) {
    this->slowed_ = true;
    this->burst_length_ = 0;
  }

  this->record_burst_sighting_(now);

  // Re-arm only on a genuine return to fast advertising. Reconnecting on the
  // first sparse advertisement is what previously dropped the bridge back into
  // a controller that was merely ramping down, not asleep.
  if (!this->slowed_ || !this->burst_detected_(now)) {
    return false;
  }
  this->suppressed_ = false;
  this->sleep_confirmed_ = false;
  this->slowed_ = false;
  this->burst_length_ = 0;
  return true;
}

void RideIdlePolicy::record_burst_sighting_(uint32_t now) {
  const uint8_t capacity = this->burst_capacity_();
  // Collapse the same advertising event seen on more than one channel, so a
  // single slow-phase event cannot look like a fast burst.
  if (this->burst_length_ != 0 &&
      static_cast<uint32_t>(now - this->burst_times_[this->burst_length_ - 1]) <
          this->config_.burst_min_spacing_ms) {
    this->burst_times_[this->burst_length_ - 1] = now;
    return;
  }
  if (this->burst_length_ < capacity) {
    this->burst_times_[this->burst_length_++] = now;
    return;
  }
  for (uint8_t i = 1; i < capacity; i++) {
    this->burst_times_[i - 1] = this->burst_times_[i];
  }
  this->burst_times_[capacity - 1] = now;
}

uint8_t RideIdlePolicy::burst_capacity_() const {
  uint8_t count = this->config_.wake_burst_count;
  if (count < 2)
    count = 2;
  if (count > kMaxWakeBurstCount)
    count = kMaxWakeBurstCount;
  return count;
}

bool RideIdlePolicy::burst_detected_(uint32_t now) const {
  const uint8_t capacity = this->burst_capacity_();
  if (this->burst_length_ < capacity) {
    return false;
  }
  return static_cast<uint32_t>(now - this->burst_times_[0]) <=
         this->config_.wake_burst_window_ms;
}

void RideIdlePolicy::request_reconnect(uint32_t now) {
  this->suppressed_ = false;
  this->sleep_confirmed_ = false;
  this->slowed_ = false;
  this->burst_length_ = 0;
  this->last_activity_ms_ = now;
}

bool RideIdlePolicy::advertising(uint32_t now) const {
  return this->has_advertisement_ &&
         !timeout_elapsed(now, this->last_advertisement_ms_,
                          this->config_.sleep_confirm_ms);
}

RideIdleAction RideIdlePolicy::poll(uint32_t now, bool session_ready) {
  if (session_ready) {
    if (this->suppressed_) {
      // A live session always wins over stale suppression, but it must also
      // restart the quiet window. Reporting a second disconnect on the very
      // next iteration would otherwise machine-gun the link.
      this->suppressed_ = false;
      this->sleep_confirmed_ = false;
      this->slowed_ = false;
      this->burst_length_ = 0;
      this->last_activity_ms_ = now;
      return RideIdleAction::NONE;
    }
    if (this->config_.idle_timeout_ms != 0 &&
        timeout_elapsed(now, this->last_activity_ms_,
                        this->config_.idle_timeout_ms)) {
      this->begin_suppression_(now);
      return RideIdleAction::DISCONNECT;
    }
    return RideIdleAction::NONE;
  }

  if (!this->suppressed_) {
    return RideIdleAction::NONE;
  }

  if (this->config_.max_suppression_ms != 0 &&
      timeout_elapsed(now, this->suppressed_since_ms_,
                      this->config_.max_suppression_ms)) {
    this->suppressed_ = false;
    this->sleep_confirmed_ = false;
    this->slowed_ = false;
    this->burst_length_ = 0;
    return RideIdleAction::REARM;
  }

  // Silence latches "slowed" too, covering a controller that stops outright
  // rather than ramping down, and one that is already asleep when suppression
  // begins. Both then reconnect on the wake burst.
  if (!this->slowed_ &&
      timeout_elapsed(now, this->last_advertisement_ms_,
                      this->config_.slow_gap_ms)) {
    this->slowed_ = true;
    this->burst_length_ = 0;
  }

  if (!this->sleep_confirmed_ &&
      timeout_elapsed(now, this->last_advertisement_ms_,
                      this->config_.sleep_confirm_ms)) {
    this->sleep_confirmed_ = true;
  }
  return RideIdleAction::NONE;
}

void RideIdlePolicy::begin_suppression_(uint32_t now) {
  this->suppressed_ = true;
  this->sleep_confirmed_ = false;
  // The controller is still advertising fast at this instant, so the burst
  // detector must not fire until a slow gap proves it has ramped down.
  this->slowed_ = false;
  this->burst_length_ = 0;
  this->suppressed_since_ms_ = now;
  // The controller was demonstrably present a moment ago, so treat the
  // disconnect itself as the most recent sighting. Without this the
  // advertising diagnostic would read "gone" the instant the link dropped.
  this->last_advertisement_ms_ = now;
  this->has_advertisement_ = true;
  this->idle_disconnect_count_++;
}

}  // namespace esphome::zwift_ride_hid
