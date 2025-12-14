// SPDX-License-Identifier: MIT
// High-Performance TRIAC Dimmer for ESPHome
// Based on MycilaDimmer optimizations by Mathieu Carbou
#pragma once

#include "../zero_cross/zero_cross.h"
#include "esphome/components/output/float_output.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"

#include <driver/gpio.h>
#include <driver/gptimer.h>

namespace esphome {
namespace triac {

/// Pre-computed firing delay lookup table size
static constexpr uint32_t FIRING_DELAYS_LEN = 200U;

/// Timer resolution (12-bit for firing delays)
static constexpr uint32_t DIMMER_RESOLUTION = 12;

/// Maximum firing delay value
static constexpr uint32_t FIRING_DELAY_MAX = (1 << DIMMER_RESOLUTION) - 1;

/// Scale factor for LUT interpolation
static constexpr uint32_t FIRING_DELAYS_SCALE = (FIRING_DELAYS_LEN - 1U) * (1UL << (16 - DIMMER_RESOLUTION));

/// Minimum delay to reach the voltage required for gate triggering (~90µs)
/// This ensures reliable triac triggering even at high power levels
static constexpr uint16_t PHASE_DELAY_MIN_US = 90;

/**
 * @brief High-performance TRIAC dimmer output for ESPHome
 *
 * Performance optimizations implemented:
 * 1. Event-driven timer with gptimer alarms (not fixed polling)
 * 2. Direct GPIO access via gpio_ll_set_level()
 * 3. Pre-computed 200-entry firing delay LUT with interpolation
 * 4. Smart scheduling - only fires when needed
 * 5. Spinlock protection for ISR-safe data access
 * 6. 1µs precision (1MHz timer resolution)
 * 7. Minimum gate delay enforcement (~90µs)
 */
class TriacDimmer : public output::FloatOutput, public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE - 1.0f; }

  // Configuration setters
  void set_gate_pin(InternalGPIOPin *pin) { this->gate_pin_ = pin; }
  void set_zero_cross(zero_cross::ZeroCrossComponent *zc) { this->zero_cross_ = zc; }
  void set_min_power(float min_power) { this->min_power_ = min_power; }
  void set_max_power(float max_power) { this->max_power_ = max_power; }
  void set_power_limit(float limit) { this->power_limit_ = limit; }
  void set_power_lut_enabled(bool enabled) { this->power_lut_enabled_ = enabled; }
  void set_semi_period_override(uint16_t semi_period) { this->semi_period_override_ = semi_period; }

  // State getters
  bool is_on() const { return this->duty_cycle_ > 0 && this->enabled_; }
  float get_duty_cycle() const { return this->duty_cycle_; }
  float get_duty_cycle_mapped() const {
    return this->min_power_ + this->duty_cycle_ * (this->max_power_ - this->min_power_);
  }
  float get_duty_cycle_fire() const { return this->duty_cycle_fire_; }
  uint16_t get_firing_delay() const { return this->firing_delay_; }
  uint16_t get_semi_period() const;

 protected:
  void write_state(float state) override;

  /// Apply the current duty cycle to hardware
  void apply_duty_cycle();

  /// Lookup firing delay from pre-computed table with interpolation
  static uint16_t lookup_firing_delay(float duty_cycle, uint16_t semi_period);

  /// Register this dimmer for zero-cross callbacks
  void register_with_zero_cross();

  /// Zero-cross callback handler
  void on_zero_cross(int16_t delay_until_zero);

  /// Timer ISR for firing the triac
  static bool IRAM_ATTR fire_timer_isr(gptimer_handle_t timer, const gptimer_alarm_event_data_t *event, void *arg);

  // Configuration
  InternalGPIOPin *gate_pin_{nullptr};
  gpio_num_t gate_pin_num_{GPIO_NUM_NC};
  zero_cross::ZeroCrossComponent *zero_cross_{nullptr};

  float min_power_{0.0f};
  float max_power_{1.0f};
  float power_limit_{1.0f};
  bool power_lut_enabled_{true};
  uint16_t semi_period_override_{0};

  // State
  bool enabled_{false};
  float duty_cycle_{0.0f};
  float duty_cycle_fire_{0.0f};
  volatile uint16_t firing_delay_{UINT16_MAX};  // UINT16_MAX = off

  // Timer
  gptimer_handle_t fire_timer_{nullptr};
  static portMUX_TYPE spinlock_;

  // Pre-computed firing delay lookup table
  static const uint16_t FIRING_DELAYS[FIRING_DELAYS_LEN];
};

}  // namespace triac
}  // namespace esphome
