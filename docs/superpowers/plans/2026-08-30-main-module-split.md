# `main.cpp` Module Split Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split the current 1156-line `src/main.cpp` into 5–7 focused application modules while preserving every externally observable behavior.

**Architecture:** Use one lightweight `AppState` shared by reference, with statically allocated `AppLog`, `BleManager`, `OutputController`, and `WebUi` objects. Keep waveform data in a stateless module, keep `lib/DgLabControl` unchanged, and let `main.cpp` perform only initialization, BLE-event dispatch, and the existing loop ordering.

**Tech Stack:** C++11, Arduino ESP32, ESP32 BLE Arduino, FreeRTOS queue, Arduino `WebServer`, PlatformIO native/esp32dev environments, Unity tests.

## Global Constraints

- Follow `AGENTS.md`: use `docs/coyote` as the protocol authority, `docs/Web Bluetooth` as the official Demo, and avoid over-defensive programming on ESP32.
- This phase is behavior-preserving only. Do not fix relative-strength payloads, B1 confirmation state, B0 scheduling, or dropped-disconnect recovery.
- Keep all Web paths, parameters, HTML text, HTTP status codes, redirects, BLE prefixes, UUIDs, wave bytes, BF bytes, logs, intervals, retries, MTU, queue capacity, and initial values unchanged.
- Do not add tests or change existing test expectations. Run the existing test and firmware-build commands after every independently compilable task.
- Do not add tasks, locks, queues, dynamic containers, inheritance, virtual methods, event buses, or dependency-injection frameworks.
- Keep every intermediate commit compilable.
- Preserve the user's unrelated `.gitignore` change and all untracked temporary directories.

---

## File Map

- Create `src/AppState.h`: shared application types and cross-module runtime state.
- Create `src/AppLog.h`: fixed-size log API.
- Create `src/AppLog.cpp`: existing timestamped ring-buffer and serial logging behavior.
- Create `src/Waveforms.h`: waveform lookup API.
- Create `src/Waveforms.cpp`: unchanged V2/V3 waveform tables and lengths.
- Create `src/BleManager.h`: BLE event, scanning, connection, and raw-write interface.
- Create `src/BleManager.cpp`: BLE callbacks, queue, scanning, GATT setup, reconnect cleanup, and characteristic writes.
- Create `src/OutputController.h`: strength and waveform control interface.
- Create `src/OutputController.cpp`: existing V2/V3 strength logic, `StrengthController`, `ResumePolicy`, frame construction, and 100 ms output handling.
- Create `src/WebUi.h`: HTTP server interface.
- Create `src/WebUi.cpp`: unchanged page renderer and route handlers.
- Modify `src/main.cpp`: static module construction, setup, event dispatch, and loop orchestration only.
- Do not modify `lib/DgLabControl/src/DgLabControl.h`, `lib/DgLabControl/src/DgLabControl.cpp`, or `test/test_dglab_control/test_main.cpp` in this phase.

---

### Task 1: Extract shared state, logging, and waveform catalog

**Files:**
- Create: `src/AppState.h`
- Create: `src/AppLog.h`
- Create: `src/AppLog.cpp`
- Create: `src/Waveforms.h`
- Create: `src/Waveforms.cpp`
- Modify: `src/main.cpp:42-180,313-351,836-1011`

**Interfaces:**
- Produces: `DeviceType`, `deviceTypeLabel(DeviceType)`, `deviceTypeToInt(DeviceType)`, `parseDeviceType(int)`, `ScannedDevice`, `BleEventType`, `BleEvent`, and `AppState`.
- Produces: `AppLog::add(const String&)`, `AppLog::capacity()`, and `AppLog::newest(size_t)`.
- Produces: `waveforms::current(DeviceType, char, int)`.
- Consumes: `dglab::DeviceIdentity` from `DgLabControl.h`.

- [ ] **Step 1: Record the pre-refactor validation baseline**

Run:

