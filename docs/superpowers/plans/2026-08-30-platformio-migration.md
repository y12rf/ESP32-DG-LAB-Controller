# PlatformIO Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Convert the existing Arduino sketch into a standard PlatformIO project for the generic ESP32 Dev Module.

**Architecture:** Keep the application as one translation unit and move it from the root Arduino sketch into `src/main.cpp`. Add only the PlatformIO build configuration and documentation needed for the new workflow; preserve all runtime behavior.

**Tech Stack:** PlatformIO Core, Espressif 32 platform, Arduino framework, ESP32 WiFi/WebServer/BLE framework libraries

## Global Constraints

- Use `board = esp32dev` and `framework = arduino`.
- Keep the serial monitor speed at `19200`.
- Use the `huge_app.csv` partition table because the linked firmware exceeds the default application partition.
- Do not add third-party libraries.
- Do not alter Wi-Fi, Web, BLE, or waveform-control behavior.
- Do not split the existing application into additional modules during this migration.

---

### Task 1: Convert the repository to PlatformIO layout

**Files:**
- Create: `platformio.ini`
- Create: `src/main.cpp`
- Delete: `ESP32-DG-LAB-Controller.ino`
- Modify: `.gitignore`
- Modify: `README.md`

**Interfaces:**
- Consumes: Existing Arduino sketch entry points `void setup()` and `void loop()`.
- Produces: A PlatformIO `esp32dev` environment that builds the same application and exposes build, upload, and monitor commands.

- [ ] **Step 1: Record the pre-migration source checksum and entry points**

Run:

```powershell
Get-FileHash ESP32-DG-LAB-Controller.ino -Algorithm SHA256
Select-String -Path ESP32-DG-LAB-Controller.ino -Pattern '^void (setup|loop)\(\)'
```

Expected: one SHA-256 hash plus matches for both `setup()` and `loop()`.

- [ ] **Step 2: Add the PlatformIO environment**

Create `platformio.ini` with:

```ini
[env:esp32dev]
platform = platformio/espressif32@6.13.0
board = esp32dev
framework = arduino
monitor_speed = 19200
board_build.partitions = huge_app.csv
```

- [ ] **Step 3: Convert the sketch to a standard C++ source file**

Create `src/main.cpp` by copying the sketch verbatim after this first include:

```cpp
#include <Arduino.h>
```

Delete the root `ESP32-DG-LAB-Controller.ino` after confirming the copied body is byte-for-byte identical to the original apart from the new include.

- [ ] **Step 4: Ignore generated PlatformIO state**

Ensure `.gitignore` contains:

```gitignore
.pio/
.vscode/
```

Keep all existing ignore entries.

- [ ] **Step 5: Document the PlatformIO workflow**

Update `README.md` so the software requirements and installation section name PlatformIO, then document these exact commands:

```bash
pio run
pio run --target upload
pio device monitor
```

Retain the existing device usage, safety notice, and attribution sections.

- [ ] **Step 6: Verify structural equivalence**

Run a comparison that removes the first `#include <Arduino.h>` line from `src/main.cpp` and compares the remaining bytes with the recorded original sketch content.

Expected: no differences. Also run:

```powershell
Select-String -Path src/main.cpp -Pattern '^void (setup|loop)\(\)'
```

Expected: matches for both `setup()` and `loop()`.

- [ ] **Step 7: Build the firmware**

Run:

```bash
pio run
```

Expected: `SUCCESS` for environment `esp32dev`. If `pio` is unavailable, try `platformio run`; if neither executable exists, report the missing local tool without altering project dependencies.

- [ ] **Step 8: Check the final patch**

Run:

```bash
git diff --check
git status --short
```

Expected: no whitespace errors; only the planned PlatformIO migration files are changed.

- [ ] **Step 9: Commit the migration**

```bash
git add platformio.ini src/main.cpp ESP32-DG-LAB-Controller.ino .gitignore README.md
git commit -m "build: migrate project to PlatformIO"
```
