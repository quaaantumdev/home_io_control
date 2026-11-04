/// @file status_refresh_policy_test.cpp
/// @brief Unit tests for StatusRefreshPolicy and its jitter function.
///
/// Covers: jitter bounds (including an overflow-prone 24 h interval), the first refresh one interval
/// after the opt-in, re-arm from the pop time, a reported status pushing the refresh out, spreading
/// of devices that share an interval, and millis() rollover.

#include "status_refresh_policy.h"

#include <gtest/gtest.h>

#include <string>

using namespace esphome::home_io_control;

static constexpr uint32_t T0 = 1000000;          ///< Arbitrary base timestamp (ms).
static constexpr uint32_t INTERVAL = 900000;     ///< 15 minutes, the documented recommendation.
static constexpr uint32_t JITTER = 90000;        ///< 10 % of INTERVAL.
static constexpr uint32_t MID = UINT32_MAX / 2;  ///< Middle of the random range.
static constexpr uint32_t RANDOM_MAX = UINT32_MAX;

// ============================================================================
// Pure jitter function
// ============================================================================

TEST(StatusRefreshPolicy, JitteredIntervalSpansPlusMinusTenPercent) {
  EXPECT_EQ(status_refresh_jittered_interval_ms(INTERVAL, 0), 810000u);
  EXPECT_EQ(status_refresh_jittered_interval_ms(INTERVAL, RANDOM_MAX), 990000u);
  EXPECT_NEAR(static_cast<double>(status_refresh_jittered_interval_ms(INTERVAL, MID)), 900000.0, 1.0)
      << "mid-range random leaves the interval";
}

TEST(StatusRefreshPolicy, JitteredIntervalNeverLeavesItsRangeNorOverflows) {
  for (uint32_t interval : {60000u, INTERVAL, 86400000u}) {
    const uint32_t jitter = interval / 10;
    for (uint32_t random : {0u, 1u, MID, RANDOM_MAX - 1, RANDOM_MAX}) {
      const uint32_t delay = status_refresh_jittered_interval_ms(interval, random);
      EXPECT_GE(delay, interval - jitter) << "interval " << interval << " random " << random;
      EXPECT_LE(delay, interval + jitter) << "interval " << interval << " random " << random;
    }
  }
}

// ============================================================================
// Scheduling
// ============================================================================

TEST(StatusRefreshPolicy, NothingIsDueWithoutAnInterval) {
  StatusRefreshPolicy policy;
  EXPECT_FALSE(policy.has_due(T0 + 10 * INTERVAL));
  EXPECT_FALSE(policy.pop_due_device(T0 + 10 * INTERVAL, 0).has_value());
  EXPECT_EQ(policy.get_interval("DEV"), 0u);
  EXPECT_EQ(policy.get_next_refresh("DEV"), 0u);
}

TEST(StatusRefreshPolicy, FirstRefreshIsDueOneIntervalAfterTheOptIn) {
  StatusRefreshPolicy policy;
  policy.set_interval("DEV", INTERVAL, T0, MID);

  EXPECT_EQ(policy.get_interval("DEV"), INTERVAL);
  EXPECT_FALSE(policy.has_due(T0 + INTERVAL - JITTER - 1));
  EXPECT_FALSE(policy.pop_due_device(T0 + INTERVAL - JITTER - 1, MID).has_value());
  EXPECT_TRUE(policy.has_due(T0 + INTERVAL + JITTER));
  const auto due = policy.pop_due_device(T0 + INTERVAL + JITTER, MID);
  ASSERT_TRUE(due.has_value());
  EXPECT_EQ(*due, "DEV");
}

TEST(StatusRefreshPolicy, PopRearmsFromThePopTime) {
  StatusRefreshPolicy policy;
  policy.set_interval("DEV", INTERVAL, T0, 0);
  const uint32_t pop_time = T0 + INTERVAL + 4000;  // loop() ran late

  ASSERT_TRUE(policy.pop_due_device(pop_time, 0).has_value());

  EXPECT_EQ(policy.get_next_refresh("DEV"), pop_time + 810000) << "a missed refresh is tried an interval later";
  EXPECT_FALSE(policy.has_due(pop_time + 1)) << "not again on the next loop";
}

TEST(StatusRefreshPolicy, AReportedStatusPushesTheRefreshOut) {
  StatusRefreshPolicy policy;
  policy.set_interval("DEV", INTERVAL, T0, 0);  // first refresh at T0 + 810 s
  const uint32_t reported_at = T0 + 800000;

  policy.on_status_reported("DEV", reported_at, 0);

  EXPECT_EQ(policy.get_next_refresh("DEV"), reported_at + 810000);
  EXPECT_FALSE(policy.has_due(T0 + 810000)) << "the device answered 10 s before; no read is needed";
}

TEST(StatusRefreshPolicy, AReportedStatusOfAnUnscheduledDeviceSchedulesNothing) {
  StatusRefreshPolicy policy;
  policy.on_status_reported("DEV", T0, 0);

  EXPECT_EQ(policy.get_next_refresh("DEV"), 0u);
  EXPECT_FALSE(policy.has_due(T0 + 10 * INTERVAL));
}

TEST(StatusRefreshPolicy, ZeroIntervalRemovesTheDevice) {
  StatusRefreshPolicy policy;
  policy.set_interval("DEV", INTERVAL, T0, 0);
  policy.set_interval("DEV", 0, T0, 0);

  EXPECT_EQ(policy.get_interval("DEV"), 0u);
  EXPECT_FALSE(policy.pop_due_device(T0 + 10 * INTERVAL, 0).has_value());
}

TEST(StatusRefreshPolicy, DevicesThatReportedTogetherComeDueAtDifferentTimes) {
  StatusRefreshPolicy policy;
  policy.set_interval("A", INTERVAL, T0, 0);
  policy.set_interval("B", INTERVAL, T0, 0);

  // A group command: both report in the same moment, the random draws differ.
  policy.on_status_reported("A", T0 + 1000, 0);
  policy.on_status_reported("B", T0 + 1000, RANDOM_MAX);

  EXPECT_EQ(policy.get_next_refresh("A"), T0 + 1000 + 810000);
  EXPECT_EQ(policy.get_next_refresh("B"), T0 + 1000 + 990000);
}

TEST(StatusRefreshPolicy, TwoDueDevicesPopOnePerCall) {
  StatusRefreshPolicy policy;
  policy.set_interval("A", INTERVAL, T0, 0);
  policy.set_interval("B", INTERVAL, T0, 0);
  const uint32_t now = T0 + INTERVAL;

  const auto first = policy.pop_due_device(now, MID);
  const auto second = policy.pop_due_device(now, MID);
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_NE(*first, *second);
  EXPECT_FALSE(policy.pop_due_device(now, MID).has_value());
}

TEST(StatusRefreshPolicy, ScheduleSurvivesTheMillisRollover) {
  StatusRefreshPolicy policy;
  const uint32_t near_wrap = UINT32_MAX - 5000;
  policy.set_interval("DEV", INTERVAL, near_wrap, 0);  // next refresh lands after the rollover

  EXPECT_FALSE(policy.has_due(near_wrap));
  EXPECT_FALSE(policy.pop_due_device(near_wrap + 810000 - 1, 0).has_value());
  EXPECT_TRUE(policy.pop_due_device(near_wrap + 810000, 0).has_value());
}
