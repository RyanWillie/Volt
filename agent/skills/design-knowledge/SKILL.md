---
name: design-knowledge
description: Hardware design reference knowledge including component selection rules, common circuit patterns, PCB layout guidelines, and electrical design best practices. Use when deciding what components to use, how to wire circuits, or how to lay out a PCB.
---

# Hardware Design Knowledge

## Component Selection

### Decoupling Capacitors
- **Every IC** needs at least one 100nF ceramic capacitor close to its VCC/GND pins
- High-speed ICs: add 10µF bulk cap in addition to 100nF
- Place as close as possible to the IC power pins

### Pull-up / Pull-down Resistors
- I2C: 4.7kΩ pull-ups on SDA and SCL to VCC
- SPI CS: 10kΩ pull-up to VCC (keep device deselected by default)
- Reset pins: 10kΩ pull-up to VCC with 100nF cap to GND
- UART: 10kΩ pull-up on TX/RX lines if floating

### USB-C Power Input
- CC1 and CC2: 5.1kΩ resistors to GND each (identifies as USB sink)
- ESD protection: TVS diode array (e.g., USBLC6-2SC6)
- VBUS to VCC: Schottky diode or MOSFET switch for reverse polarity protection
- Bulk capacitor: 10µF on VBUS

### Voltage Regulators
- **LDO** (e.g., AP2112K-3.3): simple, low noise, for small load (< 600mA)
  - Input cap: 1µF ceramic
  - Output cap: 1µF ceramic
- **Switching** (e.g., TPS563200): efficient, for higher loads
  - Follow datasheet layout exactly, inductor and caps close to IC

### LED Circuits
- Series resistor: R = (VCC - Vf) / If
  - Red/Yellow: Vf ≈ 2.0V, If = 20mA → R = (3.3 - 2.0) / 0.02 = 65Ω → use 68Ω
  - Blue/White: Vf ≈ 3.0V, If = 20mA → R = (3.3 - 3.0) / 0.02 = 15Ω → use 22Ω
  - For indicator LEDs use lower current: 2-5mA

### Battery Charging (LiPo)
- TP4056: standalone linear charger for single-cell LiPo
  - PROG resistor sets charge current: R = 1200/I(mA) kΩ (e.g., 1.2kΩ for 1A)
  - Needs 4.7µF input and output caps
- DW01A + FS8205: battery protection (over-discharge, over-charge, short circuit)

## Common Circuit Patterns

### Microcontroller Minimum Circuit
1. VCC/GND connections with 100nF decoupling
2. Reset circuit: 10kΩ pull-up + 100nF cap
3. Crystal/oscillator if needed (load caps per datasheet)
4. Programming header (ICSP/SWD/JTAG)
5. Power LED with series resistor (optional)

### I2S Audio Output (MAX98357A)
- BCLK, LRCLK, DIN from MCU
- GAIN pin: pull to GND (9dB), float (12dB), or VCC (15dB)
- Ferrite bead on VCC, 10µF + 100nF decoupling
- Speaker connected between OUTP and OUTN

### SPI Display (ILI9341 / ST7789)
- MOSI, SCK, CS, DC (data/command), RST
- 3.3V logic, some modules have built-in regulator
- Backlight: control with PWM or just connect to VCC through resistor

## PCB Layout Guidelines

### Trace Width
- **Signal traces**: 0.2-0.3mm (8-12mil) for most digital signals
- **Power traces**: 0.5-1.0mm minimum, wider for higher current
- Current capacity: ~1A per 0.25mm width on 1oz copper (outer layer)
- Use the IPC-2221 formula for precise calculations

### Via Sizing
- Standard via: 0.3mm drill, 0.6mm pad
- Power via: 0.4mm drill, 0.8mm pad
- Via-in-pad: avoid unless using filled vias

### Clearances (JLCPCB minimums)
- Trace-to-trace: 0.127mm (5mil)
- Trace-to-pad: 0.127mm
- Pad-to-pad: 0.127mm
- Drill-to-drill: 0.25mm
- Board edge: 0.3mm

### Ground Planes
- **Always use a ground plane** on one layer (typically bottom for 2-layer)
- Connect all GND pads to the plane
- Use thermal relief for hand-soldering
- Keep plane continuous under ICs — avoid splitting

### Component Placement
- Group by function: power section, digital section, analog section
- Place ICs first, then their support components nearby
- Decoupling caps as close as possible to IC power pins
- Keep high-speed signal paths short
- Crystal close to MCU, with ground plane underneath
- Input power connector near board edge

### Board Sizing
- Consider enclosure constraints
- Add mounting holes in corners (M3 = 3.2mm hole)
- Standard PCB thickness: 1.6mm
- Allow 3-5mm from components to board edge

## Design Review Checklist

Before exporting:
1. ✅ All nets connected (ratsnest empty)
2. ✅ ERC passes with no errors
3. ✅ DRC passes with no errors
4. ✅ Decoupling caps on every IC
5. ✅ Pull-up/down resistors where needed
6. ✅ Power traces wide enough
7. ✅ Ground plane intact
8. ✅ Mounting holes placed
9. ✅ Board outline correct size
10. ✅ Silkscreen readable, designators visible