```powershell
pio test -e native
pio run -e esp32dev
```

Expected: the native suite reports all current tests passing; the firmware build ends with `SUCCESS`. Record the reported RAM and Flash percentages in the task notes for comparison after Task 4.

- [ ] **Step 2: Add `AppState.h` with exact existing types and initial values**

Create the following public shape; move the existing type-conversion function bodies unchanged from `main.cpp` into inline functions below the enum:

```cpp
#pragma once

#include <Arduino.h>
#include <DgLabControl.h>
#include <atomic>
#include <stdint.h>

enum class DeviceType : uint8_t { None = 0, DG2 = 1, DG3 = 2 };

inline const char* deviceTypeLabel(DeviceType type) {
  switch (type) {
    case DeviceType::DG2: return "2.0版本";
    case DeviceType::DG3: return "3.0版本";
    default: return "未知版本";
  }
}

inline int deviceTypeToInt(DeviceType type) {
  switch (type) {
    case DeviceType::DG2: return 1;
    case DeviceType::DG3: return 2;
    default: return 0;
  }
}

inline DeviceType parseDeviceType(int value) {
  switch (value) {
    case 1: return DeviceType::DG2;
    case 2: return DeviceType::DG3;
    default: return DeviceType::None;
  }
}

struct ScannedDevice {
  String name;
  String address;
  DeviceType type;
  int rssi;
  dglab::DeviceIdentity identity;
};

enum class BleEventType : uint8_t { StrengthResponse, Disconnected };

struct BleEvent {
  BleEventType type;
  uint8_t sequence;
  uint8_t strengthA;
  uint8_t strengthB;
};

struct AppState {
  DeviceType deviceType = DeviceType::None;
  std::atomic<bool> deviceConnected{false};
  String connectedDeviceName;
  String connectedDeviceAddress;
  bool autoConnectEnabled = true;
  unsigned long lastScanFinished = 0;
  bool scanInProgress = false;
  bool autoScanSuppressedLogged = false;
  int strengthA = 0;
  int strengthB = 0;
  uint8_t orderNo = 0;
  bool isInputAllowed = true;
  bool waitingForResponse = false;
  bool strengthConfirmed = false;
  std::atomic<bool> linkReady{false};
  std::atomic<bool> bleLinkAlive{false};
  bool desiredSending = false;
  bool manualDisconnectRequested = false;
  bool clientCleanupPending = false;
  dglab::DeviceIdentity connectedIdentity = {{0, 0, 0, 0, 0, 0}, 0};
  bool connectedIdentityValid = false;
  DeviceType resumeDeviceType = DeviceType::None;
  bool isSending = false;
  int waveIndex = 0;
  char selectedWave = 'a';
  unsigned long lastSendTime = 0;
};
```

Replace the corresponding globals in `main.cpp` with one `AppState appState;`. During this task, use a temporary local alias or direct `appState.field` access; do not change conditions or assignment order.

- [ ] **Step 3: Extract the existing log ring without changing output**

Create this interface:

```cpp
#pragma once

#include <Arduino.h>
#include <stddef.h>

class AppLog {
 public:
  static constexpr size_t kCapacity = 10;
  void add(const String& message);
  size_t capacity() const { return kCapacity; }
  const String& newest(size_t offset) const;

 private:
  String entries_[kCapacity];
  size_t next_ = 0;
};
```

Implement `add()` with the current `millis() / 1000`, `"s: "`, ring increment, and `Serial.println(message)` operations in the same order. Implement `newest(offset)` with the current index calculation:

```cpp
constexpr size_t AppLog::kCapacity;

const String& AppLog::newest(size_t offset) const {
  const size_t index = (next_ + kCapacity - 1 - offset) % kCapacity;
  return entries_[index];
}
```

Create `AppLog appLog;` in `main.cpp`, replace `addLog(x)` with `appLog.add(x)`, and update the HTML log loop to call `appLog.newest(i)` without changing filtering or markup.

