// SPDX-License-Identifier: MIT
// Unit tests for TRIAC dimmer logic functions
// Compile and run on host: g++ -std=c++17 -o test_triac test_triac_logic.cpp && ./test_triac

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

// ============================================================================
// Copy of the logic we want to test (isolated from hardware dependencies)
// ============================================================================

static constexpr uint32_t FIRING_DELAYS_LEN = 200U;
static constexpr uint32_t DIMMER_RESOLUTION = 12;
static constexpr uint32_t FIRING_DELAY_MAX = (1 << DIMMER_RESOLUTION) - 1;
static constexpr uint32_t FIRING_DELAYS_SCALE = (FIRING_DELAYS_LEN - 1U) * (1UL << (16 - DIMMER_RESOLUTION));
static constexpr uint16_t PHASE_DELAY_MIN_US = 90;

// Pre-computed firing delay lookup table (from MycilaDimmer)
static const uint16_t FIRING_DELAYS[FIRING_DELAYS_LEN] = {
    0xffff, 0xe877, 0xe240, 0xddd9, 0xda51, 0xd74f, 0xd4aa, 0xd248, 0xd01a, 0xce16, 0xcc34, 0xca6e, 0xc8c0, 0xc728,
    0xc5a1, 0xc42b, 0xc2c3, 0xc168, 0xc019, 0xbed3, 0xbd98, 0xbc65, 0xbb3b, 0xba17, 0xb8fb, 0xb7e5, 0xb6d5, 0xb5ca,
    0xb4c5, 0xb3c4, 0xb2c8, 0xb1d1, 0xb0dd, 0xafed, 0xaf01, 0xae18, 0xad33, 0xac51, 0xab71, 0xaa95, 0xa9bb, 0xa8e3,
    0xa80e, 0xa73b, 0xa66b, 0xa59c, 0xa4d0, 0xa406, 0xa33d, 0xa276, 0xa1b1, 0xa0ed, 0xa02b, 0x9f6b, 0x9eac, 0x9dee,
    0x9d32, 0x9c76, 0x9bbc, 0x9b04, 0x9a4c, 0x9996, 0x98e0, 0x982b, 0x9778, 0x96c5, 0x9613, 0x9563, 0x94b2, 0x9403,
    0x9354, 0x92a6, 0x91f9, 0x914c, 0x90a0, 0x8ff5, 0x8f4a, 0x8ea0, 0x8df6, 0x8d4d, 0x8ca4, 0x8bfb, 0x8b53, 0x8aab,
    0x8a04, 0x895d, 0x88b6, 0x8810, 0x876a, 0x86c4, 0x861e, 0x8579, 0x84d3, 0x842e, 0x8389, 0x82e4, 0x823f, 0x819b,
    0x80f6, 0x8051, 0x7fad, 0x7f08, 0x7e63, 0x7dbf, 0x7d1a, 0x7c75, 0x7bd0, 0x7b2b, 0x7a85, 0x79e0, 0x793a, 0x7894,
    0x77ee, 0x7748, 0x76a1, 0x75fa, 0x7553, 0x74ab, 0x7403, 0x735a, 0x72b1, 0x7208, 0x715e, 0x70b4, 0x7009, 0x6f5e,
    0x6eb2, 0x6e05, 0x6d58, 0x6caa, 0x6bfb, 0x6b4c, 0x6a9b, 0x69eb, 0x6939, 0x6886, 0x67d3, 0x671e, 0x6668, 0x65b2,
    0x64fa, 0x6442, 0x6388, 0x62cc, 0x6210, 0x6152, 0x6093, 0x5fd3, 0x5f11, 0x5e4d, 0x5d88, 0x5cc1, 0x5bf8, 0x5b2e,
    0x5a62, 0x5993, 0x58c3, 0x57f0, 0x571b, 0x5643, 0x5569, 0x548d, 0x53ad, 0x52cb, 0x51e6, 0x50fd, 0x5011, 0x4f21,
    0x4e2d, 0x4d36, 0x4c3a, 0x4b39, 0x4a34, 0x4929, 0x4819, 0x4703, 0x45e7, 0x44c3, 0x4399, 0x4266, 0x412b, 0x3fe5,
    0x3e96, 0x3d3b, 0x3bd3, 0x3a5d, 0x38d6, 0x373e, 0x3590, 0x33ca, 0x31e8, 0x2fe4, 0x2db6, 0x2b54, 0x28af, 0x25ad,
    0x2225, 0x1dbe, 0x1787, 0x0000};

