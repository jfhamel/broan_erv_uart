# Broan/NuTone ERV RS-485 protocol, full summary

This document combines what is documented on [spitko.net](https://spitko.net/2025/08/08/Reverse-Engineering-an-ERV/) (project [broan_erv_uart](https://github.com/nspitko/broan_erv_uart)) with conclusions drawn from analyzing the protocol on this installation.

---

## 1. Bus and addressing

- RS-485 bus, one ERV and one or more controllers (wall heads) share the line.
- Each device has an ID (1 byte). The ERV and the controller each have their own; up to 32 addresses would be allowed by the protocol.
- The controller (wall unit or our ESP32 acting as one) = address `10`, the ERV = address `12`.
- **Flow control**: only one device at a time is allowed to talk. The ERV offers control (`04`), the requester replies `05` to take it, sends its messages (each following a request/response round trip), then gives control back by sending `04` again, and so on. Documented on spitko.net.
- **Important**: the `broan_erv_uart` component itself acts as a controller (address `10`, hardcoded in `broan.h`). Once permanently deployed, there is therefore **no longer a physical wall controller on the bus** — our ESP32 stands in for it. Any data the wall controller normally provided (ambient temperature/humidity, see `04 50`/`05 50` below) then has to come from elsewhere: another instrument (HA sensor, probe wired to the ESP32) configured in the YAML.
- **Possible electrical echo**: on some half-duplex RS-485 transceivers, the bytes we transmit ourselves can be read back on our own RX line. An implementation must therefore filter frames by their source address rather than assume everything received comes from the other device.

## 2. Frame structure

```
01 <FROM> <TO> 01 <LEN> <PAYLOAD...> <CHECKSUM> 04
```

- Byte 1: always `01`
- Bytes 2-3: source / destination address
- Byte 4: always `01`
- Byte 5: payload length (payload + checksum, but not the 4 header bytes nor the trailing `04`)
- Payload: depends on the opcode (see section 3)
- Second-to-last byte: checksum, sum of all preceding bytes (starting from the first `01`), minus one, subtracted from zero (8-bit overflow arithmetic). Documented on spitko.net.
- Last byte: always `04`

## 3. Message types (opcodes)

| Opcode | Direction | Description |
|---|---|---|
| `02`/`03` | init | Ping/pong at startup (once only, outside of flow control) |
| `04`/`05` | flow control | Offer / take control of the line |
| `20` | request | Read, list of registers (2 bytes each, no length/data) |
| `21` | response | Read, for each requested register: `<register 2b> <length 1b> <data>` |
| `40` | request | Write, one or more `<register 2b> <length 1b> <data>` |
| `41` | response | Write acknowledgement, `<register 2b>` (sometimes followed by a partial echo) |

The normal read cycle loops through a fixed set of registers (temperature, humidity, filter, mode, speeds, etc.) at a regular interval (roughly 10 times/second depending on bus load).

## 4. Known registers

### Understood and used by the component

| Register | Type | Content |
|---|---|---|
| `00 20` | uint8 | **Full commanded mode** (see table in section 5), covers every mode, including Turbo/Absence/Dehumidistat. |
| `00 22` | uint32 LE (seconds) | **Selected Turbo duration** (1h=3600, 2h=7200, 4h=14400), written once by the controller at activation, in the same frame as `00 20=0x0C`. |
| `01 30` | uint8 | **FilterReset**, setting it to `1` triggers applying the value preloaded in `09 30` to `08 30`. |
| `01 E0` | float32 LE | Temperature (°C), ERV intake sensor. **Not representative during recirculation** (the intake air is then recycled air, not outdoor air) — the HA component publishes `NAN`/unavailable in that case instead of the measured value. |
| `02 20` | uint8 | **Commanded base mode** (partially documented on spitko.net: `0A`=MAX, `09`=MIN, `01`=STB), stays on the last "normal" mode while an overlay mode (Turbo/Absence/Dehumidistat) is active. **Does not reflect the internal Smart-mode switching** between exchange and recirculation (see `07 20` and section 7) — stays frozen at `0x11` the entire time in that specific case. Exposed as `base_fan_mode`. |
| `03 30` | uint16 LE (seconds) | **Bathroom Boost countdown**, same behavior as `04 30` (Turbo), separate register. |
| `04 30` | uint16 LE (seconds) | **Turbo countdown**, decrements in real time, maintained and reported by the ERV. |
| `04 50` | float32 LE | **Ambient humidity (%)**, written by whichever device controls the bus (periodic broadcast, ~20.3s interval). **Comes from the physical wall controller if one is present on the bus, otherwise this value must be supplied by another instrument** (HA sensor, wired probe), since only one controller can be active at a time and the ERV has no humidity sensor of its own. |
| `05 50` | float32 LE | **Ambient temperature (°C)**, transmitted along with `04 50` in the same packet, same origin as above (wall controller, or substitute instrument), distinct from the ERV's own `01 E0`. |
| `06 22`, `08 22` | float32 LE | Speed in % (0%→32, 100%→175 on the scale observed by spitko.net), written together. **Only applies in continuous mode (Manual/`0x0B`)**: in recirculation, the ERV completely ignores a custom target written here, actual airflow stays fixed regardless of the value sent. Recirculation therefore only has 3 fixed steps, never a continuous speed. |
| `07 20` | uint8 | **ERV status code.** Indicates the ERV's real operating mode, such as recirculation or exchange, and fan speed (min, med, max). See table in section 7 for the different states. Exposed as `ventilation_state`. |
| `08 20` | uint8 (bool) | **Schedule activation flag** (`01`=active). See section 11. Understood at the protocol level, but not yet exposed by a component entity. |
| `08 30` | uint32 LE (seconds) | **Remaining filter life**, in seconds, read continuously, feeds the `filter_life` sensor. Also written during a reset (see section 12). |
| `09 30` | uint32 LE (seconds) | **FilterLifeStage.** The wall controller writes the desired value (in seconds) here **before** triggering `01 30=1`; acts as a "preload" register that `FilterReset` then applies to `08 30`. See section 12 for the full sequence. |
| `0A 22`, `0C 22` | float32 LE (%) | **Target humidity threshold** for the Dehumidistat, both registers always receive the same value. |
| `0F 22` | uint8 (bool) | Dehumidistat activation flag (`01`=active). |
| `14 00` | uint32 LE (seconds) | ERV **uptime**, documented on spitko.net. |

### Uncertain meaning

| Register | Type | Content |
|---|---|---|
| `00 50` | uint8 | Periodic write (~10s), value always `00`, likely a heartbeat/keep-alive, exact meaning unknown |
| `02 30`, `03 20` | uint8 | `02 30` stays constant at `01`, `03 20` consistently responds with zero length. Function unknown |
| `07 E0`, `08 E0`, `09 E0` | float32 LE | Possibly three sensors, never documented elsewhere. All three drift slowly and continuously, probably other temperature measurement points, but exact role (intake/exhaust/exchange core?) unconfirmed |
| `09 40` | float32 LE | Only non-zero value in the `40` group during a full scan (everything else in the group responds `0`/empty), ~2434.6 observed, role unknown |
| `0A E0`, `0B E0`, `0C E0`, `0D E0` | float32 LE | Always `-1.0` (`00 00 80 BF`) on this unit, probably a "sensor not present/not applicable on this model" marker rather than an actual measurement |
| `0A F0` | complex block (120 bytes) | Static for the entire duration of a log (identical on every read), so not live telemetry. Structure identified: 10 blocks of 12 bytes, 7 "populated" (`32 00 00 00` + two 4-byte values sharing the same 2 high-order bytes) and 3 "empty" (`FF FF FF FF` + two zeros, same "absent" marker as `0A`-`0D E0`). Probably a history/statistics block (filter or otherwise), not yet correlated to a specific event |
| `0C 50`, `0D 50`, `10 50`, `11 50`, `17 50`, `18 50` | float32 LE | Values (65-122) in the same range as the measured CFM values, possibly other airflow targets (Intermittent/Recirculation steps?) not yet mapped to an HA entity |
| `10 22` | uint8 (bool) | Second Dehumidistat-related flag, always observed as `00`, role still uncertain |
| `19 50`, `20 50`, `21 50` | uint8 | Always observed as `1`, possibly state/configuration flags |
| `24 50` | float32 LE | ~16.67 observed, role unknown, possibly a percentage or a ratio |

## 5. Mode table (`00 20` / `02 20`)

| Mode | Value (`00 20`) | `02 20` while this mode is active |
|---|---|---|
| Off (Standby) | `0x01` | `0x01` |
| Bathroom Boost | `0x02` | *(previous base mode, unchanged)* |
| Recirc min | `0x05` | `0x05` |
| Recirc max | `0x06` | `0x06` |
| Recirc med | `0x07` | `0x07` |
| Int (Intermittent) | `0x08` | `0x08` |
| Cont min | `0x09` | `0x09` |
| Cont max | `0x0A` | `0x0A` |
| Cont med | `0x0B` | `0x0B` |
| Turbo | `0x0C` | *(previous base mode, unchanged)* |
| Dehumidistat | `0x0D` | *(previous base mode, unchanged)* |
| Absence | `0x0F` | *(previous base mode, unchanged)* |
| Auto | `0x10` | `0x10` |
| Smart | `0x11` | `0x11` |

The values `0A`=MAX, `09`=MIN, `01`=STB documented on spitko.net correspond exactly to Cont max, Cont min, and Off.

**Values with no effect**: `0x03`, `0x04`, and `0x0E` were tested by writing directly to `00 20`; each is acknowledged at the transport level (`41 00 20`) but the ERV completely ignores them, `00 20` stays unchanged. Probably unused values, or reserved for other models/features.

## 6. Turbo mode, detailed behavior

The wall controller offers durations of **1h, 2h, or 4h**.

**Activation**, a single write frame combines both registers:
```
40 00 22 04 <duration in seconds, uint32 LE> 00 20 01 0C
```
For the three durations:
- 1h → `10 0E 00 00` = 3600 s
- 2h → `20 1C 00 00` = 7200 s
- 4h → `40 38 00 00` = 14400 s

**Who keeps the countdown?** The ERV. The controller transmits the duration **only once**, at activation. The ERV then decrements its own internal counter in real time, exposed (a different register: `04 30`, read-only) on every normal polling cycle — this lets the controller update its display without having to retransmit anything, and lets the ERV return on its own to the previous mode (`02 20`) once time has elapsed, even if the bus is disrupted.

## 7. Smart mode, internal exchange/recirculation switching

Unlike other modes, Smart doesn't just stay fixed: it alternates on its own, continuously and autonomously, between **exchange** ventilation (fresh outdoor air intake) and **recirculation** (recycled air), also at different speeds, based on internal conditions (indoor humidity and outdoor temperature). The wall controller displays this state (exchange vs. recirculation) and the speed (min/med/max) on its screen. Smart mode's behavior is partially documented in the wall controller spec sheet: https://broan-nutone.com/getmedia/a91b1616-4d06-449c-abfa-0c14add1aa99/Spec_Sheet_WallControls_En_Fr.pdf?ext=.pdf.

Neither `00 20` (commanded mode) nor `02 20` (base mode) move during this internal switching — both stay frozen at `0x11` (Smart) continuously, even when airflow (CFM) clearly shows step changes corresponding to real transitions. The ERV's actual state is instead stored in `07 20`.

Auto mode likely behaves similarly but with different conditions, as described in the controller spec sheet.

### Full `07 20` table

The register is a true status code per mode/step, not a simple exchange/recirculation binary:

| `07 20` | Meaning |
|---|---|
| `00` | Off, and Absence's resting phase |
| `01` | Exchange min |
| `02` | Exchange max |
| `03` | Turbo |
| `04` | Exchange medium |
| `05` | Boost |
| `06` | Recirculation min |
| `07` | Recirculation max |
| `08` | Recirculation medium |

**Implementation consequences**:
- `ventilation_state` (text_sensor) is based entirely on `07 20`.
- Outdoor temperature (`01 E0`) is published as unavailable (`NAN`) whenever `07 20` indicates recirculation — the intake probe is then only sensing recycled air, not real outdoor air.

## 8. Dehumidistat mode, detailed behavior

**Activation/threshold change**:
```
40 0A 22 04 <threshold %, float32 LE> 0C 22 04 <same threshold, float32 LE>
40 0F 22 01 01 10 22 01 00
```
Particularities:
- Unlike Turbo, **no explicit write of `00 20=0x0D`** is needed — the ERV switches its own `00 20` register to `0x0D` as soon as `0F 22=1` is received.
- Changing the threshold while active (e.g. 45%→55%→60%) does not require rewriting the `0F 22`/`10 22` flags each time, only `0A 22`/`0C 22` are rewritten.
- As with Turbo, `02 20` stays frozen on the base mode (e.g. Cont min) for the entire activation, this is the mode the ERV returns to once the humidity threshold is reached.
- **Total lockout of `FanMode` while the Dehumidistat is active**: any attempt to write `00 20` (Exchange min/med/max, Recirculation min/med/max, Turbo, Absence, Off — tested on these five families) is acknowledged at the transport level (`41 00 20`) but **has no real effect**, `00 20` stays at `0x0D` and `07 20` stays at `0x02`. Only disabling the Dehumidistat (`0F 22=0`), and probably reaching the threshold (not confirmed by observation), releases `FanMode` again.

## 9. Bathroom Boost, detailed behavior

Dedicated wall buttons (20 / 40 / 60 minutes), wired separately from the main RS-485 controller.

**Confirmed for all three durations**, via register `03 30`:
- 20 min → `B0 04 00 00` = 1200 s
- 40 min → `60 09 00 00` = 2400 s
- 60 min → `10 0E 00 00` = 3600 s
- Cancel → `00 00 00 00`

**Fundamental difference from Turbo: no write frame (`40`) ever appears on the bus** when the buttons are pressed, neither for `00 20` nor for `03 30`. Only reads (`21`) show the registers changing.

**Conclusion: the bathroom buttons are dry contacts wired to the ERV's OVR (override) terminal**, a dedicated physical input, independent of the RS-485 bus. It's the ERV that detects the contact closing, starts its own internal timer, and exposes it read-only — the controller (wall unit or ESP32) only observes it through normal polling, never writing anything. There is therefore **no way to trigger this mode via an RS-485 write**, only to read it.

The component does not implement triggering this mode from Home Assistant. Instead, it exposes reading the state: active "Bathroom Boost" mode (via `00 20=0x02`) and the remaining time (`03 30`), to distinguish this case from other modes rather than just seeing an unexplained high speed.

As with other overrides, `02 20` stays frozen on the base mode for the entire duration of the boost, and the ERV returns to it on its own once the timer reaches zero (or on cancellation).

## 10. Absence mode, two phases, weekly schedule not explored

Like Intermittent, Absence is not a fixed state: it alternates between a **resting phase** (~50 min/h, `07 20=00`, same code as Off) and an **active phase** (~10 min/h, `07 20=04`, same code as Exchange/Cont Med direct) — both phases reuse already-established codes rather than having their own.

`00 20=0x0F` throughout the mode (both phases), and `02 20` stays on the previous base mode (same behavior as Turbo/Dehumidistat). The weekly schedule mechanism (registers, format) has not been analyzed.

## 11. Schedule mode, detailed behavior

"Schedule" is a selectable mode in its own right on the wall controller, just like Smart or Intermittent, but unlike other modes, it **has no dedicated `00 20` value**.

**Activation**: the wall controller writes `08 20=1`. This is the one and only signal indicating the Schedule is active — `00 20`/`02 20` don't switch to a different value family, they keep displaying the normal step (Exchange/Recirculation min/med/max) dictated by the schedule at that specific moment.

**Sequence observed at activation**:
1. Transient pass through Off (`00 20=0x01`) for a few seconds, visible on the wall controller's screen.
2. Writing the step actually scheduled for the current hour, always accompanied by `08 20=1` in the same frame.
3. `08 20=1` is rewritten repeatedly as long as the Schedule stays active, even without a step change.

**Changing phase while the Schedule is running**: confirmed, the wall controller sends `00 20=<new step> | 08 20=1` directly at the moment of transition, with no intermediate register announcing the change in advance.

**The schedule itself (hours, phases, days) never seems to be transmitted over the RS-485 bus**: no register write corresponding to an hour/day was observed, even when directly changing a phase's start time while a capture was running. The wall controller likely keeps all of this logic in local memory, and only communicates with the ERV at the exact moment of a step change, never the schedule parameters themselves. It is therefore probably impossible to read or drive the Schedule from this component (except by building automations in Home Assistant) — only the real-time result (the current step + the `08 20` flag) is observable.

**Why `08 20` regardless?** Probably for the same reason as `02 20` (base mode): the ERV serves as shared, queryable memory for any controller on the bus. Persisting "this step comes from the Schedule" on the ERV lets a controller that connects afterward (a replacement, or us) know this without access to the original wall controller's internal logic. Still to be confirmed whether this also changes the ERV's own behavior (for example, what it returns to after a Turbo/Boost triggered while the Schedule is active).

**Limitation**: with Schedule mode started from the wall controller, an ESP32 in listen-only mode will display the mode dictated by the wall controller. If the ESP32 takes control, Schedule mode changes will not happen as long as the wall controller doesn't have control.

## 12. Filter reset, three-message sequence

**The wall controller offers 1 to 24 months**, each an exact multiple of 30 days, no real calendar (28-31 days), just `months × 2,592,000 s`. Examples:

```
1 month = 2,592,000 s = exactly 30 days
2 months = 5,184,000 s = exactly 60 days
3 months = 7,776,000 s = exactly 90 days
4 months = 10,368,000 s = exactly 120 days
```

**The sequence, as performed by the wall controller and reproduced by the component:**

```
1) 40 09 30 04 <value in seconds, uint32 LE>             , alone, "preloads" the desired value
2) 40 01 30 01 01  08 30 04 <same value, uint32 LE>      , together: FilterReset=1 + FilterLife
3) 40 08 30 04 <same value, uint32 LE>                   , alone, probably a display confirmation on the wall side
```

All three steps are necessary: `FilterReset` (`01 30=1`) alone is not enough — the ERV ignores the value sent in `FilterLife` (`08 30`) and always reverts to exactly 90 days if `FilterLifeStage` (`09 30`) was not preloaded beforehand (step 1).

Works for any value (not just the wall unit's 4 presets) once all three steps are reproduced. The strict necessity of step 3 (rewriting `08 30` alone) is not confirmed, it is reproduced out of caution.
