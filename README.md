# Grouter

![A cute, small tree-like character with glowing leaves, holding an orb of solar energy – in flat modern icon style.](/assets/logo.png)

**An ESPHome Solar Router (aka Solar Diverter)**

## About

Grouter is based on [Solar-Router-for-ESPHome](https://github.com/XavierBerger/Solar-Router-for-ESPHome). The goal is the same: control energy sent to a load based on exported solar energy.

This project uses a **hybrid relay + dimmer** approach for better efficiency and finer control.

## Bill of Materials

| Component | Cost |
|-----------|------|
| ESP32-S3 DevKit | ~5€ |
| 4-gang relay board | ~2.5€ |
| Robotdyn AC Dimmer | ~10€ |
| BTA40-800B Triac | ~2€ |
| 5V PSU | ~1.5€ |
| Wiring, junctions, enclosure | ~10€ |
| **Total** | **~30€** |

## Wiring

![Wiring Diagram](/assets/wiring.png)

## How It Works

### Designed For Any Triple Identical Load

This router is designed for loads with **3 identical heating elements**. While the example below uses a 3×800W = 2400W water heater, **it works with any power rating** as long as all three elements are identical:

- 3× 500W = 1500W ✅
- 3× 800W = 2400W ✅ *(author's setup)*
- 3× 1000W = 3000W ✅
- 3× 1500W = 4500W ✅

Simply adjust the `regulation.power_divisor` and `energy.default_load_power` variables to match your total load wattage.

> ⚠️ **Heatsink Warning**: Even though this project's hybrid approach reduces TRIAC heat by offloading base load to relays, **always properly size your heatsink**. The TRIAC still handles fine-tuning power and can get hot under sustained use. Overheating can cause component failure or fire hazards.

### Example: 3×800W Water Heater (2400W Total)

The router controls power using stepped relay switching combined with fine dimmer control:

| Router Level | Relay 1 | Relay 2 | Relay 3 | Dimmer | Power Range |
|--------------|---------|---------|---------|--------|-------------|
| 0-33% | OFF | OFF | OFF | 0-100% | 0-800W |
| 33-66% | ON | OFF | OFF | 0-100% | 800-1600W |
| 66-100% | ON | ON | OFF | 0-100% | 1600-2400W |
| 100% | ON | ON | ON | OFF | 2400W |

### Benefits vs Pure Dimmer

- ✅ **Less heat** - Relays handle base load, dimmer only fine-tunes
- ✅ **Less noise** - Reduced phase-cutting means cleaner AC waveform
- ✅ **Finer control** - Dimmer only varies 0-33% of total power
- ✅ **Zero dimmer heat** at 33%, 66%, 100% thresholds

### High-Performance TRIAC Component

This project includes a **custom optimized TRIAC component** based on [MycilaDimmer](https://github.com/mathieucarbou/MycilaDimmer), offering significant improvements over ESPHome's standard `ac_dimmer`:

- ⚡ **1µs precision** (vs 50µs in ac_dimmer)
- ⚡ **Event-driven** timer instead of constant polling
- ⚡ **IRAM-safe** for glitch-free OTA updates
- ⚡ **Direct GPIO access** for minimal latency
- ⚡ **Pre-computed power LUT** for linear power output

👉 See [components/README.md](components/README.md) for full documentation.

### Load Thermostat Detection

The router can detect when your water heater's internal thermostat has turned off (water reached target temperature). When the measured home consumption drops below the expected diverted power, the "Load Thermostat Off" sensor becomes `true`. This is useful for:
- Knowing when your water is fully heated
- Avoiding wasted regulation cycles
- Home Assistant automations

## Project Structure

```
router/
├── core/
│   ├── substitutions.yaml           # All configurable constants
│   ├── globals.yaml                 # Runtime state variables
│   └── regulation.yaml              # Main regulation algorithm
├── hardware/
│   ├── templates/                   # Reusable component templates
│   │   ├── relay_gpio.yaml          # GPIO output template
│   │   ├── relay_switch.yaml        # Relay switch template
│   │   └── relay_state.yaml         # Binary sensor template
│   ├── relay_controller.yaml        # 3-channel relay control
│   ├── triac_controller.yaml        # AC dimmer control
│   └── power_meter_ha.yaml          # Home Assistant power sensors
├── ui/
│   ├── templates/                   # Reusable UI templates
│   │   └── countdown_sensor.yaml    # Relay countdown template
│   ├── controls.yaml                # User-facing switches & numbers
│   └── sensors.yaml                 # Status sensors
├── features/
│   ├── common.yaml                  # Restart button, uptime
│   ├── forced_run.yaml              # Scheduled forced heating
│   ├── energy_counter.yaml          # Daily energy tracking
│   ├── network.yaml                 # WiFi diagnostics
│   └── debug.yaml                   # Debug sensors (optional)
└── engine_3_relays_1_dimmer.yaml    # All-in-one include
```

## Configuration

### Quick Start (All-in-One)

```yaml
esphome:
  name: grouter
  friendly_name: Solar Router

esp32:
  board: esp32-s3-devkitc-1

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

api:
  encryption:
    key: !secret api_key

ota:
  - platform: esphome
    password: !secret ota_password

packages:
  engine:
    url: https://github.com/ghisch/grouter/
    files:
      - path: router/engine_3_relays_1_dimmer.yaml
        vars:
          # Hardware pins
          pins.regulator_gate: "GPIO42"
          pins.regulator_zero_crossing: "GPIO41"
          pins.relay_1: "GPIO40"
          pins.relay_2: "GPIO39"
          pins.relay_3: "GPIO38"
          # Home Assistant sensors
          sensors.grid_power: "sensor.grid_power"
          sensors.consumption: "sensor.home_consumption"
          # Load configuration (adjust to your setup!)
          regulation.power_divisor: "2400"  # Total load wattage
    refresh: 1d

  # Optional features
  common:
    url: https://github.com/ghisch/grouter/
    file: router/features/common.yaml
    refresh: 1d

  energy:
    url: https://github.com/ghisch/grouter/
    files:
      - path: router/features/energy_counter.yaml
        vars:
          energy.default_load_power: "2400"  # Total load wattage
    refresh: 1d

  network:
    url: https://github.com/ghisch/grouter/
    file: router/features/network.yaml
    refresh: 1d

  # Optional: Debug sensors (for troubleshooting)
  # debug:
  #   url: https://github.com/ghisch/grouter/
  #   file: router/features/debug.yaml
  #   refresh: 1d
```

### Debug Mode

For troubleshooting, include the debug package to expose diagnostic sensors:

```yaml
packages:
  debug:
    url: https://github.com/ghisch/grouter/
    file: router/features/debug.yaml
    refresh: 1d
```

This adds the following diagnostic entities:

| Sensor | Description |
|--------|-------------|
| **Device Info** | ESP32 chip model and features |
| **Reset Reason** | Why the device last restarted |
| **Internal Temperature** | ESP32 CPU temperature |
| **Heap Free** | Available memory (bytes) |
| **Heap Max Block** | Largest contiguous free memory block |
| **Loop Time** | Main loop execution time (ms) |

⚠️ Debug mode sets logger level to `DEBUG`, which increases serial output and may slightly impact performance. **Remove in production.**

### Modular Configuration

For more control, include individual components:

```yaml
packages:
  # Core (required)
  substitutions:
    url: https://github.com/ghisch/grouter/
    file: router/core/substitutions.yaml
    refresh: 1d

  globals:
    url: https://github.com/ghisch/grouter/
    file: router/core/globals.yaml
    refresh: 1d

  regulation:
    url: https://github.com/ghisch/grouter/
    file: router/core/regulation.yaml
    refresh: 1d

  # Hardware (pick what you need)
  relays:
    url: https://github.com/ghisch/grouter/
    files:
      - path: router/hardware/relay_controller.yaml
        vars:
          pins.relay_1: "GPIO40"
          pins.relay_2: "GPIO39"
          pins.relay_3: "GPIO38"
    refresh: 1d

  triac:
    url: https://github.com/ghisch/grouter/
    files:
      - path: router/hardware/triac_controller.yaml
        vars:
          pins.regulator_gate: "GPIO42"
          pins.regulator_zero_crossing: "GPIO41"
    refresh: 1d

  power_meter:
    url: https://github.com/ghisch/grouter/
    files:
      - path: router/hardware/power_meter_ha.yaml
        vars:
          sensors.grid_power: "sensor.grid_power"
          sensors.consumption: "sensor.home_consumption"
    refresh: 1d

  # UI (required)
  controls:
    url: https://github.com/ghisch/grouter/
    file: router/ui/controls.yaml
    refresh: 1d

  sensors:
    url: https://github.com/ghisch/grouter/
    file: router/ui/sensors.yaml
    refresh: 1d
```

## Configurable Variables

All variables are defined in `core/substitutions.yaml` with sensible defaults. Override them via `vars` in your package includes.

### Hardware Pins (`pins.*`)

| Variable | Default | Description |
|----------|---------|-------------|
| `pins.regulator_gate` | `GPIO42` | Triac gate control pin |
| `pins.regulator_zero_crossing` | `GPIO41` | Zero-crossing detector input |
| `pins.relay_1` | `GPIO40` | First relay GPIO (active LOW) |
| `pins.relay_2` | `GPIO39` | Second relay GPIO (active LOW) |
| `pins.relay_3` | `GPIO38` | Third relay GPIO (active LOW) |

### Home Assistant Sensors (`sensors.*`)

| Variable | Default | Description |
|----------|---------|-------------|
| `sensors.grid_power` | `sensor.grid_power` | Grid power exchange entity (+ = import, - = export) |
| `sensors.consumption` | `sensor.home_consumption` | Total home consumption entity |
| `sensors.force_run` | `binary_sensor.force_run` | Forced run enable/disable entity |

### Relay Thresholds (`thresholds.*`)

| Variable | Default | Description |
|----------|---------|-------------|
| `thresholds.relay_1` | `33.33333333` | Router level % to activate relay 1 |
| `thresholds.relay_2` | `66.66666666` | Router level % to activate relay 2 |
| `thresholds.relay_3` | `100.0` | Router level % to activate relay 3 |

### Regulation Parameters (`regulation.*`)

| Variable | Default | Description |
|----------|---------|-------------|
| `regulation.power_divisor` | `3000` | Total load wattage (for delta calculation) |
| `regulation.interval` | `10` | Fallback regulation interval in seconds |

### Relay Protection (`relay.*`)

| Variable | Default | Description |
|----------|---------|-------------|
| `relay.anti_cycle_duration` | `30` | Regulation cycles before relay can re-enable* |

> **\*Note on Regulation Cycles:** The countdown is NOT in seconds! One cycle = one regulation script execution. The script runs on **every power meter value change** (reactive) OR **every `regulation.interval` seconds** (fallback). With frequent power updates, 30 cycles might complete in 30-300 seconds depending on power meter activity.

### Forced Run Schedule (`forced_run.*`)

| Variable | Default | Description |
|----------|---------|-------------|
| `forced_run.on_cron` | `0 0 2 * * *` | Cron to start forced run (default: 2 AM) |
| `forced_run.off_cron` | `0 0 6 * * *` | Cron to stop forced run (default: 6 AM) |

### Energy Counter (`energy.*`)

| Variable | Default | Description |
|----------|---------|-------------|
| `energy.default_load_power` | `2400` | Total load wattage for energy calculation |

### Power Meter (`power_meter.*`)

| Variable | Default | Description |
|----------|---------|-------------|
| `power_meter.activated_at_start` | `false` | Start with power meter activated |

## Adapting to Your Load

### For Different Power Ratings

If your water heater has different resistor values, update these variables:

| Your Setup | `regulation.power_divisor` | `energy.default_load_power` |
|------------|---------------------------|----------------------------|
| 3× 500W = 1500W | `1500` | `1500` |
| 3× 800W = 2400W | `2400` | `2400` |
| 3× 1000W = 3000W | `3000` | `3000` |
| 3× 1500W = 4500W | `4500` | `4500` |

## Safety Notes

⚠️ **Working with mains voltage is dangerous!**

- Always leave relay NC (Normally Closed) ports disconnected
- Ground your heatsink properly
- Use appropriate wire gauges for current load
- Install proper circuit breakers

💡 **PSU Tip**: Cheap 5V PSUs may crash during relay switching due to voltage drops. The ESP32 will reboot and retry - usually works on the second attempt.

## License

See [LICENSE](LICENSE) for details.

## Credits

Based on [Solar-Router-for-ESPHome](https://github.com/XavierBerger/Solar-Router-for-ESPHome) by Xavier Berger.
