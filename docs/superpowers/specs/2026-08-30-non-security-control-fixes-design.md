# Non-Security Control Fixes Design

**Date:** 2026-08-30

**Status:** Implemented and verified in firmware build

## Implementation Record

The implementation was split into small ESP32-facing modules instead of leaving all adapter work in `main.cpp`:

- `AppState` and `AppLog` own application state and the fixed-capacity log.
- `BleManager` owns scanning, profile discovery, notification events, writes, and deferred client cleanup.
- `OutputController` owns strength requests, the 100 ms output cycle, and reconnect-resume decisions.
- `Waveforms` owns compile-time fixed byte tables copied from the official Web Bluetooth demo.
- `WebUi` owns HTTP routes and rendering; `main.cpp` only initializes and sequences the loop.

The final protocol alignment also includes behavior discovered during the official-document re-audit:

- DG-LAB 2.0 `1504` is treated as read/write/notify. Its initial three-byte value and later notifications are decoded as two little-endian 11-bit strengths; predicted local writes remain unconfirmed until feedback arrives.
- DG-LAB 3.0 emits one combined B0 per due 100 ms cycle. Strength intent is committed only after that frame is written, and BF is written after every connection.
- BLE callbacks only update the link-alive atomic or enqueue fixed-size events. If the queue is full, a disconnect is recovered from the callback-updated atomic before client cleanup.
- Wave tables are stored as fixed bytes, so the 100 ms path performs no hexadecimal parsing, temporary `String` slicing, or `vector` allocation.

The Native environment builds only `Waveforms.cpp` from `src` in addition to the pure `DgLabControl` library. This keeps waveform rotation tests host-compatible without compiling Arduino, BLE, or Web sources.

## Goal

Fix the confirmed protocol, state-machine, reconnect, BLE resource-lifecycle, and callback-concurrency defects without expanding the scope into a full firmware rewrite.

## Scope

This design covers:

- Correct DG-LAB 3.0 relative and absolute strength encoding.
- Lossless handling of strength requests while waiting for a B1 response.
- Recovery from a missing B1 response.
- Correct 20-byte B0 construction for both waveform channels.
- A 100 ms waveform schedule for DG-LAB 2.0 and 3.0.
- Automatic waveform recovery only after reconnecting to the same BLE address.
- Preservation of BLE public/random address types.
- Deterministic `BLEClient` cleanup across failures and reconnects.
- Moving mutable state and `String` logging out of BLE callbacks.
- Host-side unit tests for protocol and state-machine behavior.

This design intentionally excludes the previously identified security findings:

- Default Wi-Fi credentials and HTTP authentication.
- State-changing HTTP GET endpoints and CSRF protection.
- HTML escaping and BLE-name/log injection.

## Constraints

- Keep the existing Arduino, ESP32 BLE Arduino, WebServer, and PlatformIO stack.
- Add no runtime third-party dependency.
- Avoid dynamic allocation in the new protocol/state module.
- Keep BLE callbacks short and non-blocking.
- Do not introduce a general event-driven application framework.
- Preserve automatic reconnect and waveform recovery for the same physical BLE address.
- Connecting to a different address must not inherit the previous device's sending state.

## Architecture

### Pure control library

Create `lib/DgLabControl/src/DgLabControl.h` and `lib/DgLabControl/src/DgLabControl.cpp`. The library must compile without Arduino, BLE, FreeRTOS, or `String` headers and use fixed-size value types.

The library owns:

- B0 byte layout and sequence/method nibble encoding.
- Eight-byte channel waveform blocks and the fixed 20-byte B0 frame.
- Per-channel pending strength intents.
- The single in-flight strength command and its B1 deadline.
- Confirmed-strength values and whether feedback is currently synchronized.
- Reconnect-resume decisions based on exact BLE address identity.

The library does not own:

- BLE characteristics or writes.
- Web request parsing.
- Logging or HTML.
- FreeRTOS queues.
- Scanning and connection establishment.

### ESP32 adapters

`WebUi` translates Web requests into typed control operations, `OutputController` supplies the current waveform and owns the 100 ms schedule, and `BleManager` performs BLE writes and returns fixed-size feedback events to the loop. `main.cpp` only sequences these modules.

`OutputController` remains responsible for DG-LAB 2.0 strength writes because that protocol does not use B0/B1 sequencing. It uses the same rollover-safe 100 ms scheduling rule for wave output.

### Host tests

Create `test/test_dglab_control/test_main.cpp` using PlatformIO Native and Unity. Add an `[env:native]` environment to `platformio.ini`. CI runs native tests before the existing ESP32 firmware build.

