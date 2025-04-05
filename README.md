# Grouter

![A cute, small tree-like character with glowing leaves, holding an orb of solar energy – in flat modern icon style.](/assets/logo.png)
An ESPHome Solar Router (aka Solar Diverter)

## About

Grouter is based on https://github.com/XavierBerger/Solar-Router-for-ESPHome
The goal is the same, to control the energy sent to a load based on the exported energy to reach a level.
This project uses a different logic than it's source.

## Bills of Materials

- ESP32 - 5€
- 4 gang relay boards - 2.5€
- Robotdyn AC Dimmer - 10€/units
- BTA40-800B Triac - 2€/units
- 5V PSU - 1.5€
- Electronic Wires
- Electric Wire
- Electric Junctions (Wago)
- Heatsink + Thermal Paste - Recycling old AM2 CPU heatsink
- Enclosure - 3D Printed + M4 Screws and inserts

Total Cost: Around 30€

## Wiring

![Wiring Diagram](/assets/wiring.png)

## How it works

In my case, I control a 2400W water boiler composed of 3x 800W heating resistors.
The router will control one heating resistor after the other.
Benefits vs a router without relays:
- If the router level is at 33.33%, 66.66% or 100%, there's litterally no heat created by the AC Dimmer as all power goes through relay(s).
- Less heat dissipated by the AC dimmer as less power dimmed
- Less signal noise by the AC dimmer as less power dimmed
- Finer control as dimmed range is 0-800W

## Notes

- Always leave the NC port of relays hanging.
- My PSU is so cheap it doesn't survive the voltage drop created by turning one or more relays on. If it happens, the PSU will just crash, and the ESP32 will reboot and start again. Usually, it works the second time.
- Do not mess up your ground wiring and do not forget to ground your heatsink.
