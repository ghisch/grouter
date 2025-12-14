# High-Performance TRIAC Dimmer Components for ESPHome

A set of ESPHome external components for high-performance TRIAC/SSR dimming, based on the optimizations from [MycilaDimmer](https://github.com/mathieucarbou/MycilaDimmer) by Mathieu Carbou.

## Features

### Performance Optimizations

| # | Optimization | Description |
|---|--------------|-------------|
| 1 | **Event-driven timer** | Uses `gptimer` alarms instead of fixed 50µs polling |
| 2 | **IRAM-safe functions** | Inlined gptimer functions work during flash operations (OTA) |
| 3 | **Direct GPIO access** | Uses `gpio_ll_set_level()` for fastest switching |
| 4 | **Pre-computed LUT** | 200-entry firing delay lookup table with interpolation |
| 5 | **Smart scheduling** | Timer only wakes up when triac needs to fire |
| 6 | **Spinlock protection** | `portENTER_CRITICAL_SAFE` for ISR-safe data access |
| 7 | **1µs precision** | 1MHz timer resolution (vs 50µs in ESPHome ac_dimmer) |
| 8 | **Minimum gate delay** | Enforces ~90µs minimum for reliable triac triggering |

### Configuration Options

- **Duty cycle remapping** (`min_power` / `max_power`): Calibrate your hardware range
- **Power limit** (`power_limit`): Safety cap on maximum output
- **Power LUT** (`power_lut`): Non-linear phase angle for linear power output
- **Semi-period** (`semi_period`): Auto-detect or manual override

## Installation

Add this to your ESPHome YAML configuration:

```yaml
external_components:
  - source:
      type: local
      path: components
```

Or from a Git repository:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/ghisch/grouter
      ref: main
    components: [zero_cross, triac]
```

## Components

### `zero_cross` - Zero-Cross Detection

Detects AC zero-crossing events and provides timing information for the triac component.

```yaml
zero_cross:
  id: zcd
  pin: GPIO4
```

### `triac` - TRIAC Dimmer Output

High-performance phase-angle controlled output.

```yaml
output:
  - platform: triac
    id: triac_dimmer
    gate_pin: GPIO26
    zero_cross_id: zcd
    min_power: 0%        # Duty cycle remapping (default: 0%)
    max_power: 100%      # Duty cycle remapping (default: 100%)
    power_limit: 100%    # Safety limit (default: 100%)
    power_lut: true      # Use power lookup table (default: true)
    semi_period: 0       # 0 = auto-detect, or manual µs (default: 0)
```

## Example Configuration

```yaml
esphome:
  name: solar-router
  platform: ESP32
  board: esp32dev

external_components:
  - source:
      type: local
      path: components

# Zero-cross detection
zero_cross:
  id: zcd
  pin: GPIO4

# TRIAC dimmer output
output:
  - platform: triac
    id: triac_dimmer
    gate_pin: GPIO26
    zero_cross_id: zcd
    power_lut: true
    power_limit: 80%  # Safety limit

# Example: Light control
light:
  - platform: monochromatic
    name: "Dimmer Light"
    output: triac_dimmer
    gamma_correct: 1.0  # Power LUT already handles this

# Example: Climate/heater control
number:
  - platform: template
    name: "Heater Power"
    min_value: 0
    max_value: 100
    step: 1
    unit_of_measurement: "%"
    set_action:
      - output.set_level:
          id: triac_dimmer
          level: !lambda "return x / 100.0;"
```

## Build Flags

The following build flags are **automatically added** by the components:

```ini
-DCONFIG_ARDUINO_ISR_IRAM=1
-DCONFIG_GPTIMER_ISR_HANDLER_IN_IRAM=1
-DCONFIG_GPTIMER_CTRL_FUNC_IN_IRAM=1
-DCONFIG_GPTIMER_ISR_IRAM_SAFE=1
-DCONFIG_GPIO_CTRL_FUNC_IN_IRAM=1
```

These flags ensure IRAM safety - the dimmer will continue working correctly during flash operations (OTA updates, file system writes, etc.).

## Hardware Requirements

- **ESP32** (any variant: ESP32, ESP32-S2, ESP32-S3, ESP32-C3, ESP32-C6)
- **Zero-Cross Detection circuit** (e.g., RobotDyn ZCD, or custom optocoupler circuit)
- **TRIAC** or **Random SSR** with gate control

### Recommended Zero-Cross Detectors

- [Zero-Cross Detector by Daniel S](https://www.pcbway.com/project/shareproject/Zero_Cross_Detector_a707a878.html) (highly recommended)
- RobotDyn ZCD module
- JSY-MK-194G (has built-in ZCD)

## Comparison with ESPHome ac_dimmer

| Aspect | ESPHome ac_dimmer | This component |
|--------|-------------------|----------------|
| Timer ISR frequency | Fixed 20,000/sec | ~100-200/sec (event-driven) |
| Timing precision | 50µs | 1µs |
| IRAM safe | ❌ No | ✅ Yes |
| GPIO access | HAL abstraction | Direct register |
| Power curve | Runtime `acos()` | Pre-computed LUT |
| CPU overhead | High (constant polling) | Low (on-demand) |

## Project Structure

```
components/
├── README.md              # This file
├── TESTING.md             # Testing guide
├── .clang-format          # C++ formatting rules
├── .yamllint              # YAML linting rules
├── ruff.toml              # Python linting/formatting rules
├── zero_cross/            # Zero-cross detection component
│   ├── __init__.py
│   ├── zero_cross.cpp
│   └── zero_cross.h
├── triac/                 # TRIAC dimmer output component
│   ├── __init__.py
│   ├── output.py
│   ├── triac_dimmer.cpp
│   └── triac_dimmer.h
├── tests/                 # Test configurations
│   ├── test_compile.yaml
│   ├── test_hardware_safe.yaml
│   └── test_triac_logic.cpp
└── examples/              # Example configurations
    └── example_triac.yaml
```

## Development

### Pre-commit Hooks

Install pre-commit hooks to ensure code quality:

```bash
# From the repository root (grouter/)
pip install pre-commit
pre-commit install
```

Hooks will run automatically on `git commit`. To run manually:

```bash
pre-commit run --all-files
```

## Testing

See [TESTING.md](TESTING.md) for comprehensive testing guide including:
- Unit tests (run on your computer)
- Hardware tests with safe loads
- Oscilloscope verification procedures
- Safety checklists

## Credits

- Performance optimizations based on [MycilaDimmer](https://github.com/mathieucarbou/MycilaDimmer) by Mathieu Carbou
- Used in [YaSolR Solar Router](https://yasolr.carbou.me)

## License

MIT License