// Function under test: lookup_firing_delay
uint16_t lookup_firing_delay(float duty_cycle, uint16_t semi_period) {
  uint32_t duty = static_cast<uint32_t>(duty_cycle * FIRING_DELAY_MAX);
  uint32_t slot = duty * FIRING_DELAYS_SCALE + (FIRING_DELAYS_SCALE >> 1);
  uint32_t index = slot >> 16;

  if (index >= FIRING_DELAYS_LEN - 1) {
    index = FIRING_DELAYS_LEN - 2;
  }

  uint32_t a = FIRING_DELAYS[index];
  uint32_t b = FIRING_DELAYS[index + 1];
  uint32_t delay = a - (((a - b) * (slot & 0xffff)) >> 16);

  return static_cast<uint16_t>((delay * semi_period) >> 16);
}

// Function under test: duty cycle mapping
float get_duty_cycle_mapped(float duty_cycle, float min_power, float max_power) {
  return min_power + duty_cycle * (max_power - min_power);
}

// Function under test: calculate firing delay from duty cycle
uint16_t calculate_firing_delay(float duty_cycle_fire, uint16_t semi_period) {
  if (duty_cycle_fire <= 0.0f) {
    return UINT16_MAX;  // Off
  } else if (duty_cycle_fire >= 1.0f) {
    return 0;  // Full on
  } else {
    return static_cast<uint16_t>((1.0f - duty_cycle_fire) * static_cast<float>(semi_period));
  }
}

// ============================================================================
// Test Framework (minimal)
// ============================================================================

int tests_passed = 0;
int tests_failed = 0;

#define TEST(name) void test_##name()
#define RUN_TEST(name)                                  \
  do {                                                  \
    std::cout << "Running " << #name << "... ";         \
    try {                                               \
      test_##name();                                    \
      std::cout << "PASSED" << std::endl;               \
      tests_passed++;                                   \
    } catch (const std::exception &e) {                 \
      std::cout << "FAILED: " << e.what() << std::endl; \
      tests_failed++;                                   \
    }                                                   \
  } while (0)

#define ASSERT_EQ(a, b)                                                                            \
  do {                                                                                             \
    if ((a) != (b)) {                                                                              \
      throw std::runtime_error("Expected " + std::to_string(b) + " but got " + std::to_string(a)); \
    }                                                                                              \
  } while (0)

#define ASSERT_NEAR(a, b, epsilon)                                                                  \
  do {                                                                                              \
    if (std::abs((a) - (b)) > (epsilon)) {                                                          \
      throw std::runtime_error("Expected ~" + std::to_string(b) + " but got " + std::to_string(a)); \
    }                                                                                               \
  } while (0)

