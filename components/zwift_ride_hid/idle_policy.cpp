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
}

void RideIdlePolicy::on_activity(uint32_t now) { this->last_activity_ms_ = now; }

bool RideIdlePolicy::on_advertisement(uint32_t now) {
  const bool wakes = this->suppressed_ && this->sleep_confirmed_;
  // Record the sighting either way. Outside suppression this only feeds the
  // advertising diagnostics; inside it, an advertisement from a controller that
  // has not yet slept restarts the absence window rather than reconnecting
  // straight back into the session that was just dropped.
  this->last_advertisement_ms_ = now;
  this->has_advertisement_ = true;
  if (!wakes) {
    return false;
  }
  this->suppressed_ = false;
  this->sleep_confirmed_ = false;
  return true;
}

void RideIdlePolicy::request_reconnect(uint32_t now) {
  this->suppressed_ = false;
  this->sleep_confirmed_ = false;
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
    return RideIdleAction::REARM;
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
  this->suppressed_since_ms_ = now;
  // The controller was demonstrably present a moment ago, so treat the
  // disconnect itself as the most recent sighting. Without this the
  // advertising diagnostic would read "gone" the instant the link dropped.
  this->last_advertisement_ms_ = now;
  this->has_advertisement_ = true;
  this->idle_disconnect_count_++;
}

}  // namespace esphome::zwift_ride_hid
