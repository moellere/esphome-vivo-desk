# ProForm XP 185 U under-desk bike (ESPHome)

Replaces the battery-powered console of a ProForm XP 185 U (Sears 831.21741.0 /
ICON PFCCEX01210) with an ESP32 running ESPHome. The bike has no lower control
board and no serial bus: the console *is* the controller. It drives a 6 V
gearmotor that moves the magnetic brake, reads that motor's feedback
potentiometer, counts a crank reed switch, and amplifies the handlebar pulse
pads. All of that is trivial from an ESP32, so the config in `desk-bike.yaml`
does the same job and exposes it to Home Assistant.

> ⚠️ Not affiliated with ICON / ProForm. You are cutting into your own
> hardware; check everything with a meter before applying power.

## Hardware

| Part | Notes |
|---|---|
| ESP32 dev board (`esp32dev`) | Classic ESP32 or S3. BLE + Wi‑Fi at the same time is tight on a C3, so prefer these. |
| H-bridge | Either the console's own bridge with its two inputs cut free from the blob MCU, or a DRV8833 / TB6612 module. Avoid the L298N (drops ~2 V, leaves the motor with 3 V). |
| 5 V supply | USB‑C receptacle with **5.1 kΩ from CC1 and CC2 to GND** (needed for C‑to‑C cables and PD chargers), 2 A or better. 470 µF + 100 nF at the bridge's motor rail. |
| AD8232 breakout (optional) | ECG front end for the grip electrodes. |
| BLE heart rate strap (optional, recommended) | Anything exposing the standard Heart Rate Service. |

### Lower harness (8 wires)

Identify with the console unplugged:

| Function | Wires | How to find it |
|---|---|---|
| Brake motor | 2 | A few ohms between them. This is the H‑bridge *output*; polarity reversal moves the magnets in or out. |
| Feedback pot | 3 | Two ends read a fixed few kΩ; wiper varies as the motor turns. |
| Crank reed switch | 2 | Open, closes to ~0 Ω as the crank magnet passes. |
| Spare | 1 | Frame ground / shield, an end‑stop switch, or unused. |

Measured on one XP 185 U (pin numbers as counted on the console‑side 8‑pin
footprint; verify yours):

| Pin | Function | Console PCB side |
|---|---|---|
| 1 | Motor A | Output node of one half‑bridge (2N4403 PNP high side + 2N4401 NPN low side) |
| 2 | Motor B | Output node of the other half‑bridge (2N4403 PNP + 2N4401 NPN) |
| 3 | Pot supply | Feed from **3V3** in the new build |
| 4 | Pot wiper | → ADC (`pin_pot_wiper`) |
| 5 | Pot ground | GND |
| 6 | Chassis ground | GND (common with 5 and 8 on the PCB; single‑point ground at the ESP/bridge) |
| 7 | Reed switch | → `pin_reed` (pull‑up enabled) |
| 8 | Reed switch return | GND |

Pins 1/2 reverse polarity across the motor to move the magnets toward or away
from the flywheel. The 2N4401/2N4403 are rated 600 mA each with one device
per side, so the motor's stall current is below that. A saturated PNP + NPN
pair drops about 1 V, so on a 5 V supply the motor sees ~4 V through the
console bridge; a MOSFET module (DRV8833) gives it the full 5 V.

### ESP32 connections (defaults in the `substitutions:` block)

| Signal | ESP pin | Notes |
|---|---|---|
| Bridge IN1 | GPIO18 | 10 kΩ pull‑down to GND so the motor can't run during boot |
| Bridge IN2 | GPIO19 | 10 kΩ pull‑down to GND |
| Pot wiper | GPIO34 | Pot ends to **3V3** and GND (not 5 V) so the wiper stays in ADC range |
| Reed switch | GPIO27 | Other side to GND; internal pull‑up is enabled |
| AD8232 OUTPUT | GPIO35 | |
| AD8232 LO+ / LO‑ | GPIO32 / GPIO33 | Lead‑off detect |
| AD8232 SDN | GPIO25 | Exposed as the "Grip ECG Amplifier" switch |
| AD8232 3.3V / GND | 3V3 / GND | |
| Resistance dial A / B | GPIO16 / GPIO17 | Encoder common to GND; internal pull‑ups |
| Console button 1 / 2 | GPIO21 / GPIO22 | Button common to GND; internal pull‑ups |
| Buzzer | GPIO23 | See *Console controls* |

### Grip electrodes → AD8232

Each grip has two wires. Meter them: if the two wires of one grip are **open**
to each other, each grip has two isolated pads; if they're **shorted**, the grip
is a single electrode with doubled wiring.

| Layout | LA | RA | RL |
|---|---|---|---|
| Isolated pads | one pad of the left grip | one pad of the right grip | the remaining left pad **and** right pad, joined |
| Paralleled pads | left grip | right grip | leave unconnected |