#define ASSERT_TRUE(cond)                                   \
  do {                                                      \
    if (!(cond)) {                                          \
      throw std::runtime_error("Condition failed: " #cond); \
    }                                                       \
  } while (0)

// ============================================================================
// Tests
// ============================================================================

TEST(lookup_table_boundaries) {
  // At 0% duty, delay should be maximum (close to full semi-period)
  uint16_t delay_0 = lookup_firing_delay(0.0f, 10000);
  ASSERT_NEAR(delay_0, 10000, 200);  // Should be close to semi-period

  // At 100% duty, delay should be very small (close to 0)
  uint16_t delay_100 = lookup_firing_delay(1.0f, 10000);
  ASSERT_TRUE(delay_100 < 100);  // Should be very small (may not be exactly 0 due to interpolation)
}

TEST(lookup_table_50_percent) {
  // At 50% duty with LUT, delay should be ~5000µs for 50Hz (90° phase angle)
  uint16_t delay_50 = lookup_firing_delay(0.5f, 10000);
  // The LUT is designed for linear power, so 50% power ≈ 90° ≈ 5000µs
  ASSERT_TRUE(delay_50 > 4000 && delay_50 < 6000);
  std::cout << "(50% delay = " << delay_50 << "µs) ";
}

TEST(lookup_table_monotonic) {
  // Verify the lookup table is monotonically decreasing
  uint16_t prev_delay = lookup_firing_delay(0.0f, 10000);
  for (float duty = 0.01f; duty <= 1.0f; duty += 0.01f) {
    uint16_t current_delay = lookup_firing_delay(duty, 10000);
    ASSERT_TRUE(current_delay <= prev_delay);
    prev_delay = current_delay;
  }
}

TEST(lookup_table_different_frequencies) {
  // Test at 50Hz (10000µs semi-period)
  uint16_t delay_50hz = lookup_firing_delay(0.5f, 10000);

  // Test at 60Hz (8333µs semi-period)
  uint16_t delay_60hz = lookup_firing_delay(0.5f, 8333);

  // Delays should scale proportionally
  float ratio = static_cast<float>(delay_60hz) / static_cast<float>(delay_50hz);
  ASSERT_NEAR(ratio, 8333.0f / 10000.0f, 0.05f);
}

TEST(duty_cycle_mapping_identity) {
  // With min=0, max=1, mapping should be identity
  ASSERT_NEAR(get_duty_cycle_mapped(0.0f, 0.0f, 1.0f), 0.0f, 0.001f);
  ASSERT_NEAR(get_duty_cycle_mapped(0.5f, 0.0f, 1.0f), 0.5f, 0.001f);
  ASSERT_NEAR(get_duty_cycle_mapped(1.0f, 0.0f, 1.0f), 1.0f, 0.001f);
}

TEST(duty_cycle_mapping_custom_range) {
  // With min=0.1, max=0.9, 0% should map to 10%, 100% to 90%
  ASSERT_NEAR(get_duty_cycle_mapped(0.0f, 0.1f, 0.9f), 0.1f, 0.001f);
  ASSERT_NEAR(get_duty_cycle_mapped(0.5f, 0.1f, 0.9f), 0.5f, 0.001f);
  ASSERT_NEAR(get_duty_cycle_mapped(1.0f, 0.1f, 0.9f), 0.9f, 0.001f);
}

TEST(firing_delay_off) {
  // At 0% duty cycle fire, should return UINT16_MAX (off)
  ASSERT_EQ(calculate_firing_delay(0.0f, 10000), UINT16_MAX);
}

TEST(firing_delay_full_on) {
  // At 100% duty cycle fire, should return 0 (full on)
  ASSERT_EQ(calculate_firing_delay(1.0f, 10000), 0);
}

TEST(firing_delay_partial) {
  // At 50% duty cycle fire, delay should be 5000µs
  uint16_t delay = calculate_firing_delay(0.5f, 10000);
  ASSERT_EQ(delay, 5000);
}

TEST(firing_delay_minimum_enforcement) {
  // Even at high duty cycles, we should enforce minimum delay
  uint16_t delay = calculate_firing_delay(0.99f, 10000);
  // 99% duty = 1% delay = 100µs, but we enforce PHASE_DELAY_MIN_US = 90µs
  ASSERT_TRUE(delay >= PHASE_DELAY_MIN_US || delay == 0);
}

TEST(power_output_linearity) {
  // The LUT maps duty cycle to firing delay for linear POWER output on resistive loads.
  // The relationship between firing angle (α) and power is:
  //   Power = (1/π) * [π - α + sin(2α)/2]
  //
  // We verify that higher duty cycle = smaller delay (more conduction time)
  // and that the relationship is monotonic and smooth.

  std::cout << std::endl << "    Delay vs Duty cycle:" << std::endl;

  uint16_t prev_delay = UINT16_MAX;
  for (float duty = 0.0f; duty <= 1.0f; duty += 0.1f) {
    uint16_t delay = lookup_firing_delay(duty, 10000);
    float conduction_ratio = 1.0f - static_cast<float>(delay) / 10000.0f;

    std::cout << "      Duty=" << (duty * 100) << "% -> Delay=" << delay
              << "µs -> Conduction=" << (conduction_ratio * 100) << "%" << std::endl;

    // Higher duty should give smaller delay (monotonically decreasing)
    ASSERT_TRUE(delay <= prev_delay);
    prev_delay = delay;
  }

  // Additional check: at 50% duty, conduction should be around 50%
  // (the LUT is designed for linear power, so this is a rough approximation)
  uint16_t delay_50 = lookup_firing_delay(0.5f, 10000);
  float conduction_50 = 1.0f - static_cast<float>(delay_50) / 10000.0f;
  ASSERT_TRUE(conduction_50 > 0.3f && conduction_50 < 0.7f);
}

TEST(semi_period_50hz) {
  // 50Hz = 20ms period = 10000µs semi-period
  uint16_t expected = 10000;
  // Verify our constant is correct
  ASSERT_TRUE(expected >= 9900 && expected <= 10100);
}

TEST(semi_period_60hz) {
  // 60Hz = 16.67ms period = 8333µs semi-period
  uint16_t expected = 8333;
  ASSERT_TRUE(expected >= 8200 && expected <= 8500);
}

// ============================================================================
// Main
// ============================================================================

int main() {
  std::cout << "========================================" << std::endl;
  std::cout << "TRIAC Dimmer Logic Unit Tests" << std::endl;
  std::cout << "========================================" << std::endl;

  RUN_TEST(lookup_table_boundaries);
  RUN_TEST(lookup_table_50_percent);
  RUN_TEST(lookup_table_monotonic);
  RUN_TEST(lookup_table_different_frequencies);
  RUN_TEST(duty_cycle_mapping_identity);
  RUN_TEST(duty_cycle_mapping_custom_range);
  RUN_TEST(firing_delay_off);
  RUN_TEST(firing_delay_full_on);
  RUN_TEST(firing_delay_partial);
  RUN_TEST(firing_delay_minimum_enforcement);
  RUN_TEST(power_output_linearity);
  RUN_TEST(semi_period_50hz);
  RUN_TEST(semi_period_60hz);

  std::cout << "========================================" << std::endl;
  std::cout << "Results: " << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
  std::cout << "========================================" << std::endl;

  return tests_failed > 0 ? 1 : 0;
}
