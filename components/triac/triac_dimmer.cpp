// SPDX-License-Identifier: MIT
// High-Performance TRIAC Dimmer for ESPHome
// Based on MycilaDimmer optimizations by Mathieu Carbou
//
// Performance optimizations implemented:
// 1. Event-driven timer with gptimer alarms (not fixed 50µs polling)
// 2. Direct GPIO access via gpio_ll_set_level()
// 3. Pre-computed 200-entry firing delay LUT with interpolation
// 4. Smart scheduling - only fires when needed
// 5. Spinlock protection for ISR-safe data access
// 6. 1µs precision (1MHz timer resolution)
// 7. Minimum gate delay enforcement (~90µs)

#include "triac_dimmer.h"
#include "esphome/core/log.h"

#include <driver/gpio.h>
#include <driver/gptimer.h>
#include <hal/gpio_ll.h>
#include <soc/gpio_struct.h>

namespace esphome {
namespace triac {

static const char *const TAG = "triac";

// Static member initialization
portMUX_TYPE TriacDimmer::spinlock_ = portMUX_INITIALIZER_UNLOCKED;

// Pre-computed firing delay lookup table (from MycilaDimmer)
// This table maps linear duty cycle to phase angle for linear power output
// Values are in 16-bit fixed point, scaled by semi-period during lookup
// clang-format off
const uint16_t TriacDimmer::FIRING_DELAYS[FIRING_DELAYS_LEN] = {
  0xffff, 0xe877, 0xe240, 0xddd9, 0xda51, 0xd74f, 0xd4aa, 0xd248, 0xd01a, 0xce16,
  0xcc34, 0xca6e, 0xc8c0, 0xc728, 0xc5a1, 0xc42b, 0xc2c3, 0xc168, 0xc019, 0xbed3,
  0xbd98, 0xbc65, 0xbb3b, 0xba17, 0xb8fb, 0xb7e5, 0xb6d5, 0xb5ca, 0xb4c5, 0xb3c4,
  0xb2c8, 0xb1d1, 0xb0dd, 0xafed, 0xaf01, 0xae18, 0xad33, 0xac51, 0xab71, 0xaa95,
  0xa9bb, 0xa8e3, 0xa80e, 0xa73b, 0xa66b, 0xa59c, 0xa4d0, 0xa406, 0xa33d, 0xa276,
  0xa1b1, 0xa0ed, 0xa02b, 0x9f6b, 0x9eac, 0x9dee, 0x9d32, 0x9c76, 0x9bbc, 0x9b04,
  0x9a4c, 0x9996, 0x98e0, 0x982b, 0x9778, 0x96c5, 0x9613, 0x9563, 0x94b2, 0x9403,
  0x9354, 0x92a6, 0x91f9, 0x914c, 0x90a0, 0x8ff5, 0x8f4a, 0x8ea0, 0x8df6, 0x8d4d,
  0x8ca4, 0x8bfb, 0x8b53, 0x8aab, 0x8a04, 0x895d, 0x88b6, 0x8810, 0x876a, 0x86c4,
  0x861e, 0x8579, 0x84d3, 0x842e, 0x8389, 0x82e4, 0x823f, 0x819b, 0x80f6, 0x8051,
  0x7fad, 0x7f08, 0x7e63, 0x7dbf, 0x7d1a, 0x7c75, 0x7bd0, 0x7b2b, 0x7a85, 0x79e0,
  0x793a, 0x7894, 0x77ee, 0x7748, 0x76a1, 0x75fa, 0x7553, 0x74ab, 0x7403, 0x735a,
  0x72b1, 0x7208, 0x715e, 0x70b4, 0x7009, 0x6f5e, 0x6eb2, 0x6e05, 0x6d58, 0x6caa,
  0x6bfb, 0x6b4c, 0x6a9b, 0x69eb, 0x6939, 0x6886, 0x67d3, 0x671e, 0x6668, 0x65b2,
  0x64fa, 0x6442, 0x6388, 0x62cc, 0x6210, 0x6152, 0x6093, 0x5fd3, 0x5f11, 0x5e4d,
  0x5d88, 0x5cc1, 0x5bf8, 0x5b2e, 0x5a62, 0x5993, 0x58c3, 0x57f0, 0x571b, 0x5643,
  0x5569, 0x548d, 0x53ad, 0x52cb, 0x51e6, 0x50fd, 0x5011, 0x4f21, 0x4e2d, 0x4d36,
  0x4c3a, 0x4b39, 0x4a34, 0x4929, 0x4819, 0x4703, 0x45e7, 0x44c3, 0x4399, 0x4266,
  0x412b, 0x3fe5, 0x3e96, 0x3d3b, 0x3bd3, 0x3a5d, 0x38d6, 0x373e, 0x3590, 0x33ca,
  0x31e8, 0x2fe4, 0x2db6, 0x2b54, 0x28af, 0x25ad, 0x2225, 0x1dbe, 0x1787, 0x0000
};
// clang-format on

void TriacDimmer::setup() {
  ESP_LOGCONFIG(TAG, "Setting up TRIAC Dimmer...");

  // Setup gate pin using ESP-IDF API for better control
  this->gate_pin_->setup();
  this->gate_pin_num_ = static_cast<gpio_num_t>(this->gate_pin_->get_pin());

  gpio_config_t io_conf = {};
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_OUTPUT;
  io_conf.pin_bit_mask = (1ULL << this->gate_pin_num_);
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  gpio_config(&io_conf);

  // Ensure gate starts LOW
  gpio_set_level(this->gate_pin_num_, 0);

  // Initialize the fire timer using ESP-IDF 5.x gptimer API
  ESP_LOGD(TAG, "Initializing fire timer...");

  gptimer_config_t timer_config = {};
  timer_config.clk_src = GPTIMER_CLK_SRC_DEFAULT;
  timer_config.direction = GPTIMER_COUNT_UP;
  timer_config.resolution_hz = 1000000;  // 1MHz resolution = 1µs precision
  timer_config.flags.intr_shared = true;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
  timer_config.flags.backup_before_sleep = false;
#endif

  esp_err_t err = gptimer_new_timer(&timer_config, &this->fire_timer_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create timer: %s", esp_err_to_name(err));
    return;
  }

  gptimer_event_callbacks_t callbacks_config = {};
  callbacks_config.on_alarm = &TriacDimmer::fire_timer_isr;
  err = gptimer_register_event_callbacks(this->fire_timer_, &callbacks_config, this);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register timer callbacks: %s", esp_err_to_name(err));
    return;
  }

