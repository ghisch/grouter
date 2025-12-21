// SPDX-License-Identifier: MIT
// Zero-Cross Detection Component for ESPHome
// Inspired by MycilaPulseAnalyzer
#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"

#include <driver/gpio.h>
#include <functional>

namespace esphome {
namespace zero_cross {

// Callback type: receives delay until actual zero (in microseconds)
using ZeroCrossCallback = std::function<void(int16_t delay_until_zero)>;

// Maximum number of callbacks (typically just 1 triac dimmer)
static constexpr size_t MAX_CALLBACKS = 4;

class ZeroCrossComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  void set_pin(InternalGPIOPin *pin) { this->pin_ = pin; }

  /// Register a callback to be called on each zero-cross event
  /// Thread-safe: disables interrupts during registration
  void register_callback(ZeroCrossCallback callback);

  /// Get the measured semi-period in microseconds (half of AC cycle)
  /// For 50Hz: ~10000µs, for 60Hz: ~8333µs
  uint16_t get_semi_period() const { return this->semi_period_; }

  /// Get the measured AC frequency in Hz
  float get_frequency() const;

  /// Check if zero-cross detection is active and stable
  bool is_stable() const { return this->stable_; }

 protected:
  InternalGPIOPin *pin_{nullptr};
  gpio_num_t pin_num_{GPIO_NUM_NC};

  // Timing measurements
  volatile uint32_t last_zc_time_{0};
  volatile uint16_t semi_period_{0};
  volatile bool stable_{false};

  // For stability detection
  uint32_t stable_count_{0};
  static constexpr uint32_t STABLE_THRESHOLD = 10;  // Need 10 consistent readings

  // Registered callbacks - fixed size array for ISR safety
  ZeroCrossCallback callbacks_[MAX_CALLBACKS];
  volatile size_t callback_count_{0};

  // Spinlock for thread safety
  static portMUX_TYPE spinlock_;

  // Estimated delay from edge detection to actual zero crossing
  // Depends on ZCD circuit - typical values:
  // - RobotDyn ZCD: ~200µs
  // - Daniel S ZCD: ~550µs
  int16_t delay_until_zero_{200};  // Default for common ZCD circuits

  /// Static ISR handler (IRAM_ATTR in implementation)
  static void gpio_isr(void *arg);

  /// Call registered callbacks (IRAM_ATTR in implementation)
  void call_callbacks(int16_t delay);
};

}  // namespace zero_cross
}  // namespace esphome
