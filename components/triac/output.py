# SPDX-License-Identifier: MIT
# High-Performance TRIAC Dimmer Output for ESPHome
# Based on MycilaDimmer optimizations

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import output
from esphome.const import CONF_ID, CONF_MAX_POWER, CONF_MIN_POWER

CODEOWNERS = ["@ghisch"]
DEPENDENCIES = ["zero_cross"]

# Import zero_cross namespace
zero_cross_ns = cg.esphome_ns.namespace("zero_cross")
ZeroCrossComponent = zero_cross_ns.class_("ZeroCrossComponent", cg.Component)

triac_ns = cg.esphome_ns.namespace("triac")
TriacDimmer = triac_ns.class_("TriacDimmer", output.FloatOutput, cg.Component)

# Configuration constants
CONF_GATE_PIN = "gate_pin"
CONF_ZERO_CROSS_ID = "zero_cross_id"
CONF_POWER_LIMIT = "power_limit"
CONF_POWER_LUT = "power_lut"
CONF_SEMI_PERIOD = "semi_period"

CONFIG_SCHEMA = cv.All(
    output.FLOAT_OUTPUT_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(TriacDimmer),
            cv.Required(CONF_GATE_PIN): pins.internal_gpio_output_pin_schema,
            cv.Required(CONF_ZERO_CROSS_ID): cv.use_id(ZeroCrossComponent),
            # Duty cycle remapping (like Shelly Dimmer calibration)
            cv.Optional(CONF_MIN_POWER, default=0.0): cv.percentage,
            cv.Optional(CONF_MAX_POWER, default=1.0): cv.percentage,
            # Safety power limit (clamps maximum output)
            cv.Optional(CONF_POWER_LIMIT, default=1.0): cv.percentage,
            # Power LUT for linear power output (non-linear phase angle)
            cv.Optional(CONF_POWER_LUT, default=True): cv.boolean,
            # Manual semi-period override (0 = auto-detect from zero_cross)
            cv.Optional(CONF_SEMI_PERIOD, default=0): cv.int_range(min=0, max=20000),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_on_esp32,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await output.register_output(var, config)

    # Gate pin
    pin = await cg.gpio_pin_expression(config[CONF_GATE_PIN])
    cg.add(var.set_gate_pin(pin))

    # Zero-cross reference
    zc = await cg.get_variable(config[CONF_ZERO_CROSS_ID])
    cg.add(var.set_zero_cross(zc))

    # Duty cycle remapping
    cg.add(var.set_min_power(config[CONF_MIN_POWER]))
    cg.add(var.set_max_power(config[CONF_MAX_POWER]))

    # Power limit
    cg.add(var.set_power_limit(config[CONF_POWER_LIMIT]))

    # Power LUT
    cg.add(var.set_power_lut_enabled(config[CONF_POWER_LUT]))

    # Semi-period override
    if config[CONF_SEMI_PERIOD] > 0:
        cg.add(var.set_semi_period_override(config[CONF_SEMI_PERIOD]))

    # Add required build flags for IRAM safety (also added by zero_cross, but just in case)
    cg.add_build_flag("-DCONFIG_ARDUINO_ISR_IRAM=1")
    cg.add_build_flag("-DCONFIG_GPTIMER_ISR_HANDLER_IN_IRAM=1")
    cg.add_build_flag("-DCONFIG_GPTIMER_CTRL_FUNC_IN_IRAM=1")
    cg.add_build_flag("-DCONFIG_GPTIMER_ISR_IRAM_SAFE=1")
    cg.add_build_flag("-DCONFIG_GPIO_CTRL_FUNC_IN_IRAM=1")
