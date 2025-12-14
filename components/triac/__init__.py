# SPDX-License-Identifier: MIT
# High-Performance TRIAC Dimmer Component for ESPHome
# Based on MycilaDimmer optimizations

import esphome.codegen as cg

CODEOWNERS = ["@ghisch"]
DEPENDENCIES = ["zero_cross"]

# Only ESP32 is supported
triac_ns = cg.esphome_ns.namespace("triac")
