# Testing Guide for TRIAC Dimmer Component

This guide covers how to thoroughly test the TRIAC dimmer component before deploying to high-current applications.

## ⚠️ Safety Warning

**NEVER connect to mains voltage until all tests pass!**

The TRIAC component controls high-voltage AC power. Always:
1. Test with safe loads first (LEDs, oscilloscope)
2. Verify timing is correct before connecting to mains
3. Use proper isolation and safety equipment
4. Have the circuit reviewed by a qualified electrician

---

## Test Levels

### Level 1: Unit Tests (No Hardware Required)

Run pure logic tests on your computer:

```bash
cd components/tests
g++ -std=c++17 -o test_triac test_triac_logic.cpp && ./test_triac
```

**What it tests:**
- Lookup table interpolation
- Duty cycle mapping
- Firing delay calculations
- Power output linearity
- Boundary conditions

Expected output:
```
========================================
TRIAC Dimmer Logic Unit Tests
========================================
Running lookup_table_boundaries... PASSED
Running lookup_table_50_percent... (50% delay = 5004µs) PASSED
Running lookup_table_monotonic... PASSED
...
Results: 13 passed, 0 failed
========================================
```

---

### Level 2: ESPHome Compilation Test

Verify the component compiles correctly:

```bash
cd components/tests
esphome compile test_compile.yaml
```

---

### Level 3: Hardware Test with Safe Load

**Required equipment:**
- ESP32 development board
- LED + 330Ω resistor (connected to gate_pin)
- Signal generator OR another ESP32 generating 50/60Hz square wave
- Oscilloscope (highly recommended)

**Wiring:**
```
ESP32 GPIO26 (gate_pin) ---[330Ω]--- LED --- GND
ESP32 GPIO4 (zc_pin) <--- Signal Generator (50Hz square wave, 3.3V)
```

**Compile and flash:**
```bash
cd components/tests
esphome run test_hardware_safe.yaml
```

**What to verify with oscilloscope:**

1. **Zero-cross detection:**
   - Trigger on GPIO4 (ZC input)
   - Verify detection occurs on falling edge

2. **Gate firing timing:**
   - At 50% power: Gate should go HIGH ~5000µs after ZC
   - At 25% power: Gate should go HIGH ~7500µs after ZC
   - At 75% power: Gate should go HIGH ~2500µs after ZC

3. **Minimum delay:**
   - Even at 99% power, gate should not fire earlier than 90µs after ZC

4. **Response time:**
   - Change power level and verify new timing on next ZC

---

### Level 4: Integration Test with Real ZCD Circuit

**Required equipment:**
- ESP32 development board
- Zero-Cross Detection circuit (e.g., RobotDyn ZCD, Daniel S ZCD)
- AC power source (isolated, through variac if possible)
- Oscilloscope

**Test procedure:**

1. Connect ZCD circuit to AC (through variac at low voltage if available)
2. Connect ZCD output to ESP32 GPIO4
3. Monitor serial output for detected frequency:
   ```
   [zero_cross] Semi-period: 10000µs
   [zero_cross] Frequency: 50.0Hz
   ```
4. Verify stability indicator goes TRUE after ~10 cycles

---

### Level 5: Full System Test with Resistive Load

**Required equipment:**
- Complete TRIAC/SSR circuit
- Low-power resistive load (e.g., 25W incandescent bulb)
- Variac (optional but recommended)
- Oscilloscope with high-voltage differential probe

**Safety precautions:**
- Use GFCI/RCD protected circuit
- Keep one hand behind your back when working near live circuits
- Never touch anything while power is on
- Have fire extinguisher nearby

**Test procedure:**

1. Start with variac at 0V
2. Slowly increase voltage while monitoring:
   - Gate firing (oscilloscope)
   - Load brightness
   - Timing consistency
3. Test all power levels: 0%, 25%, 50%, 75%, 100%
4. Run stress test (fast ramping) for 10 minutes
5. Check for any instability, flickering, or missed firings

---

## Test Checklist

### Before Connecting to High Current:

- [ ] Unit tests pass (Level 1)
- [ ] ESPHome compilation succeeds (Level 2)
- [ ] LED test shows expected timing (Level 3)
- [ ] Oscilloscope confirms firing delays are correct
- [ ] ZCD detection is stable with real AC (Level 4)
- [ ] Detected frequency matches local grid (50Hz or 60Hz)
- [ ] Low-power load test successful (Level 5)
- [ ] No flickering at any power level
- [ ] Smooth ramping from 0% to 100%
- [ ] System stable for >10 minutes continuous operation

### Oscilloscope Measurements to Record:

| Test | Expected | Measured |
|------|----------|----------|
| ZC pulse width | ~200-1000µs | ________ |
| Semi-period (50Hz) | ~10000µs | ________ |
| Semi-period (60Hz) | ~8333µs | ________ |
| Gate delay @ 0% | No pulse | ________ |
| Gate delay @ 25% | ~7500µs | ________ |
| Gate delay @ 50% | ~5000µs | ________ |
| Gate delay @ 75% | ~2500µs | ________ |
| Gate delay @ 100% | ~0µs (always on) | ________ |
| Minimum delay | ≥90µs | ________ |

---

## Troubleshooting

### ZCD not detecting:
- Check pin configuration
- Verify signal level (should be 3.3V logic)
- Check for noise (add 100nF capacitor)

### Inconsistent timing:
- Check for WiFi interference (disable for testing)
- Verify IRAM flags are set
- Check for other interrupts

### Flickering at low power:
- May need snubber circuit
- Check minimum delay setting
- Verify ZCD circuit quality

### Gate not firing:
- Check gate pin configuration
- Verify TRIAC gate current requirement
- Check optocoupler if used

---

## Performance Benchmarks

Expected performance (ESP32 @ 240MHz):

| Metric | Expected | Notes |
|--------|----------|-------|
| ISR latency | <5µs | From ZC to first GPIO |
| Timer precision | 1µs | gptimer resolution |
| Max ISR rate | ~100/sec | 2 per AC cycle |
| CPU usage | <1% | Event-driven design |

---

## Files Reference

- `tests/test_triac_logic.cpp` - Unit tests for logic functions
- `tests/test_compile.yaml` - Minimal compilation test
- `tests/test_hardware_safe.yaml` - Hardware test with safe loads
- `examples/example_triac.yaml` - Full example with WiFi/API