  err = gptimer_enable(this->fire_timer_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to enable timer: %s", esp_err_to_name(err));
    return;
  }

  err = gptimer_start(this->fire_timer_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start timer: %s", esp_err_to_name(err));
    return;
  }

  // Register with zero-cross component
  this->register_with_zero_cross();

  this->enabled_ = true;
  ESP_LOGCONFIG(TAG, "TRIAC Dimmer initialized on GPIO%d", this->gate_pin_num_);
}

void TriacDimmer::loop() {
  // Nothing to do in loop - all handled in ISRs
}

void TriacDimmer::dump_config() {
  ESP_LOGCONFIG(TAG, "TRIAC Dimmer:");
  LOG_PIN("  Gate Pin: ", this->gate_pin_);
  ESP_LOGCONFIG(TAG, "  Min Power: %.1f%%", this->min_power_ * 100.0f);
  ESP_LOGCONFIG(TAG, "  Max Power: %.1f%%", this->max_power_ * 100.0f);
  ESP_LOGCONFIG(TAG, "  Power Limit: %.1f%%", this->power_limit_ * 100.0f);
  ESP_LOGCONFIG(TAG, "  Power LUT: %s", this->power_lut_enabled_ ? "enabled" : "disabled");
  if (this->semi_period_override_ > 0) {
    ESP_LOGCONFIG(TAG, "  Semi-period: %uµs (manual)", this->semi_period_override_);
  } else {
    ESP_LOGCONFIG(TAG, "  Semi-period: %uµs (auto)", this->get_semi_period());
  }
  LOG_FLOAT_OUTPUT(this);
}

void TriacDimmer::register_with_zero_cross() {
  if (this->zero_cross_ == nullptr) {
    ESP_LOGE(TAG, "Zero-cross component not set!");
    return;
  }

  this->zero_cross_->register_callback([this](int16_t delay_until_zero) { this->on_zero_cross(delay_until_zero); });

  ESP_LOGD(TAG, "Registered with zero-cross component");
}

uint16_t TriacDimmer::get_semi_period() const {
  if (this->semi_period_override_ > 0) {
    return this->semi_period_override_;
  }
  if (this->zero_cross_ != nullptr) {
    return this->zero_cross_->get_semi_period();
  }
  return 10000;  // Default to 50Hz
}

void TriacDimmer::write_state(float state) {
  // Clamp and apply power limit
  float clamped = std::max(0.0f, std::min(state, this->power_limit_));
  this->duty_cycle_ = clamped;

  this->apply_duty_cycle();
}

void TriacDimmer::apply_duty_cycle() {
  // Apply min/max remapping
  float mapped = this->get_duty_cycle_mapped();

  uint16_t semi_period = this->get_semi_period();

  // Calculate firing duty cycle (with optional power LUT)
  if (this->power_lut_enabled_ && semi_period > 0) {
    if (mapped <= 0.0f) {
      this->duty_cycle_fire_ = 0.0f;
    } else if (mapped >= 1.0f) {
      this->duty_cycle_fire_ = 1.0f;
    } else {
      // Use LUT to convert linear power to phase angle
      uint16_t delay = lookup_firing_delay(mapped, semi_period);
      this->duty_cycle_fire_ = 1.0f - static_cast<float>(delay) / static_cast<float>(semi_period);
    }
  } else {
    // Linear mode (no LUT)
    this->duty_cycle_fire_ = mapped;
  }

  // Calculate firing delay
  portENTER_CRITICAL(&spinlock_);
  if (this->duty_cycle_fire_ <= 0.0f) {
    this->firing_delay_ = UINT16_MAX;  // Off
  } else if (this->duty_cycle_fire_ >= 1.0f) {
    this->firing_delay_ = 0;  // Full on
  } else {
    this->firing_delay_ = static_cast<uint16_t>((1.0f - this->duty_cycle_fire_) * static_cast<float>(semi_period));
  }
  portEXIT_CRITICAL(&spinlock_);

  ESP_LOGV(TAG, "Duty: %.2f%%, Fire: %.2f%%, Delay: %uµs", this->duty_cycle_ * 100.0f, this->duty_cycle_fire_ * 100.0f,
           this->firing_delay_);
}

uint16_t TriacDimmer::lookup_firing_delay(float duty_cycle, uint16_t semi_period) {
  // Fast lookup with linear interpolation (from MycilaDimmer)
  uint32_t duty = static_cast<uint32_t>(duty_cycle * FIRING_DELAY_MAX);
  uint32_t slot = duty * FIRING_DELAYS_SCALE + (FIRING_DELAYS_SCALE >> 1);
  uint32_t index = slot >> 16;

  // Bounds check
  if (index >= FIRING_DELAYS_LEN - 1) {
    index = FIRING_DELAYS_LEN - 2;
  }

  uint32_t a = FIRING_DELAYS[index];
  uint32_t b = FIRING_DELAYS[index + 1];

  // Linear interpolation between table entries
  uint32_t delay = a - (((a - b) * (slot & 0xffff)) >> 16);

  // Scale to actual semi-period
  return static_cast<uint16_t>((delay * semi_period) >> 16);
}

void IRAM_ATTR TriacDimmer::on_zero_cross(int16_t delay_until_zero) {
  if (this->fire_timer_ == nullptr || !this->enabled_) {
    return;
  }

  portENTER_CRITICAL_ISR(&spinlock_);
  uint16_t firing_delay = this->firing_delay_;
  portEXIT_CRITICAL_ISR(&spinlock_);

  if (firing_delay == 0) {
    // Full power - turn on immediately and keep on using direct GPIO access
    // Disable any pending alarm to prevent spurious ISR calls
    gptimer_set_alarm_action(this->fire_timer_, nullptr);
    gpio_ll_set_level(&GPIO, this->gate_pin_num_, 1);
    return;
  }

  if (firing_delay == UINT16_MAX) {
    // Off - turn off and stay off using direct GPIO access
    // Disable any pending alarm to prevent spurious ISR calls
    gptimer_set_alarm_action(this->fire_timer_, nullptr);
    gpio_ll_set_level(&GPIO, this->gate_pin_num_, 0);
    return;
  }

  // Partial power - turn off now, set alarm to turn on later
  gpio_ll_set_level(&GPIO, this->gate_pin_num_, 0);

  // Enforce minimum delay for reliable triac triggering
  uint16_t actual_delay = firing_delay;
  if (actual_delay < PHASE_DELAY_MIN_US) {
    actual_delay = PHASE_DELAY_MIN_US;
  }

  // Account for delay until actual zero crossing
  if (delay_until_zero > 0 && actual_delay > static_cast<uint16_t>(delay_until_zero)) {
    actual_delay -= delay_until_zero;
  }

  // Reset timer counter to start fresh from this zero-cross
  gptimer_set_raw_count(this->fire_timer_, 0);

  // Set alarm to fire at the calculated delay
  // Note: alarm_config must be in stack (internal RAM) for ISR safety
  gptimer_alarm_config_t alarm_config = {};
  alarm_config.alarm_count = actual_delay;
  alarm_config.reload_count = 0;
  alarm_config.flags.auto_reload_on_alarm = false;

  gptimer_set_alarm_action(this->fire_timer_, &alarm_config);
}

bool IRAM_ATTR TriacDimmer::fire_timer_isr(gptimer_handle_t timer, const gptimer_alarm_event_data_t *event, void *arg) {
  auto *self = static_cast<TriacDimmer *>(arg);
  if (self == nullptr) {
    return false;
  }

  // Fire the triac using direct GPIO access for maximum speed
  gpio_ll_set_level(&GPIO, self->gate_pin_num_, 1);

  return false;  // Don't wake up any task
}

}  // namespace triac
}  // namespace esphome