- [ ] **Step 4: Extract waveform constants and lookup**

Declare:

```cpp
#pragma once

#include "AppState.h"

namespace waveforms {
const char* current(DeviceType deviceType, char selectedWave, int waveIndex);
}
```

Move all six existing arrays and their six lengths byte-for-byte to `Waveforms.cpp`. Implement `current()` with the exact existing `getCurrentWave()` switches and fallback `"000000"`. Replace `getCurrentWave()` calls with:

```cpp
waveforms::current(appState.deviceType, appState.selectedWave,
                   appState.waveIndex)
```

- [ ] **Step 5: Run existing verification**

Run:

```powershell
pio test -e native
pio run -e esp32dev
```

Expected: native tests pass and firmware build reports `SUCCESS`. If compilation fails, correct only names, includes, and moved references; do not change behavior.

- [ ] **Step 6: Commit the foundation extraction**

```powershell
git --git-dir=.pushgit --work-tree=. add src/AppState.h src/AppLog.h src/AppLog.cpp src/Waveforms.h src/Waveforms.cpp src/main.cpp
git --git-dir=.pushgit --work-tree=. commit -m "refactor: extract app state logs and waveforms"
```

---

### Task 2: Extract BLE lifecycle and transport

**Files:**
- Create: `src/BleManager.h`
- Create: `src/BleManager.cpp`
- Modify: `src/main.cpp:30-40,75-80,172-311,353-376,533-542,617-833,1134-1155`

**Interfaces:**
- Consumes: `AppState&`, `AppLog&`, `dglab::B0Frame`, `ScannedDevice`, and `BleEvent`.
- Produces: BLE initialization, scanning, connection, event polling, disconnect cleanup, scan-result access, and raw characteristic writes.
- Produces exact methods:
  `begin()`, `pollEvent(BleEvent&)`, `startBleScan()`, `handleAutoScan()`, `connectToDevice(...)`, `disconnectDevice()`, `handleDisconnectEvent()`, `handleDisconnectedClient(bool&)`, `scannedDevices()`, `writeV2WaveBytes(...)`, `writeV2StrengthBytes(...)`, and `writeV3Frame(...)`.

- [ ] **Step 1: Define the BLE manager boundary**

Create `BleManager.h` with this public interface:

```cpp
#pragma once

#include "AppLog.h"
#include "AppState.h"
#include <BLEAdvertisedDevice.h>
#include <BLEClient.h>
#include <BLEDevice.h>
#include <DgLabControl.h>
#include <atomic>
#include <freertos/queue.h>
#include <vector>

class BleManager {
 public:
  BleManager(AppState& state, AppLog& log);
  bool begin();
  bool pollEvent(BleEvent& event);
  bool startBleScan();
  bool handleAutoScan();
  bool connectToDevice(const String& address, DeviceType type,
                       const dglab::DeviceIdentity* identity = nullptr,
                       bool manualSelection = false);
  void disconnectDevice();
  void handleDisconnectEvent();
  bool handleDisconnectedClient(bool& manualDisconnect);
  const std::vector<ScannedDevice>& scannedDevices() const { return scannedDevices_; }
  bool writeV2WaveBytes(const std::vector<uint8_t>& bytesA,
                        const std::vector<uint8_t>& bytesB);
  bool writeV2StrengthBytes(const uint8_t (&bytes)[3]);
  bool writeV3Frame(const dglab::B0Frame& frame);

 private:
  class ScanCallbacks final : public BLEAdvertisedDeviceCallbacks {
   public:
    explicit ScanCallbacks(BleManager& owner) : owner_(owner) {}
    void onResult(BLEAdvertisedDevice device) override;
   private:
    BleManager& owner_;
  };
  class ClientCallbacks final : public BLEClientCallbacks {
   public:
    explicit ClientCallbacks(BleManager& owner) : owner_(owner) {}
    void onConnect(BLEClient* client) override;
    void onDisconnect(BLEClient* client) override;
   private:
    BleManager& owner_;
  };
  AppState& state_;
  AppLog& log_;
  BLEClient* client_ = nullptr;
  BLERemoteCharacteristic* characteristicA2_ = nullptr;
  BLERemoteCharacteristic* characteristicB2_ = nullptr;
  BLERemoteCharacteristic* characteristicWrite3_ = nullptr;
  BLERemoteCharacteristic* characteristicNotify3_ = nullptr;
  QueueHandle_t eventQueue_ = nullptr;
  std::atomic<uint32_t> droppedEvents_{0};
  std::vector<ScannedDevice> scannedDevices_;
  ScanCallbacks scanCallbacks_;
  ClientCallbacks clientCallbacks_;
  static BleManager* notifyOwner_;

  void enqueueEvent(const BleEvent& event);
  static void notifyCallback(BLERemoteCharacteristic*, uint8_t* data,
                             size_t length, bool isNotify);
  void handleNotification(uint8_t* data, size_t length, bool isNotify);
  bool autoConnectNearestDevice();
  dglab::DeviceIdentity makeIdentity(BLEAddress address, uint8_t addressType);
};
```

