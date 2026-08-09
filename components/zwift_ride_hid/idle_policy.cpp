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
  this->rate_length_ = 0;
}

void RideIdlePolicy::on_activity(uint32_t now) { this->last_activity_ms_ = now; }

bool RideIdlePolicy::on_advertisement(uint32_t now) {
  this->last_advertisement_ms_ = now;
  this->has_advertisement_ = true;

  if (!this->suppressed_) {
    // Outside suppression this only feeds the advertising diagnostics.
    this->rate_length_ = 0;
    return false;
  }

  this->record_rate_sample_(now);
  const uint32_t rate = this->advertising_rate_ms();
  if (rate == 0) {
    // Not enough sightings yet to say anything about the rate.
    return false;
  }

  // Hysteresis on one measured quantity. Leaving the fast regime latches;
  // returning to it is the wake. Anything less specific reconnects into a
  // controller that is merely winding down, which is what a gap-length rule
  // and a fixed sighting count both did.
  if (!this->slowed_) {
    if (rate >= this->config_.slow_rate_ms) {
      this->slowed_ = true;
    }
    return false;
  }
  if (rate > this->config_.wake_rate_ms) {
    return false;
  }

  this->suppressed_ = false;
  this->sleep_confirmed_ = false;
  this->slowed_ = false;
  this->rate_length_ = 0;
  return true;
}

void RideIdlePolicy::record_rate_sample_(uint32_t now) {
  const uint8_t capacity = this->rate_capacity_();
  // Collapse the same advertising event seen on more than one channel, which
  // would otherwise halve the apparent gap and make the slow phase look fast.
  if (this->rate_length_ != 0 &&
      static_cast<uint32_t>(now - this->rate_times_[this->rate_length_ - 1]) <
          this->config_.burst_min_spacing_ms) {
    this->rate_times_[this->rate_length_ - 1] = now;
    return;
  }
  if (this->rate_length_ < capacity) {
    this->rate_times_[this->rate_length_++] = now;
    return;
  }
  for (uint8_t i = 1; i < capacity; i++) {
    this->rate_times_[i - 1] = this->rate_times_[i];
  }
  this->rate_times_[capacity - 1] = now;
}

uint8_t RideIdlePolicy::rate_capacity_() const {
  uint8_t count = this->config_.rate_sample_count;
  if (count < 3)
    count = 3;
  if (count > kMaxRateSampleCount)
    count = kMaxRateSampleCount;
  return count;
}

uint32_t RideIdlePolicy::advertising_rate_ms() const {
  const uint8_t capacity = this->rate_capacity_();
  if (this->rate_length_ < capacity) {
    return 0;
  }
  const uint32_t span =
      static_cast<uint32_t>(this->rate_times_[capacity - 1] - this->rate_times_[0]);
  return span / (capacity - 1);
}

void RideIdlePolicy::request_reconnect(uint32_t now) {
  this->suppressed_ = false;
  this->sleep_confirmed_ = false;
  this->slowed_ = false;
  this->rate_length_ = 0;
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
      this->rate_length_ = 0;
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
    this->rate_length_ = 0;
    return RideIdleAction::REARM;
  }

  // Silence latches "slowed" as well. One slow interval of quiet is already
  // slower than the fast regime, and this also covers a controller that stops
  // outright and one that is already asleep when suppression begins. Both then
  // reconnect once fast advertising returns.
  if (!this->slowed_ &&
      timeout_elapsed(now, this->last_advertisement_ms_,
                      this->config_.slow_rate_ms)) {
    this->slowed_ = true;
    this->rate_length_ = 0;
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
  // The controller is still advertising fast at this instant, so the rate must
  // be observed dropping before any wake can be recognised.
  this->slowed_ = false;
  this->rate_length_ = 0;
  this->suppressed_since_ms_ = now;
  // The controller was demonstrably present a moment ago, so treat the
  // disconnect itself as the most recent sighting. Without this the
  // advertising diagnostic would read "gone" the instant the link dropped.
  this->last_advertisement_ms_ = now;
  this->has_advertisement_ = true;
  this->idle_disconnect_count_++;
}

}  // namespace esphome::zwift_ride_hid