On the XP 185 U the grips use the isolated layout. Each grip's two wires land
on a 4‑pin footprint on the console PCB, and the two **centre pins (the black
wire from each grip) are tied together** there: that pair is the console's
shared reference electrode. So: left non‑black → LA, right non‑black → RA,
both blacks → RL. If you keep the console PCB for its H‑bridge, desolder the
grip wires from that footprint (or cut its outgoing traces) so the old
amplifier no longer loads the electrodes.

Do **not** tie any electrode to ESP ground. RL is the driven‑right‑leg output;
the chip actively drives it to mid‑supply to cancel common‑mode noise, and
grounding an electrode defeats that.

Keep the four grip wires twisted, short, and away from the motor leads.

### Console controls

The console's resistance dial is a 3‑wire quadrature encoder (A, B, common),
the two buttons share a common, and the buzzer is a two‑terminal piezo. All
three carry over:

- **Dial**: each detent steps **Resistance Level** by one. If one detent
  moves it by two or four, raise `encoder_resolution` to `2` or `4`.
- **Button 1** resets the crank revolution counter and beeps. **Button 2**
  only beeps; both are exposed to Home Assistant as binary sensors for your
  own automations.
- **Buzzer**: short chirp on a level change, two‑tone on reset, three slow
  beeps on a brake motor fault. A piezo *disc* can hang straight off the GPIO
  (add a 100 Ω to 1 kΩ series resistor if you like). If the part is a
  *magnetic* buzzer instead (a coil that meters at a few tens of ohms), drive
  it through an NPN transistor with a flyback diode; it will draw more than a
  GPIO can source.

## Reusing the console's H‑bridge

Follow the two motor wires back into the console PCB.

- **Driver IC** (BA6208, BA6218, LB1638, TA7291 or similar): cut the two traces
  from the blob MCU to its inputs and wire the ESP GPIOs in. Check the
  datasheet's truth table; some parts treat both‑high as *brake*, some as
  *forbidden*.
- **Discrete transistors** (the XP 185 U: a 2N4403 PNP high side and a 2N4401
  NPN low side per motor pin). If the two bases of a leg are tied together
  and driven from one node, that node must swing the **full rail**: at 3.3 V
  on a 5 V rail both transistors conduct at once. Never drive it straight
  from an ESP GPIO. Use a small NPN inverter with a pull‑up to 5 V, or a
  74AHCT125 buffer powered from 5 V, per leg (an inverter flips the sense;
  set `in1_raises_pot` accordingly). With full‑swing drive this topology
  cannot shoot through leg‑to‑leg: both inputs high or both low simply
  parks both motor leads on the same rail. If instead the four bases are
  driven separately, the config needs four outputs with interlocking; ask.

If the bridge turns out to be discrete, weigh the time against a DRV8833 module.

## Using two relays as the bridge

Two SPDT relays (a common dual 5 V relay board) make a bridge with no
shoot‑through state at all, and the motor gets the full 5 V through the
contacts:

| Relay board | Connects to |
|---|---|
| Relay 1 COM / NO / NC | motor pin 1 / +5 V / GND |
| Relay 2 COM / NO / NC | motor pin 2 / +5 V / GND |
| VCC (and JD‑VCC if jumpered), GND | +5 V, common GND |
| IN1 / IN2 | `pin_bridge_in1` / `pin_bridge_in2` |

Same truth table as the transistor bridges: both off = both pins grounded,
one on = one direction, both on = both pins at 5 V. Most relay boards are
**active‑low** (the relay pulls in when IN is grounded): set
`bridge_active_low: "true"`. If a relay clicks in during boot, add 10 kΩ from
that IN pin to 5 V. Keep the board away from the AD8232 wiring; the coils
are noisy when they switch.

## Building your own bridge from 2N4401 / 2N4403

If the console bridge is awkward to reuse, this discrete design takes the
ESP32's 3.3 V GPIOs directly, drives the motor from the 5 V rail, and has no
static state that passes current through a leg. Build two identical
half‑bridges, one for `pin_bridge_in1` and one for `pin_bridge_in2`.

```
                          +5V (motor rail)
                 ┌──────────┬──────────────┬────────┐
                 │          │              │        │
               R4 4.7k    R6 220Ω        Q1 E      D1 ▲ 1N5819
                 │          │          2N4403       │  (cathode to +5V)
                 │          │            B ─ R1 220Ω ─┐
                 │          │              C ────────┼──┬──────► MOTOR pin (harness 1 or 2)
                 │          │                        │  │
                 │          │                        │  D2 ▲ 1N5819 (anode to GND)
                 │          │              C ────────┘  │
                 │          │          2N4401           │
                 │          │            B ─ R2 220Ω ─┐ │
                 │          │             Q2 E ───────┼─┼──── GND
                 │          │                         │ │
      GPIO ──┬── R3 1k ── B  Q3 2N4401  C ────────────┘ │   (Q3 collector = node D: to R1 and R4)
             │            E ── GND                      │
             ├── R7 1k ── B  Q4 2N4401  C ──────────────┘   (Q4 collector = node E: to R2 and R6)
             │            E ── GND
            R8 10k
             │
            GND
```

