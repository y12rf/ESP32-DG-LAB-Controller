# Main module split — Task 4 report

## Migration mapping

- `makeHTML()` and its complete HTML/CSS/JS string construction moved to `WebUi::makeHtml()` in `src/WebUi.cpp`.
- `setupWeb()` and all nine existing `WebServer` route registrations moved to `WebUi::begin()`.
- The existing `server.sendHeader("Location", "/")` + `server.send(302, "text/plain", "")` tails are centralized in `WebUi::redirectHome()`; the invalid-strength early return still sends the same response at the same point.
- Web references were mapped to injected module references: `appState` → `state_`, `appLog` → `log_`, `bleManager` → `ble_`, `outputController` → `output_`, and `server` → `server_`.
- `src/main.cpp` now only owns static module construction, Wi-Fi/BLE/Web setup, BLE event dispatch, and the existing loop ordering: Web client → BLE events → disconnect cleanup/output hook → strength drain → wave send → auto scan/output hook → `delay(10)`.

## Verification

- ESP32 firmware: `PLATFORMIO_CORE_DIR=<workspace>/.piohome C:\Users\h\.platformio\penv\Scripts\platformio.exe run -e esp32dev` — **SUCCESS**.
  - RAM: 17.9% (58,756 / 327,680 bytes)
  - Flash: 50.8% (1,597,453 / 3,145,728 bytes)
- Native tests: `PLATFORMIO_CORE_DIR=<workspace>/.piohome C:\Users\h\.platformio\penv\Scripts\platformio.exe test -e native` — **not runnable in this environment**; PlatformIO failed while compiling because `gcc` and `g++` are not installed/on PATH.
- Source checks: nine `server_.on(...)` registrations found; HTML body matches the pre-refactor `makeHTML()` after symbol-only substitution; required BLE prefixes/UUIDs, BF bytes, queue size 16, MTU 517, scan interval 10000 ms, and retry delay 150 ms remain in their owning modules.
- `git --git-dir=.pushgit --work-tree=. diff --check` — no whitespace errors (only existing line-ending warnings for `.gitignore` and `src/main.cpp`).

## Scope/self-check

Only `src/WebUi.h`, `src/WebUi.cpp`, `src/main.cpp`, and this report are Task 4 changes. Existing unrelated `.gitignore`, `.piohome*`, `.pushgit`, and prior `.superpowers` files were left untouched. No protocol behavior, queues, tasks, locks, or additional allocation layers were introduced.