Initialize the callback members in the constructor as `scanCallbacks_(*this)` and `clientCallbacks_(*this)`. They remain part of the static `BleManager` object; do not allocate callbacks in `begin()`, `loop()`, or per scan.

- [ ] **Step 2: Move BLE constants, callbacks, and event queue unchanged**

Move the V2/V3 UUID constants, `devicePrefix_2_0 = "D-LAB"`, `devicePrefix_3_0 = "47"`, address conversion, advertised-device callback, client callback, notify callback, queue creation, enqueue behavior, and `pollEvent()` implementation into `BleManager.cpp`.

Use the class-static pointer only for the C-style notify callback bridge:

```cpp
BleManager* BleManager::notifyOwner_ = nullptr;

void BleManager::notifyCallback(BLERemoteCharacteristic*, uint8_t* data,
                                size_t length, bool isNotify) {
  if (notifyOwner_) notifyOwner_->handleNotification(data, length, isNotify);
}
```

Set `notifyOwner_ = this` in `begin()`. Preserve the checks `isNotify`, `length >= 4`, and `data[0] == 0xB1` before enqueueing.

- [ ] **Step 3: Move scanning and connection without changing decisions**

Move `autoConnectNearestDevice()`, `connectToDevice()`, `startBleScan()`, and `handleAutoScan()` into `BleManager.cpp`. Replace every global access with the corresponding `state_` field, `scannedDevices_`, or BLE member pointer. Preserve:

```cpp
constexpr unsigned long kAutoScanIntervalMs = 10000;
constexpr int kServiceRetries = 10;
constexpr unsigned long kServiceRetryDelayMs = 150;
```

`startBleScan()` and `handleAutoScan()` return `true` only when their call synchronously creates a usable connection. This lets the caller invoke `OutputController::onConnected(false)` later without making `BleManager` depend on `OutputController`.

- [ ] **Step 4: Move disconnect cleanup and raw writes**

`handleDisconnectEvent()` must perform the current event-consumption operations in order: log, clear `deviceConnected`, clear `linkReady`, then set `clientCleanupPending`.

`handleDisconnectedClient(bool& manualDisconnect)` must return `false` when no cleanup is pending. Otherwise copy the current manual flag into the output reference, perform the current pointer cleanup and `AppState` connection-field updates, and return `true`. Leave strength-controller and resume-policy updates for `OutputController::onDisconnected()` in Task 3.

Implement the three raw write methods from the existing V2 and V3 write lambdas. They return only transport success; packing, strength state updates, and high-level logs remain outside `BleManager`.

- [ ] **Step 5: Temporarily delegate from `main.cpp`**

