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

## Reusing the console's H‑bridge

Follow the two motor wires back into the console PCB.

- **Driver IC** (BA6208, BA6218, LB1638, TA7291 or similar): cut the two traces
  from the blob MCU to its inputs and wire the ESP GPIOs in. Check the
  datasheet's truth table; some parts treat both‑high as *brake*, some as
  *forbidden*.
- **Discrete transistors**: the blob's firmware was the shoot‑through protection,
  and now the config is (it always passes through all‑off for one 50 ms tick
  before changing direction). Test each input at 3.3 V through 1 kΩ with a
  current‑limited bench supply, note which leg moves the motor which way, and
  check that 3.3 V fully turns the high‑side transistors off against the 5 V
  rail. If they leak or heat, add a small NPN inverter per input or a
  74AHCT125 buffer powered from 5 V.

If the bridge turns out to be discrete, weigh the time against a DRV8833 module.

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