The current Windows workstation has no host C++ compiler, so native tests are expected to run in GitHub Actions until a supported Windows C++ toolchain is installed. ESP32 compilation remains locally available through the existing PlatformIO toolchain.

## Fixed-Size Data Model

The pure library exposes value types equivalent to the following:

```cpp
enum class Channel : uint8_t { A, B };
enum class StrengthOperation : uint8_t { Increase, Decrease, Absolute };
enum class RequestDisposition : uint8_t { Ready, Queued, Rejected };

struct DeviceIdentity {
  uint8_t address[6];
  uint8_t addressType;
};

struct WaveBlock {
  uint8_t bytes[8];
};

struct B0Frame {
  uint8_t bytes[20];
};

struct PreparedStrengthCommand {
  uint8_t sequenceMethod;
  uint8_t strengthA;
  uint8_t strengthB;
  uint8_t targetStrengthA;
  uint8_t targetStrengthB;
  bool valid;
};
```

`StrengthController` provides these responsibilities through explicit operations:

- `requestStrength(Channel, StrengthOperation, int, uint32_t)` records or merges an intent and returns `Rejected`, `Queued`, or `Prepared`.
- `prepareCommand(uint32_t, PreparedStrengthCommand&)` creates one command when no B1 is outstanding.
- `prepareB0Cycle(...)` applies the 100 ms schedule and combines the prepared strength fields with two complete waveform blocks.
- `commitPrepared(const PreparedStrengthCommand&, uint32_t)` marks a successfully written command as in flight.
- `rollbackPrepared(const PreparedStrengthCommand&)` keeps the prepared intent pending after a failed BLE write.
- `onStrengthResponse(uint8_t sequence, uint8_t strengthA, uint8_t strengthB, uint32_t)` updates confirmed state and returns whether it resolved the in-flight command.
- `tick(uint32_t nowMs)` returns whether it expired an in-flight command after 500 ms, without requeuing or retrying that command.
- `resetConnection()` clears prepared, in-flight, and pending intents and marks strength feedback unsynchronized after a disconnect.
- `isWaveSendDue(uint32_t nowMs, uint32_t lastSendMs)` applies the rollover-safe 100 ms scheduling rule.
- `ResumePolicy` stores one `DeviceIdentity`, decides whether a scan result is the resume target, and returns whether a completed connection may restore `desiredSending`.

The implementation may refine parameter constness, but these responsibilities and state transitions must remain distinct so a failed BLE write cannot be mistaken for an in-flight command.

## Strength Intent Rules

Each channel stores at most one compact pending intent:

- Relative operations are stored as a signed accumulated delta.
- Consecutive relative operations are added together.
- An absolute operation replaces the channel's accumulated relative delta.
- A relative operation received after an absolute operation adjusts that pending absolute target.
- Absolute targets are clamped to `0..200`.
- A relative magnitude above 200 is emitted in chunks no larger than 200, leaving the remainder pending.
- If accumulated relative changes cancel to zero, the pending intent is removed.

The Web adapter accepts only channel A/B and their documented increase, decrease, and absolute method values. It rejects missing, unknown, zero-length, or out-of-range relative requests before calling the typed control API; raw HTTP method integers never enter the encoder.

When neither channel has an in-flight command, A and B pending intents may be combined into one B0 frame. The A method occupies bits 3..2 and the B method occupies bits 1..0. Relative commands place the requested magnitude in the strength field; only absolute commands place a target value in the field.

The sequence cycles through `1..15`. Sequence zero is reserved for waveform-only frames.

## B1 and Timeout State Machine

A successfully written strength frame becomes the sole in-flight command. While it is in flight, new Web requests merge into pending intents instead of producing no-op frames.

Every valid B1 updates the confirmed A/B strengths. A B1 resolves the in-flight command only when its sequence matches. A mismatched or sequence-zero B1 updates the displayed strength but does not release the current command.

If no matching B1 arrives within 500 ms:

- Clear the in-flight command.
- Mark confirmed strength feedback as unsynchronized.
- Do not retry or requeue the expired relative command, because the device may already have applied it.
- Permit the next accumulated intent to be prepared and sent.
- Restore synchronized status after any later valid B1.

The Web log reports `sent`, `queued`, `write failed`, and `feedback timeout` as different outcomes. A queued request must never be logged as already applied.

## Waveform Construction and Timing

A B0 frame always contains exactly:

- Four command bytes.
- Eight A-channel waveform bytes.
- Eight B-channel waveform bytes.

When waveform sending is enabled, strength frames copy the current eight-byte waveform block into both A and B positions. When sending is disabled, both positions use a disabled waveform block with valid frequency bytes and at least one strength byte equal to 101, causing the device to reject waveform output while still processing the strength fields.