Construct `BleManager bleManager(appState, appLog);`. Replace direct scanning, connect, disconnect, queue, and raw-write calls with manager calls while leaving strength and Web functions in `main.cpp`.

Add these temporary functions to `main.cpp` so BLE extraction remains independently compilable without making `BleManager` depend on output control:

```cpp
void finalizeConnectedOutput(bool manualSelection) {
  if (appState.deviceType == DeviceType::DG3) {
    appState.strengthA = 0;
    appState.strengthB = 0;
  }
  appState.strengthConfirmed = (appState.deviceType == DeviceType::DG2);
  if (appState.connectedIdentityValid) resumePolicy.remember(appState.connectedIdentity);
  if (manualSelection) {
    appState.desiredSending = false;
    resumePolicy.setDesiredSending(false);
    appState.isSending = false;
  } else {
    appState.isSending = appState.connectedIdentityValid &&
                         resumePolicy.shouldRestore(appState.connectedIdentity);
  }
  appState.desiredSending = appState.isSending;
  resumePolicy.setDesiredSending(appState.desiredSending);
  if (appState.deviceType == DeviceType::DG3) strengthController.resetConnection();
  appState.orderNo = 0;
  appState.isInputAllowed = true;
  appState.waitingForResponse = false;
}

void finalizeDisconnectedOutput(bool manualDisconnect) {
  if (manualDisconnect) {
    resumePolicy.clearIdentity();
    appState.desiredSending = false;
  } else {
    appState.isSending = appState.desiredSending;
  }
  strengthController.resetConnection();
  appState.strengthA = 0;
  appState.strengthB = 0;
  appState.strengthConfirmed = false;
  appState.waitingForResponse = false;
  appState.isInputAllowed = true;
}
```

Call `finalizeConnectedOutput(true)` immediately after a successful manual connection. Call `finalizeConnectedOutput(false)` whenever `startBleScan()` or `handleAutoScan()` reports that it auto-connected. After `handleDisconnectedClient(manualDisconnect)` returns `true`, call `finalizeDisconnectedOutput(manualDisconnect)`. Task 3 replaces these temporary functions with `OutputController` methods.

- [ ] **Step 6: Run existing verification**

```powershell
pio test -e native
pio run -e esp32dev
```

Expected: all existing tests pass and the ESP32 build reports `SUCCESS`.

- [ ] **Step 7: Commit BLE extraction**

```powershell
git --git-dir=.pushgit --work-tree=. add src/BleManager.h src/BleManager.cpp src/main.cpp
git --git-dir=.pushgit --work-tree=. commit -m "refactor: extract BLE manager"
```

---

### Task 3: Extract strength and waveform output control

**Files:**
- Create: `src/OutputController.h`
- Create: `src/OutputController.cpp`
- Modify: `src/main.cpp:93-109,313-614,628-655,760-789,1066-1125,1148-1155`

**Interfaces:**
- Consumes: `AppState&`, `AppLog&`, `BleManager&`, `waveforms::current(...)`, `dglab::StrengthController`, and `dglab::ResumePolicy`.
- Produces: connection/disconnection state hooks, BLE strength-event handling, strength adjustment, wave selection, start/stop, strength draining, and periodic output.

- [ ] **Step 1: Define `OutputController`**

Create this interface:

```cpp
#pragma once

#include "AppLog.h"
#include "AppState.h"
#include "BleManager.h"
#include <DgLabControl.h>
#include <vector>

class OutputController {
 public:
  OutputController(AppState& state, AppLog& log, BleManager& ble);
  void onConnected(bool manualSelection);
  void onDisconnected(bool manualDisconnect);
  void onStrengthResponse(const BleEvent& event);
  bool adjustStrength(char channel, int value, uint8_t method);
  void startSending();
  void stopSending();
  void selectWave(char wave);
  void drainStrengthCommand();
  void handleWaveSend();

 private:
  AppState& state_;
  AppLog& log_;
  BleManager& ble_;
  dglab::StrengthController strengthController_;
  dglab::ResumePolicy resumePolicy_;

  std::vector<uint8_t> hexToBytes(const String& hex);
  dglab::WaveBlock currentWaveBlock();
  bool setStrengthV2(int channelA, int channelB);
  bool adjustStrengthA(int value, uint8_t method);
  bool adjustStrengthB(int value, uint8_t method);
  bool sendWaveV2(const String& hexA, const String& hexB);
};
```

