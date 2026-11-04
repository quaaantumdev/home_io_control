#pragma once

/// @file status_refresh_policy.h
/// @brief Schedule of the opt-in status refresh of a device at rest.
/// @ingroup hioc_hub
///
/// Owns the per-device schedule behind `status_refresh_interval`: a status read once the device has
/// not reported its status for a whole interval. Kept apart from StatusPollPolicy for the reason
/// RainPollPolicy is: that one runs bounded tracking windows which end when a device stops, this one
/// is an unbounded periodic schedule. Pure logic with injected timestamps and random numbers — fully
/// host-testable without a clock or a random source.

#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace esphome {
namespace home_io_control {

/// Per-refresh offset, as a percentage of the interval, applied symmetrically around it, so covers
/// that reported at the same moment (at boot, or after a group command) do not stay in step. The
/// same spread as the rain sensor poll (RAIN_POLL_JITTER_PERCENT); the mean stays the configured
/// interval.
static constexpr uint32_t STATUS_REFRESH_JITTER_PERCENT = 10;

/// @brief One refresh interval moved by a uniform offset of up to STATUS_REFRESH_JITTER_PERCENT either way.
///
/// The offset scales @p random over the whole 32-bit range, so 0 gives the lower bound, UINT32_MAX
/// the upper bound and the middle of the range the unmodified interval. Computed in 64 bit so a
/// 24 hour interval cannot overflow.
/// @param interval_ms The configured interval.
/// @param random A uniformly distributed 32-bit random number.
/// @return The delay until the next refresh, in [interval - jitter, interval + jitter].
[[nodiscard]] inline uint32_t status_refresh_jittered_interval_ms(uint32_t interval_ms, uint32_t random) {
  const uint64_t jitter = static_cast<uint64_t>(interval_ms) * STATUS_REFRESH_JITTER_PERCENT / 100;
  const uint64_t offset = (static_cast<uint64_t>(random) * 2 * jitter) / UINT32_MAX;  // 0 .. 2 * jitter
  return static_cast<uint32_t>(interval_ms - jitter + offset);
}

/// @brief Per-device schedule of the status refresh at rest.
///
/// Every status the device reports, whoever asked for it, moves its next refresh a whole interval
/// out, so a device that is commanded or read regularly anyway is never read on top of that.
class StatusRefreshPolicy {
 public:
  /// Set the refresh interval and schedule the first refresh one jittered interval after @p now.
  /// 0 removes the device from the schedule.
  void set_interval(const std::string &device_id, uint32_t interval_ms, uint32_t now, uint32_t random);
  /// Return the configured interval (0 when the device is not scheduled).
  [[nodiscard]] uint32_t get_interval(const std::string &device_id) const;

  /// True when any device's refresh is due at @p now. Lets the caller draw a random number only when
  /// pop_due_device() will use it.
  [[nodiscard]] bool has_due(uint32_t now) const;

  /// Return one device whose refresh is due and immediately schedule its next refresh one jittered
  /// interval after @p now, so a refresh that gets no answer, or that pairing drops from the operation
  /// queue, is tried again an interval later rather than on every loop. Returns nullopt when nothing is
  /// due. Comparison is wrap-safe across the 32-bit millis() rollover.
  [[nodiscard]] std::optional<std::string> pop_due_device(uint32_t now, uint32_t random);

  /// The device reported its status at @p now: schedule its next refresh one jittered interval later.
  /// No-op for a device that is not scheduled.
  void on_status_reported(const std::string &device_id, uint32_t now, uint32_t random);

  /// Absolute millis() time of the device's next refresh (0 for an unscheduled device).
  [[nodiscard]] uint32_t get_next_refresh(const std::string &device_id) const;

 private:
  /// Schedule state of one device.
  struct Entry {
    uint32_t interval_ms{0};   ///< Configured interval.
    uint32_t next_refresh{0};  ///< Absolute millis() time of the next refresh.
  };

  std::map<std::string, Entry> entries_;  ///< Scheduled devices, keyed by device id.
};

}  // namespace home_io_control
}  // namespace esphome
