// SPDX-License-Identifier: MIT
// Zero-Cross Detection Component for ESPHome
// Using modern ESP-IDF 5.x GPIO interrupt API

#include "zero_cross.h"
#include "esphome/core/log.h"

#include <driver/gpio.h>
#include <esp_intr_alloc.h>

namespace esphome {
namespace zero_cross {

static const char *const TAG = "zero_cross";

void ZeroCrossComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Zero-Cross Detection...");

  this->pin_->setup();
  this->pin_num_ = static_cast<gpio_num_t>(this->pin_->get_pin());

  // Configure GPIO for interrupt
  gpio_config_t io_conf = {};
  io_conf.intr_type = GPIO_INTR_NEGEDGE;  // Trigger on falling edge
  io_conf.mode = GPIO_MODE_INPUT;
  io_conf.pin_bit_mask = (1ULL << this->pin_num_);
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE;

  esp_err_t err = gpio_config(&io_conf);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to configure GPIO: %s", esp_err_to_name(err));
    return;
  }

  // Install GPIO ISR service with IRAM flag for reliability during flash operations
  err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LEVEL3);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    // ESP_ERR_INVALID_STATE means ISR service already installed, which is fine
    ESP_LOGE(TAG, "Failed to install ISR service: %s", esp_err_to_name(err));
    return;
  }

  // Add ISR handler for this pin
  err = gpio_isr_handler_add(this->pin_num_, &ZeroCrossComponent::gpio_isr, this);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to add ISR handler: %s", esp_err_to_name(err));
    return;
  }

  ESP_LOGCONFIG(TAG, "Zero-Cross Detection initialized on GPIO%d", this->pin_num_);
}

void ZeroCrossComponent::loop() {
  // Nothing to do in loop - all handled in ISR
}

void ZeroCrossComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Zero-Cross Detection:");
  LOG_PIN("  Pin: ", this->pin_);
  if (this->stable_) {
    ESP_LOGCONFIG(TAG, "  Semi-period: %uµs", this->semi_period_);
    ESP_LOGCONFIG(TAG, "  Frequency: %.1fHz", this->get_frequency());
  } else {
    ESP_LOGCONFIG(TAG, "  Status: Waiting for stable signal...");
  }
}

void ZeroCrossComponent::register_callback(ZeroCrossCallback callback) {
  this->callbacks_.push_back(std::move(callback));
}

float ZeroCrossComponent::get_frequency() const {
  if (this->semi_period_ == 0)
    return 0.0f;
  return 1000000.0f / (2.0f * this->semi_period_);
}

void IRAM_ATTR ZeroCrossComponent::gpio_isr(void *arg) {
  auto *self = static_cast<ZeroCrossComponent *>(arg);

  uint32_t now = micros();
  uint32_t elapsed = now - self->last_zc_time_;
  self->last_zc_time_ = now;

  // Filter out noise - valid semi-period should be between 5ms and 15ms
  // (supports 33Hz to 100Hz range, covering 50Hz and 60Hz)
  if (elapsed >= 5000 && elapsed <= 15000) {
    self->semi_period_ = static_cast<uint16_t>(elapsed);

    if (!self->stable_) {
      self->stable_count_++;
      if (self->stable_count_ >= STABLE_THRESHOLD) {
        self->stable_ = true;
      }
    }

    // Call registered callbacks with delay until actual zero crossing
    self->call_callbacks(self->delay_until_zero_);
  } else {
    // Invalid timing - might be noise or second edge in same pulse
    // Don't reset stability immediately, allow some tolerance
    if (self->stable_count_ > 0) {
      self->stable_count_--;
    }
  }
}

void IRAM_ATTR ZeroCrossComponent::call_callbacks(int16_t delay) {
  for (auto &callback : this->callbacks_) {
    callback(delay);
  }
}

}  // namespace zero_cross
}  // namespace esphome
