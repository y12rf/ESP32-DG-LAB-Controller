# Main module split — Task 3 report

## Baseline

- Scope: extract strength and waveform output control from `src/main.cpp` into
  `OutputController` while preserving Task 2 behavior and all existing protocol
  bytes, branches, logs, ordering, and 100 ms timing.
- Required verification: native tests (attempted) and
  `C:\Users\h\.platformio\penv\Scripts\platformio.exe run -e esp32dev`
  with `PLATFORMIO_CORE_DIR` set to the workspace `.piohome` absolute path.
- Existing unrelated/earlier-task working-tree changes are preserved.

## Migration mapping

- `hexToBytes`, `setStrength_2_0`, A/B adjustment, `currentWaveBlock`,
  `drainStrengthCommand`, and `handleWaveSend` -> `OutputController.cpp`.
- Connection and disconnection output-state tails -> `onConnected` and
  `onDisconnected`.
- BLE B1 strength-response branch -> `onStrengthResponse`.
- Web start/stop/wave operations -> `startSending`, `stopSending`, and
  `selectWave`; strength route uses `adjustStrength`.
- `main.cpp` retains explicit event and lifecycle dispatch only; BLE connection
  calls remain at the same synchronous points and invoke output hooks
  immediately after success.
- Remove `sendData_3_0`, `sendData`, and `setStrength` only if symbol search
  confirms no callers; preserve `sendData_2_0` as the V2 wave path.

## Self-check

- [x] Compare moved function bodies and call order against the baseline; only
  owner/reference names and the requested dispatch calls changed.
- [x] Confirm Task 2 manual-disconnect resume clearing and one-time
  `PWM_AB2` missing-characteristic log remain unchanged.
- [x] Confirm no new task, lock, queue, event mechanism, heap abstraction, or
  unrelated protocol behavior was introduced.
- [x] Review complete shadow diff and run `git diff --check` (only expected
  line-ending warnings).

## Verification results

- Native tests: attempted with the requested PlatformIO command; blocked because
  `gcc` and `g++` are not available in the environment.
- ESP32 firmware build: `SUCCESS` (`RAM 17.9%`, `Flash 50.8%`).
- Shadow commit: this task's final commit (hash reported with handoff).
