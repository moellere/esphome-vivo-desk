# ProForm XP 185 U under-desk bike (ESPHome)

Turns a ProForm XP 185 U (Sears 831.21741.0 / ICON PFCCEX01210) into an
under-desk bike controlled by an ESP32 running ESPHome, with a small
OLED + rotary encoder module as the local UI and everything exposed to Home
Assistant.

**The original console and handlebars are left intact.** The only thing the
build touches is the 8-pin harness coming up from the base: unplug it from the
console, plug it into the new controller. Put the harness back and the bike is
stock again.

The bike has no lower control board and no serial bus: the console *is* the
controller. It drives a 6 V gearmotor that moves the magnetic brake, reads that
motor's feedback potentiometer, counts a crank reed switch, and amplifies the
grip pulse pads. `desk-bike.yaml` reproduces the first three from an ESP32 and
takes heart rate from a BLE chest strap instead of the grips.

> ⚠️ Not affiliated with ICON / ProForm. Check everything with a meter before
> applying power; you are responsible for your own hardware.

## Bill of materials

| Part | Notes |
|---|---|
| ESP32 dev board (`esp32dev`) | Classic ESP32 or S3. An ESP32‑C3 Super Mini works with the alternate pin map in the YAML but is tight on pins and RAM with BLE + Wi‑Fi. |
| 1.3" OLED + EC11 encoder module (Estardyn, SH1116 driver, I2C) | The same module as the scroller project: SDA, SCL, VCC, GND, encoder A/B, PSH, CON (KEY1), BAK (KEY0). |
| H‑bridge | **Recommended:** two MOSFET half‑bridges from IRF4905 + IRFZ44N + 2N3904 (below). Alternatives: a DRV8833/TB6612 module, or a dual 5 V relay board. |
| 5 V supply | USB‑C receptacle with **5.1 kΩ from CC1 and CC2 to GND**, 2 A or better. 470 µF + 100 nF at the bridge's motor rail, 100 nF across the motor. |
| Mating connector for the harness | Whatever the console used (an 8‑pin JST‑XH‑style header on the console PCB), or splice a pigtail. |
| BLE heart‑rate strap (optional) | Anything exposing the standard Heart Rate Service. |
| Resistors | 2 × 10 kΩ (GPIO pull‑downs on the bridge inputs), 2 × 470 Ω, 2 × 1 kΩ, 2 × 5.1 kΩ (USB‑C CC). |

## Circuit

### 8‑pin harness from the base (measured on this bike)

| Pin | Function | Goes to |
|---|---|---|
| 1 | Motor A | bridge output A |
| 2 | Motor B | bridge output B |
| 3 | Pot supply | **3V3** (not 5 V, so the wiper stays inside the ADC range) |
| 4 | Pot wiper | `pin_pot` (GPIO34) |
| 5 | Pot ground | GND |
| 6 | Chassis ground | GND (common with 5 and 8 on the console PCB) |
| 7 | Reed switch | `pin_reed` (GPIO27); internal pull‑up enabled |
| 8 | Reed switch return | GND |

Pins 1/2 reverse polarity across the motor to move the magnets toward or away
from the flywheel. The console's own bridge used one 2N4401 + 2N4403 per
side, so the motor's stall current is under 600 mA.

### ESP32 pin map (defaults in the YAML `substitutions:`)

| Signal | ESP pin | Notes |
|---|---|---|
| Bridge IN1 | GPIO18 | 10 kΩ pull‑down to GND so the motor can't run during boot |
| Bridge IN2 | GPIO19 | 10 kΩ pull‑down to GND |
| Pot wiper | GPIO34 | ADC1, input‑only pin |
| Reed switch | GPIO27 | other side to GND |
| OLED SDA / SCL | GPIO21 / GPIO22 | 3V3 and GND to the module |
| Encoder A / B | GPIO16 / GPIO17 | internal pull‑ups |
| Encoder push (PSH) | GPIO25 | internal pull‑up |
| Back (BAK / KEY0) | GPIO26 | internal pull‑up |
| Confirm (CON / KEY1) | GPIO33 | internal pull‑up |

### Recommended bridge: two MOSFET half‑bridges

Three parts per half‑bridge, no heat, nearly the full 5 V at the motor, and
the FET body diodes handle flyback. Build two, one per motor pin.

| Part | Pin | Connects to |
|---|---|---|
| IRF4905 (P‑MOSFET) | source | +5 V rail |
| IRF4905 | drain | MOTOR pin (harness 1 or 2) |
| IRF4905 | gate | node D |
| IRFZ44N (N‑MOSFET) | source | GND |
| IRFZ44N | drain | MOTOR pin |
| IRFZ44N | gate | node D |
| R1 470 Ω | | node D to +5 V |
| Q1 2N3904 (or 2N2222 / BC337) | collector | node D |
| Q1 | emitter | GND |
| Q1 | base | R2 1 kΩ to ESP GPIO (IN1 or IN2) |
| R3 10 kΩ | | ESP GPIO to GND |

GPIO **high** → Q1 on → node D ≈ 0 V → P‑FET on, N‑FET off → motor pin at
5 V. GPIO **low** → Q1 off → node D pulled to 5 V → P‑FET off, N‑FET on →
motor pin at GND. A floating GPIO at boot reads low through R3, so both pins
start grounded. TO‑220 pinout, flat face toward you, legs down, left→right:
**G‑D‑S**, tab = drain. 2N3904 flat face toward you: **E‑B‑C**. IRF9540
substitutes for the IRF4905. Do not use the 600 V FQPF parts on the low
side; their 3–5 V threshold is too high for a 5 V gate.