- [ ] **Step 2: Move encoding, strength, and wave functions unchanged**

Move the current `hexToBytes()`, `setStrength_2_0()`, `adjustStrengthA()`, `adjustStrengthB()`, `currentWaveBlock()`, `drainStrengthCommand()`, and `handleWaveSend()` bodies to `OutputController.cpp`. Use `state_`, `log_`, `ble_`, `strengthController_`, and `resumePolicy_` without altering branch conditions, arithmetic, logs, frame fields, or assignment order.

Delete the currently unused `sendData_3_0()`, `sendData()`, and `setStrength()` only if a symbol search confirms they have no caller after extraction. Because removing dead functions has no runtime effect, include the removal in this task rather than preserving a second internal B0 path.

- [ ] **Step 3: Move connection-state hooks at the same synchronous points**

Implement `onConnected(manualSelection)` with the output-related tail of the current `connectToDevice()`:

```cpp
if (state_.deviceType == DeviceType::DG3) {
  state_.strengthA = 0;
  state_.strengthB = 0;
}
state_.strengthConfirmed = (state_.deviceType == DeviceType::DG2);
if (state_.connectedIdentityValid) resumePolicy_.remember(state_.connectedIdentity);
if (manualSelection) {
  state_.desiredSending = false;
  resumePolicy_.setDesiredSending(false);
  state_.isSending = false;
} else {
  state_.isSending = state_.connectedIdentityValid &&
                     resumePolicy_.shouldRestore(state_.connectedIdentity);
}
state_.desiredSending = state_.isSending;
resumePolicy_.setDesiredSending(state_.desiredSending);
if (state_.deviceType == DeviceType::DG3) strengthController_.resetConnection();
state_.orderNo = 0;
state_.isInputAllowed = true;
state_.waitingForResponse = false;
```

Call it immediately after each successful manual or automatic `BleManager::connectToDevice()` return, before sending the HTTP response or advancing the loop.

Implement `onDisconnected(manualDisconnect)` with the output-related current cleanup in the same order: clear/retain desired sending according to `manualDisconnect`, update `ResumePolicy`, reset `StrengthController`, clear strengths and confirmation/waiting fields, and restore `isInputAllowed = true`.

- [ ] **Step 4: Move B1 handling and Web-facing operations**

Implement `onStrengthResponse()` from the existing `processBleEvents()` strength branch, including the same state copies and log text. Implement `startSending()`, `stopSending()`, and `selectWave()` from the existing Web route bodies, including the same `ResumePolicy` updates and log calls.

`adjustStrength(channel, value, method)` dispatches to the unchanged A/B private methods:

```cpp
return channel == 'a' ? adjustStrengthA(value, method)
                      : adjustStrengthB(value, method);
```

- [ ] **Step 5: Reduce `main.cpp` to explicit event dispatch**

Use this event flow:

```cpp
BleEvent event{};
while (bleManager.pollEvent(event)) {
  if (event.type == BleEventType::StrengthResponse) {
    outputController.onStrengthResponse(event);
  } else {
    bleManager.handleDisconnectEvent();
  }
}

bool manualDisconnect = false;
if (bleManager.handleDisconnectedClient(manualDisconnect)) {
  outputController.onDisconnected(manualDisconnect);
}
```

Keep the dispatch at the current `processBleEvents()` and `handleDisconnectedClient()` positions in the loop.

- [ ] **Step 6: Run existing verification**

```powershell
pio test -e native
pio run -e esp32dev
```