Waveform-only frames use sequence and strength method zero. The scheduler writes at most one frame when `uint32_t(nowMs - lastSendMs) >= 100`, then records `lastSendMs = nowMs`. It does not burst-send missed frames. Unsigned subtraction preserves correct behavior across `millis()` rollover.

## Disconnect and Reconnect Semantics

Replace the single sending flag with two concepts:

- `desiredSending`: the user's requested continuous-output state.
- `linkReady`: a fully connected BLE client with all required characteristics and notification registration.

Waveform writes require both values to be true.

On an unexpected disconnect, preserve the exact six-byte BLE address, address type, selected waveform, wave index, and `desiredSending`. Automatic scans look only for that identity while a resume target exists. A successful connection to that same address and address type restores output from the preserved wave index.

If the user explicitly disconnects, clear the resume target and `desiredSending`. If the user manually connects a different address, clear the previous resume target and keep the new device stopped until the user starts it.

## BLE Address and Client Lifecycle

Extend `ScannedDevice` with the advertised `esp_ble_addr_type_t`. Pass the stored type to `BLEClient::connect()` instead of hard-coding `BLE_ADDR_TYPE_RANDOM`.

Only one `BLEClient` may exist at a time:

- Delete a newly created client immediately after a synchronous connection failure.
- For an established connection, request disconnect and defer deletion until its disconnect event has returned to the Arduino loop.
- Do not create another client while cleanup is pending.
- Set all characteristic pointers to null before deleting the owning client.
- Treat missing services, missing write capability, and missing notify capability as connection failures using the same deferred cleanup path.
- Register notifications and write BF configuration after every successful connection or reconnect.

ESP32 BLE Arduino 2.0.0 exposes `registerForNotify()` as a `void` API, so registration success cannot be checked synchronously. Notification health is therefore observed through B1 delivery and the 500 ms feedback timeout rather than an unavailable return value.

## BLE Callback Concurrency

Create a fixed-capacity FreeRTOS queue in `BleManager` for client events. Events contain only fixed-size scalar data: event kind, B1 sequence, and A/B strengths.

BLE notify and client callbacks may only enqueue events and update a small atomic link-alive flag. They must not call `addLog()`, modify Arduino `String` objects, delete clients, or mutate the control state machine.

The Arduino loop drains events before handling Web requests or waveform output. It performs all logging, strength-state updates, disconnect cleanup, and reconnect decisions. If the event queue overflows, callbacks increment a fixed counter; the loop reports the dropped count at a limited rate. A dropped B1 is recovered by the 500 ms timeout rather than by callback-side work.

The existing synchronous BLE scan remains unchanged because it only runs while disconnected and the Arduino loop is blocked for its duration. This avoids an unrelated asynchronous scan rewrite.

## Verification

### Native unit tests

Tests must cover:

- A and B relative increase/decrease encode the requested magnitude rather than a computed target.
- Absolute set and zero encode absolute method bits and clamped targets.
- Combined A/B methods occupy the correct nibbles.
- Every B0 frame has exactly 20 bytes.
- Enabled strength frames contain two complete channel waveform blocks.
- Disabled strength frames suppress both waveform channels.
- Rapid relative requests accumulate, cancellation removes pending work, and absolute requests supersede earlier deltas.
- Matching B1 resolves a command; mismatched B1 only refreshes confirmed strengths.
- A 499 ms wait does not expire and a 500 ms wait does.
- Timeout does not retry the expired relative command and allows later pending work to proceed.
- Same-address reconnect restores `desiredSending`; different-address connection does not.
- The 100 ms scheduler handles normal progression and `millis()` rollover.

### Firmware build

Run:

```text
pio test -e native
pio run -e esp32dev
```

CI must run the native tests before the firmware build.

### Hardware acceptance

With a DG-LAB 3.0 device:

1. Exercise `+1`, `+5`, `-1`, and zero on both channels and confirm there is no target-as-delta jump.
2. Stop waveform output, change strength, and confirm neither channel emits a waveform.
3. Change strength rapidly while a B1 is outstanding and confirm the final device value reflects every queued input.
4. Interrupt BLE during output, return the same device, and confirm output resumes at the preserved wave position.
5. Make another compatible device nearer during the interruption and confirm it is not selected as the resume target.
6. Repeatedly fail connections and perform disconnect/reconnect cycles while sampling free heap; confirm there is no monotonic loss attributable to abandoned clients.

## Documentation Impact

Update `README.md` only where user-visible behavior changes:

- State that unexpected disconnects resume output only after the same device reconnects.
- State that a manually selected different device starts in the stopped state.

Security documentation and credential guidance remain outside this change.
