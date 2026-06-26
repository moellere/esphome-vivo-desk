# VIVO Desk serial protocol (DESK-V110EW / DC-KS7 handset)

Reverse-engineered from the two-wire serial link between the desk's handset and
its controller ("brain"). Documented here so others can verify or extend it.

## Physical / framing

- **19200 baud, 8N1**, TTL UART (3.3 V / 5 V depending on the controller).
- **Two independent one-directional data lines**: handset→brain and brain→handset.
- **Frames are ASCII**, delimited by `:` (0x3A) … `;` (0x3B):
  `: <cmd/data> <2-char checksum> ;`
- **Checksum**: sum of all bytes *between* the leading `:` and the two checksum
  characters, taken mod 256, formatted as **2 uppercase hex ASCII** chars.
  For a frame `d[0..len-1]`: `sum(d[1 .. len-4]) & 0xFF`, then compare the two
  hex chars against `d[len-3]` and `d[len-2]`.

## Brain → handset

| Frame | Meaning |
|-------|---------|
| `:D <height><ck>;` | **Height telemetry**, ASCII (e.g. `:D 74.330;` → ~74.3). Streamed continuously while the brain is receiving a handset heartbeat. |
| `:D8.8.8.76;` | "All segments" boot/no-data display (handset shows `88.8`). |
| `:DE05EE;` | Status/error code `E05` (emitted at boot; on this controller it relates to lost handset comms). |
| `:LL98;` | A limit/status frame seen at boot. |
| `:A41;` | Ack / poll. |

## Handset → brain

Command frames are `:K<cmd><role><2-char checksum>;` where **role `M`** is the
momentary/button frame and **role `B`** is the status/heartbeat variant.

| Command | M frame (button held) | B frame (heartbeat) |
|---------|-----------------------|---------------------|
| UP   | `:KUAM2E;` | `:KUAB23;` |
| DOWN | `:KDAM1D;` | `:KDAB12;` |
| M1   | `:K 1ME9;` | `:K 1BDE;` |
| M2   | `:K 2MEA;` | `:K 2BDF;` |
| M3   | `:K 3MEB;` | `:K 3BE0;` |
| M4   | `:K 4MEC;` | `:K 4BE1;` |

Other handset frames:

| Frame | Meaning |
|-------|---------|
| `:A41;` | Ack / poll. |
| `:DC-KS7.V1.00.1817.0058E;` | Handset firmware/version announce (registration), sent on boot. The `vivo_desk` emulate mode replays this so the brain recognizes it as a handset. |

## Behavior model

- The brain streams `:D<height>;` **continuously as long as it receives a
  handset heartbeat** (the `B` frames). With no heartbeat, the controller can
  fault (e.g. `E05`) and stop streaming.
- While a button is **held**, the handset streams the matching **`M` frame**
  rapidly (~10–15 ms apart). Releasing stops the `M` stream (the brain's
  dead-man timer stops the motor).
- **Memory presets** are recalled by sending the preset's `M`+`B` pair once; the
  brain then drives to the stored position on its own (closed-loop), so presets
  position precisely while a raw timed Up/Down does not.

## Notes

- The frame values above were captured from one DESK-V110EW with a DC-KS7
  handset; other units in the family are expected to match but verify with the
  component's **Silent** (sniffer) mode before driving anything.