Expected: all current tests pass and the firmware build reports `SUCCESS`.

- [ ] **Step 7: Commit output extraction**

```powershell
git --git-dir=.pushgit --work-tree=. add src/OutputController.h src/OutputController.cpp src/main.cpp
git --git-dir=.pushgit --work-tree=. commit -m "refactor: extract output controller"
```

---

### Task 4: Extract the Web UI and finish the thin entry point

**Files:**
- Create: `src/WebUi.h`
- Create: `src/WebUi.cpp`
- Modify: `src/main.cpp:25-28,835-1130,1134-1156`

**Interfaces:**
- Consumes: `AppState&`, `AppLog&`, `BleManager&`, and `OutputController&`.
- Produces: `WebUi::begin()` and `WebUi::handleClient()`.
- Produces final `main.cpp` with static module construction, setup, event dispatch, and the preserved loop order.

- [ ] **Step 1: Define and construct the Web module**

Create:

```cpp
#pragma once

#include "AppLog.h"
#include "AppState.h"
#include "BleManager.h"
#include "OutputController.h"
#include <WebServer.h>

class WebUi {
 public:
  WebUi(AppState& state, AppLog& log, BleManager& ble,
        OutputController& output);
  void begin();
  void handleClient() { server_.handleClient(); }

 private:
  AppState& state_;
  AppLog& log_;
  BleManager& ble_;
  OutputController& output_;
  WebServer server_{80};

  String makeHtml();
  void redirectHome();
};
```

- [ ] **Step 2: Move HTML generation byte-for-byte**

Move `makeHTML()` into `WebUi::makeHtml()`. Replace globals only with state or module accessors:

```cpp
state_.deviceConnected
state_.connectedDeviceName
state_.deviceType
state_.strengthA
state_.strengthB
state_.strengthConfirmed
state_.autoConnectEnabled
state_.selectedWave
state_.isSending
ble_.scannedDevices()
log_.newest(i)
```

Do not alter HTML, CSS, comments, button values, method numbers, refresh interval, or concatenation order.

- [ ] **Step 3: Move all routes without changing responses**

Move all route registrations to `WebUi::begin()`. Add only this deduplication helper, which exactly matches every existing route tail:

```cpp
void WebUi::redirectHome() {
  server_.sendHeader("Location", "/");
  server_.send(302, "text/plain", "");
}
```

For successful manual connection call `output_.onConnected(true)` immediately. When `ble_.startBleScan()` reports an automatic connection, call `output_.onConnected(false)` immediately. Delegate `/start`, `/stop`, `/wave`, and `/strength` to the exact `OutputController` methods while preserving parameter checks and log messages.

- [ ] **Step 4: Replace `main.cpp` with the thin composition root**

The final file must follow this structure and effective order:

```cpp
#include <Arduino.h>
#include <BLEDevice.h>
#include <WiFi.h>
#include "AppLog.h"
#include "AppState.h"
#include "BleManager.h"
#include "OutputController.h"
#include "WebUi.h"

namespace {
constexpr const char* kSsid = "ESP32-Controller";
constexpr const char* kPassword = "12345678";
AppState appState;
AppLog appLog;
BleManager bleManager(appState, appLog);
OutputController outputController(appState, appLog, bleManager);
WebUi webUi(appState, appLog, bleManager, outputController);
}

void setup() {
  Serial.begin(19200);
  Serial.println("DG-LAB 控制器启动");
  WiFi.softAP(kSsid, kPassword, 1, 1);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
  BLEDevice::init("ESP32_DGLAB_Client");
  bleManager.begin();
  webUi.begin();
  appLog.add("系统初始化完成");
  appState.lastScanFinished = millis();
}

void loop() {
  webUi.handleClient();

  BleEvent event{};
  while (bleManager.pollEvent(event)) {
    if (event.type == BleEventType::StrengthResponse) {
      outputController.onStrengthResponse(event);
    } else {
      bleManager.handleDisconnectEvent();
    }
  }

  bool manualDisconnect = false;
  if (bleManager.handleDisconnectedClient(manualDisconnect)) {
    outputController.onDisconnected(manualDisconnect);
  }

  outputController.drainStrengthCommand();
  outputController.handleWaveSend();
  if (bleManager.handleAutoScan()) outputController.onConnected(false);
  delay(10);
}
```

