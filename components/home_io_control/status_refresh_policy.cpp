/// @file status_refresh_policy.cpp
/// @brief Status refresh schedule implementation.
/// @ingroup hioc_hub

#include "status_refresh_policy.h"

#include <algorithm>

namespace esphome {
namespace home_io_control {

namespace {

/// True when @p deadline has been reached at @p now, correct across the millis() rollover.
bool reached(uint32_t now, uint32_t deadline) { return static_cast<int32_t>(now - deadline) >= 0; }

}  // namespace

void StatusRefreshPolicy::set_interval(const std::string &device_id, uint32_t interval_ms, uint32_t now,
                                       uint32_t random) {
  if (interval_ms == 0) {
    entries_.erase(device_id);
    return;
  }
  Entry &e = entries_[device_id];
  e.interval_ms = interval_ms;
  e.next_refresh = now + status_refresh_jittered_interval_ms(interval_ms, random);
}

uint32_t StatusRefreshPolicy::get_interval(const std::string &device_id) const {
  const auto it = entries_.find(device_id);
  return it != entries_.end() ? it->second.interval_ms : 0;
}

bool StatusRefreshPolicy::has_due(uint32_t now) const {
  return std::any_of(entries_.begin(), entries_.end(),
                     [now](const auto &entry) { return reached(now, entry.second.next_refresh); });
}

std::optional<std::string> StatusRefreshPolicy::pop_due_device(uint32_t now, uint32_t random) {
  for (auto &[device_id, e] : entries_) {
    if (!reached(now, e.next_refresh))
      continue;
    e.next_refresh = now + status_refresh_jittered_interval_ms(e.interval_ms, random);
    return device_id;
  }
  return std::nullopt;
}

void StatusRefreshPolicy::on_status_reported(const std::string &device_id, uint32_t now, uint32_t random) {
  const auto it = entries_.find(device_id);
  if (it == entries_.end())
    return;
  it->second.next_refresh = now + status_refresh_jittered_interval_ms(it->second.interval_ms, random);
}

uint32_t StatusRefreshPolicy::get_next_refresh(const std::string &device_id) const {
  const auto it = entries_.find(device_id);
  return it != entries_.end() ? it->second.next_refresh : 0;
}

}  // namespace home_io_control
}  // namespace esphome