- GPIO **high**: Q3 on → Q1 base pulled low through R1 (~19 mA) → motor pin
  ≈ 4.7 V. Q4 on → node E ≈ 0.1 V → Q2 off.
- GPIO **low**: Q3 off → R4 holds Q1's base at a stiff 5 V → Q1 off. Q4 off →
  Q2 gets ~10 mA through R6 + R2 → motor pin ≈ 0.3 V.
- R8 keeps the GPIO low while the ESP is in reset or being flashed.

The two pre‑drivers are separate on purpose: if the NPN's base current ran
through the pull‑up that holds the PNP's base at 5 V, that node would sag to
~3 V and the PNP would conduct too.

| IN1 | IN2 | Pin 1 | Pin 2 | Motor |
|---|---|---|---|---|
| low | low | GND | GND | off (dynamic brake) |
| high | low | +5 V | GND | direction A |
| low | high | GND | +5 V | direction B |
| high | high | +5 V | +5 V | off |

Per half‑bridge: Q1 2N4403; Q2, Q3, Q4 2N4401; R1, R2 220 Ω; R4 4.7 kΩ;
R6 220 Ω ¼ W; R3, R7 1 kΩ; R8 10 kΩ; D1, D2 1N5819. Once per bridge: 470 µF
+ 100 nF across the 5 V rail at the bridge and 100 nF across the motor.

Each GPIO sources ~5 mA. At ~300 mA motor current both output transistors
saturate and the motor sees ~4.2 V. At a 600 mA stall the low side runs out
of base drive and dissipates ~0.5 W in a TO‑92, so set `stall_timeout_ms`
to `4000` with this bridge. A TIP120 or a logic‑level N‑MOSFET drops into
Q2's place with the same drive if you have one.

Bring‑up: build one half with no motor. Input tied to 3.3 V → output ≈ 4.7 V;
input to GND → output ≈ 0.1 V. With a 100 Ω load to GND during the high test
the rail should draw ~50 mA (Q1 saturated). Then build the second half, add
the motor, and use the jog buttons with the supply current‑limited to 1 A.

## Calibration

1. Flash with the defaults, power up, and open the device in Home Assistant.
2. Press **Brake Jog IN1** a few times and watch **Brake Position** (volts).
   If the voltage *rises*, leave `in1_raises_pot: "true"`; if it falls, set
   `"false"`.
3. Jog to the lightest resistance you want as level 1 (magnets furthest from the
   flywheel) and note the voltage → `pot_v_level1`.
4. Jog the other way to the heaviest usable position and note it →
   `pot_v_level8`. Stop short of the mechanical end stops by a few hundredths
   of a volt so the loop never drives into them.
5. Re‑flash. **Resistance Level** now maps 1–8 linearly between those two
   voltages, and the loop stops within `deadband_v` of the target.

If **Brake Motor Fault** turns on, the motor ran for `stall_timeout_ms` without
reaching the target: wrong `in1_raises_pot`, a target outside the pot's range,
or a jam. Fix the cause and change the level again to clear it.

## Heart rate

- **Strap**: set `hrm_mac`. On boot with the strap on, the ESPHome log lists
  nearby BLE devices with their MACs. **Heart Rate** prefers the strap whenever
  it has reported in the last 15 s.
- **Grip pads**: hold both grips; **Hands On Grips** turns on when both
  lead‑off lines are clear, and **Grip Heart Rate** appears after four
  consecutive beats. Tune `ecg_threshold_v` by logging the raw ADC for a minute
  and setting it to about half the R‑peak height above the baseline. Readings
  are ignored while a hand is off.

## Entities

| Entity | Type | Purpose |
|---|---|---|
| Resistance Level | number 1–8 | Sets the brake |
| Cadence, Crank Revolutions | sensor | From the reed switch |
| Pedaling | binary | Cadence > 0 |
| Heart Rate / Strap Heart Rate / Grip Heart Rate | sensor | Combined and per‑source |
| Hands On Grips | binary | Both electrodes in contact |
| Brake Position, Brake Moving, Brake Motor Fault | diagnostics | Loop state |
| Brake Jog IN1 / IN2 / Stop | buttons | Calibration and manual override |
| Console Button 1 / 2 | binary | Physical buttons on the console |
| Grip ECG Amplifier | switch | AD8232 SDN |

## Secrets

`desk-bike.yaml` expects these in `secrets.yaml`: `wifi_ssid`, `wifi_password`,
`bike_api_key`, `ota_password`, `fallback_ap_password`.

## Not included (yet)

- **Bluetooth FTMS** so Zwift or similar can read cadence and set resistance.
  ESPHome's `esp32_ble_server` can host the service, but the ESP would then be
  a BLE central (strap) and peripheral (FTMS) at once, so budget memory.
- A power estimate. The console's watts were a lookup of level × cadence; do
  the same in a template sensor once you have numbers you trust.