| IN1 | IN2 | Pin 1 | Pin 2 | Motor |
|---|---|---|---|---|
| low | low | GND | GND | off (dynamic brake) |
| high | low | +5 V | GND | direction A |
| low | high | GND | +5 V | direction B |
| high | high | +5 V | +5 V | off |

No combination passes current through a leg. The YAML still passes through
all‑off for one 50 ms tick before reversing.

### Alternative: dual relay board

Two SPDT relays: relay 1 COM → motor pin 1, relay 2 COM → motor pin 2, both
NO → +5 V, both NC → GND, VCC/JD‑VCC → 5 V, IN1/IN2 → the bridge GPIOs. Most
boards are active‑low: set `bridge_active_low: "true"`. If a relay clicks in
during boot, add 10 kΩ from that IN pin to 5 V.

### Power and grounding

One 5 V supply feeds everything. The ESP takes its 3.3 V from its own
regulator; the pot is fed from that 3.3 V. Make a single star ground at the
bridge/ESP; the harness grounds (5, 6, 8) and the chassis all land there.
Bring the motor rail in through the 470 µF + 100 nF pair at the bridge.

### Layout (perfboard)

Left to right, matching signal flow:

1. **USB‑C receptacle** with the two 5.1 kΩ CC resistors, feeding a 5 V rail
   along the top edge and GND along the bottom.
2. **Bridge** next to the harness connector: the two TO‑220 pairs side by
   side, their 2N3904 drivers and 470 Ω / 1 kΩ / 10 kΩ in front of them, the
   470 µF across the rails right there.
3. **Harness header** (8‑pin) on the edge: 1/2 to the bridge outputs, 3 to
   3V3, 4 to GPIO34, 5/6/8 to GND, 7 to GPIO27.
4. **ESP32** in the middle, GPIO side facing the bridge.
5. **OLED/encoder module** on a 4‑pin + 5‑pin header (or a short ribbon) so it
   can mount on the desk edge; keep the I2C leads under ~30 cm.

## The local UI

| Control | Action |
|---|---|
| Encoder turn | resistance level 1–8 (also `number.desk_bike_resistance_level` in HA) |
| Encoder push, short | next display page |
| Encoder push, hold 1–3 s | reset ride time and distance |
| Back | drop to level 1 (coast) and remember the previous level |
| Confirm | return to the remembered level |

Pages: **Ride** (level, rpm, ride clock, distance, time of day, heart rate),
**Heart rate** (large, plus strap status), **Brake** (pot voltage, target,
moving/idle, fault — the calibration page).

## Calibration

1. Flash with the defaults, power up, and open the device in Home Assistant
   (or turn the encoder push to the **Brake** page).
2. Press **Brake Jog IN1** a few times and watch **Brake Position** (volts).
   If the voltage *rises*, leave `in1_raises_pot: "true"`; if it falls, set
   `"false"`.
3. Jog to the lightest resistance you want as level 1 (magnets furthest from
   the flywheel) and note the voltage → `pot_v_level1`.
4. Jog the other way to the heaviest usable position and note it →
   `pot_v_level8`. Stop short of the mechanical end stops by a few hundredths
   of a volt so the loop never drives into them.
5. Re‑flash. **Resistance Level** now maps 1–8 linearly between those two
   voltages, and the loop stops within `deadband_v` of the target.

If **Brake Motor Fault** turns on, the motor ran for `stall_timeout_ms`
without reaching the target: wrong `in1_raises_pot`, a target outside the
pot's range, or a jam. Fix the cause and change the level again to clear it.

`meters_per_rev` sets the virtual distance per crank revolution (4.6 m ≈ a
road bike at 90 rpm / 25 km/h). Tune to taste.

## Heart rate

Set `hrm_mac`. On boot with the strap on, the ESPHome log lists nearby BLE
devices with their MACs. The **Heart rate** page shows whether the strap is
connected. (The grip electrodes on the handlebars are not used; they need
both hands on the bars, which doesn't fit desk use. An AD8232 version lives
in this repo's history if you ever want it.)

## Entities

| Entity | Type | Purpose |
|---|---|---|
| Resistance Level | number 1–8 | Sets the brake |
| Cadence, Crank Revolutions | sensor | From the reed switch |
| Ride Time, Ride Distance | sensor | Session counters; Reset Ride button clears them |
| Pedaling | binary | Cadence > 0 |
| Heart Rate | sensor | From the BLE strap |
| Button, Back, Confirm | binary | The module's keys, for your own automations |
| Brake Position, Brake Moving, Brake Motor Fault | diagnostics | Loop state |
| Brake Jog IN1 / IN2 / Stop | buttons | Calibration and manual override |

## Config layout

`desk-bike.yaml` follows the same shape as the scroller project: `common/
base.yaml` and `common/sensor.yaml` are merged in, fonts come from
`common/fonts/` (slkscr, BebasNeue‑Regular, arial). Remember that a
top‑level key in the main file *replaces* the same key from an include, so
wifi/api/ota/logger are left to `base.yaml`, and this file's `sensor:` block
replaces whatever `sensor.yaml` provides.

## Not included (yet)

- **Bluetooth FTMS** so Zwift or similar can read cadence and set resistance.
  ESPHome's `esp32_ble_server` can host the service, but the ESP would then be
  a BLE central (strap) and peripheral (FTMS) at once, so budget memory.
- A power estimate. The console's watts were a lookup of level × cadence; do
  the same in a template sensor once you have numbers you trust.
