# ESPHome VIVO Desk

An [ESPHome](https://esphome.io/) external component that integrates a **VIVO
electric standing desk** (DESK‑V110EW class, with a **DC‑KS7** handset) into Home
Assistant — reading the live **height** and driving **up / down / memory presets**
— while the **physical handset keeps working normally** (display, buttons,
sustained holds, presets).

It works as a transparent serial **interposer**: a small ESP board is spliced
into the two‑wire link between the desk's handset and its controller ("brain").
The component relays the handset⇄brain traffic in both directions (so the handset
stays fully functional), parses the height telemetry for Home Assistant, and can
inject up/down/preset commands from HA alongside the live relay.

> ⚠️ **Use at your own risk.** This involves cutting into your desk's handset
> cable and driving the controller over a reverse‑engineered serial protocol.
> It is not affiliated with or endorsed by VIVO. You are responsible for your
> own hardware.

## Hardware

- **An ESP board with two UARTs.** **ESP32 is strongly recommended** — its
  hardware UARTs are full‑duplex, which is required to relay the fast
  bidirectional traffic while a button is held. ESP8266 (software serial) can
  *read* height and fire one‑shot presets, but its half‑duplex serial cannot
  sustain a held‑button relay reliably. An ESP32‑C3 (e.g. SuperMini) works well.
- The desk's handset cable carries two data lines (handset→brain and
  brain→handset) plus power/ground. You splice the ESP in the middle.

### Wiring

The desk bus is **3.3 V / 5 V TTL UART, 19200 8N1**. Cut the two data wires and
route them through two UARTs on the ESP (pick any free GPIOs; example shown):

| Desk line            | ESP pin (example) | UART        |
|----------------------|-------------------|-------------|
| Handset TX (output)  | GPIO4 (RX)        | `hand_uart` |
| Handset RX (input)   | GPIO5 (TX)        | `hand_uart` |
| Brain TX (output)    | GPIO6 (RX)        | `brain_uart`|
| Brain RX (input)     | GPIO7 (TX)        | `brain_uart`|
| Desk GND             | GND (see note)    | —           |

Identify the two **TX** (output) wires by metering them to GND with the desk
powered and idle — a UART line rests **high**. The other wire in each pair is the
matching RX.

> 💡 **Grounding tip.** If you power the ESP from a *separate* supply (e.g. USB)
> than the desk, tie the desk GND to the ESP GND through a **~100 Ω series
> resistor**. A direct hard ground can form a ground loop that prevents boot,
> while no ground at all leaves the reference floating and the Wi‑Fi radio can
> brown out under load. The resistor gives a defined reference without a hard
> loop. (If you power the ESP *from* the desk's 5 V line, a single shared ground
> is cleanest and no resistor is needed.) **The ESP32‑C3 is not 5 V‑tolerant — if
> your desk's data lines are 5 V, level‑shift them.**

## Usage

```yaml
external_components:
  - source: github://moellere/esphome-vivo-desk
    components: [ vivo_desk ]

uart:
  - id: hand_uart
    rx_pin: GPIO4   # <- Handset TX
    tx_pin: GPIO5   # -> Handset RX
    baud_rate: 19200
  - id: brain_uart
    rx_pin: GPIO6   # <- Brain TX
    tx_pin: GPIO7   # -> Brain RX
    baud_rate: 19200

vivo_desk:
  id: desk
  hand_uart: hand_uart
  brain_uart: brain_uart
  height:
    name: "Desk Height"
```

See [`example.yaml`](example.yaml) for a complete device config including
switches for Up/Down and buttons for the M1–M4 presets, plus the diagnostic
toggles (relay / emulate / sniff) used while bringing a new desk online.

### From a YAML lambda

```yaml
- lambda: id(desk).start_up();     # hold up (until stop())
- lambda: id(desk).start_down();   # hold down
- lambda: id(desk).stop();         # stop movement
- lambda: id(desk).goto_preset(1); # recall stored preset 1..4
```

## Modes

- **Relay** (default): forwards handset⇄brain in both directions so the physical
  handset works (display + buttons + holds + presets), parses height for HA, and
  injects HA up/down/preset commands alongside.
- **Emulate**: the ESP *is* the handset toward the brain — height + control
  without a physical handset present. Streams the handset heartbeat/version/acks.
- **Silent**: pure listen (sniffer) — transmits nothing, logs both lines. Useful
  for capturing a new desk's protocol.

## Protocol

The handset⇄brain protocol (ASCII frames, `:cmd data checksum;`, 19200 8N1) is
documented in [`docs/PROTOCOL.md`](docs/PROTOCOL.md), including the height frame
format, command frames, the heartbeat, and the checksum.

## License

[MIT](LICENSE).
