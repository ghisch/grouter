# SPDX-License-Identifier: MIT
# Zero-Cross Detection Component for ESPHome
# Inspired by MycilaPulseAnalyzer

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.const import CONF_ID, CONF_PIN

CODEOWNERS = ["@ghisch"]
DEPENDENCIES = []

# Only ESP32 is supported
MULTI_CONF = True

zero_cross_ns = cg.esphome_ns.namespace("zero_cross")
ZeroCrossComponent = zero_cross_ns.class_("ZeroCrossComponent", cg.Component)

CONF_ZERO_CROSS_ID = "zero_cross_id"

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ZeroCrossComponent),
            cv.Required(CONF_PIN): pins.internal_gpio_input_pin_schema,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_on_esp32,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    pin = await cg.gpio_pin_expression(config[CONF_PIN])
    cg.add(var.set_pin(pin))

    # Add required build flags for IRAM safety
    cg.add_build_flag("-DCONFIG_ARDUINO_ISR_IRAM=1")
    cg.add_build_flag("-DCONFIG_GPTIMER_ISR_HANDLER_IN_IRAM=1")
    cg.add_build_flag("-DCONFIG_GPTIMER_CTRL_FUNC_IN_IRAM=1")
    cg.add_build_flag("-DCONFIG_GPTIMER_ISR_IRAM_SAFE=1")
    cg.add_build_flag("-DCONFIG_GPIO_CTRL_FUNC_IN_IRAM=1")