If `bleManager.begin()` cannot create the existing queue, preserve current behavior by allowing later enqueue attempts to increment the dropped count; do not add a new fatal path.

- [ ] **Step 5: Verify source-level behavior invariants**

Run searches and compare against the pre-refactor commit:

```powershell
Select-String -Path src/*.cpp,src/*.h -Pattern 'D-LAB|"47"|955a180b|0000180c|0000150a|0000150b|0xBF|200, 200, 128, 0, 128, 0'
Select-String -Path src/WebUi.cpp -Pattern 'server_\.on\('
Select-String -Path src/*.cpp -Pattern '10000|150|517|xQueueCreate\(16'
git --git-dir=.pushgit --work-tree=. diff 815ffe1 -- src
```

Expected: both device prefixes, all current UUIDs, BF bytes, all nine routes, the 10-second scan interval, 150 ms retry delay, MTU 517, and queue size 16 are present exactly once in their owning modules. Review the diff to ensure all log and HTML string literals remain unchanged.

- [ ] **Step 6: Run full local verification and compare size**

```powershell
pio test -e native
pio run -e esp32dev
```

Expected: native tests pass; firmware build reports `SUCCESS`; RAM and Flash use show no material increase from the Task 1 baseline.

- [ ] **Step 7: Commit the Web extraction and thin entry point**

```powershell
git --git-dir=.pushgit --work-tree=. add src/WebUi.h src/WebUi.cpp src/main.cpp
git --git-dir=.pushgit --work-tree=. commit -m "refactor: extract web UI and thin main"
```

---

### Task 5: Final review, push, and CI verification

**Files:**
- Review: `src/AppState.h`
- Review: `src/AppLog.h`
- Review: `src/AppLog.cpp`
- Review: `src/Waveforms.h`
- Review: `src/Waveforms.cpp`
- Review: `src/BleManager.h`
- Review: `src/BleManager.cpp`
- Review: `src/OutputController.h`
- Review: `src/OutputController.cpp`
- Review: `src/WebUi.h`
- Review: `src/WebUi.cpp`
- Review: `src/main.cpp`

**Interfaces:**
- Consumes: all four extraction commits and the existing PR branch.
- Produces: a pushed, CI-green behavior-preserving module split ready for the later protocol-fix plan.

- [ ] **Step 1: Confirm only intended files are committed**

```powershell
git --git-dir=.pushgit --work-tree=. status --short
git --git-dir=.pushgit --work-tree=. log --oneline -5
```

Expected: `.gitignore` and temporary directories may remain dirty/untracked, but none is staged or included in the four refactor commits.

- [ ] **Step 2: Run the final local commands once more**

```powershell
pio test -e native
pio run -e esp32dev
```

Expected: both commands succeed from a clean source state.

- [ ] **Step 3: Push the current PR branch**

```powershell
git --git-dir=.pushgit --work-tree=. push origin codex/non-security-control-fixes
```

Expected: Git reports the branch update on `origin/codex/non-security-control-fixes`.

- [ ] **Step 4: Verify GitHub Actions**

Open PR #14 and wait for the newest `PlatformIO CI` run. Inspect job results and logs rather than relying only on the aggregate badge.

Expected:

- `Run native control tests` succeeds.
- `Build firmware` succeeds.
- The workflow concludes successfully.

- [ ] **Step 5: Report the exact boundary of completion**

Report that the structural split passed existing automated checks. Explicitly state that no new tests or hardware BLE validation were performed and that the known official-protocol issues remain for the next implementation phase.
