# ESP32 DG-LAB Serial CLI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a low-overhead, human-oriented serial CLI that exposes the controller's existing Web actions plus English `status` and ANSI `watch` views without changing BLE protocol behavior.

**Architecture:** A pure C++ `CliParser` converts a fixed, mutable line buffer into a small command structure. An Arduino-facing `SerialCli` owns terminal input and rendering, then delegates all device actions to the existing `BleManager` and `OutputController`; `AppLog` gains only a serial-mirror toggle for keeping the live ANSI panel intact.

**Tech Stack:** ESP32 Arduino, PlatformIO, C++11, Unity native tests, Python `unittest` source-contract tests, ANSI terminal sequences.

## Global Constraints

- Keep the Wi-Fi Web UI and all existing HTTP APIs.
- Keep DG-LAB V2/V3 encoding, the 100 ms output scheduler, strength state machine, and reconnect policy unchanged.
- Use a fixed 96-byte input buffer; do not add JSON, regex, dynamic command containers, libraries, tasks, queues, mutexes, or background timers.
- CLI-generated text is English; stored `AppLog` messages remain in their existing language.
- `watch` refreshes in place every 500 ms, uses 20-cell UTF-8 bars, and exits only on `q` followed by Enter.
- Use 115200 baud in both firmware and PlatformIO monitor configuration.
- Preserve the main-loop priority: BLE events and due output before CLI, CLI before HTTP and auto-scan.
- Follow TDD: observe each new test fail before adding the implementation that makes it pass.

---

## File Map

- `src/CliParser.h`: Arduino-independent command enums, parsed-command structure, and parser interface.
- `src/CliParser.cpp`: fixed-buffer, in-place tokenization and validation.
- `src/SerialCli.h`: serial shell state, constants, and private dispatch/render interfaces.
- `src/SerialCli.cpp`: line editing, command execution, English panels, ANSI watch mode.
- `src/AppLog.h`, `src/AppLog.cpp`: retain the ten-entry ring and add a serial-mirror enable flag.
- `src/main.cpp`: construct, initialize, and poll `SerialCli` in the existing application loop.
- `platformio.ini`: compile `CliParser.cpp` for native tests and set monitor speed to 115200.
- `test/test_cli_parser/test_main.cpp`: behavioral parser tests.
- `test/serial_cli_contract_test.py`: source-level integration and ESP32 performance-boundary checks.
- `.github/workflows/platformio.yml`: run the new contract test before native and firmware builds.
- `README.md`: document the serial CLI, baud rate, commands, and verification command.

---

### Task 1: Fixed-buffer CLI parser

**Files:**
- Create: `src/CliParser.h`
- Create: `src/CliParser.cpp`
- Create: `test/test_cli_parser/test_main.cpp`
- Modify: `platformio.ini:13`

**Interfaces:**
- Consumes: A writable, null-terminated ASCII line no longer than the caller's fixed buffer.
- Produces: `CliParseError parseCliCommand(char* line, CliCommand& command)`; on `InvalidArguments`, `command.type` identifies the known command whose usage should be printed.
- Produces types: `CliCommandType`, `CliParseError`, `CliStrengthAction`, and `CliCommand` with `value`, `channel`, `wave`, `strengthAction`, `enabled`, and `start` fields.

- [ ] **Step 1: Write the failing parser tests**

Create `test/test_cli_parser/test_main.cpp`:

```cpp
#include <unity.h>

#include <CliParser.h>
#include <string.h>

namespace {
CliParseError parse(const char* text, CliCommand& command) {
  char line[128];
  strncpy(line, text, sizeof(line) - 1);
  line[sizeof(line) - 1] = '\0';
  return parseCliCommand(line, command);
}
}

void test_simple_commands() {
  const char* inputs[] = {
      "help", "status", "watch", "scan", "devices", "disconnect", "logs"};
  const CliCommandType types[] = {
      CliCommandType::Help, CliCommandType::Status, CliCommandType::Watch,
      CliCommandType::Scan, CliCommandType::Devices,
      CliCommandType::Disconnect, CliCommandType::Logs};
  for (size_t i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i) {
    CliCommand command{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(CliParseError::None),
        static_cast<int>(parse(inputs[i], command)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(types[i]),
                          static_cast<int>(command.type));
  }
}

void test_connect_and_switch_commands() {
  CliCommand command{};
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CliParseError::None),
                        static_cast<int>(parse("connect 12", command)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CliCommandType::Connect),
                        static_cast<int>(command.type));
  TEST_ASSERT_EQUAL_UINT32(12, command.value);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CliParseError::None),
                        static_cast<int>(parse("connect 0", command)));
  TEST_ASSERT_EQUAL_UINT32(0, command.value);

  TEST_ASSERT_EQUAL_INT(static_cast<int>(CliParseError::None),
                        static_cast<int>(parse("autoconnect on", command)));
  TEST_ASSERT_TRUE(command.enabled);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CliParseError::None),
                        static_cast<int>(parse("autoconnect off", command)));
  TEST_ASSERT_FALSE(command.enabled);

  TEST_ASSERT_EQUAL_INT(static_cast<int>(CliParseError::None),
                        static_cast<int>(parse("output start", command)));
  TEST_ASSERT_TRUE(command.start);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CliParseError::None),
                        static_cast<int>(parse("output stop", command)));
  TEST_ASSERT_FALSE(command.start);

  TEST_ASSERT_EQUAL_INT(static_cast<int>(CliParseError::None),
                        static_cast<int>(parse("wave c", command)));
  TEST_ASSERT_EQUAL_CHAR('c', command.wave);
}

void test_strength_commands() {
  const char* inputs[] = {
      "strength a add 5", "strength b sub 10", "strength a set 200"};
  const char channels[] = {'a', 'b', 'a'};
  const CliStrengthAction actions[] = {
      CliStrengthAction::Add, CliStrengthAction::Subtract,
      CliStrengthAction::Set};
  const uint32_t values[] = {5, 10, 200};

  for (size_t i = 0; i < 3; ++i) {
    CliCommand command{};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CliParseError::None),
                          static_cast<int>(parse(inputs[i], command)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CliCommandType::Strength),
                          static_cast<int>(command.type));
    TEST_ASSERT_EQUAL_CHAR(channels[i], command.channel);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(actions[i]),
                          static_cast<int>(command.strengthAction));
    TEST_ASSERT_EQUAL_UINT32(values[i], command.value);
  }
  CliCommand boundary{};
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(CliParseError::None),
      static_cast<int>(parse("strength b set 0", boundary)));
  TEST_ASSERT_EQUAL_UINT32(0, boundary.value);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(CliParseError::None),
      static_cast<int>(parse("strength b set 4294967295", boundary)));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, boundary.value);
}

void test_whitespace_is_accepted_but_uppercase_is_not() {
  CliCommand command{};
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(CliParseError::None),
      static_cast<int>(parse("  strength   a   add   5  ", command)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(CliParseError::UnknownCommand),
      static_cast<int>(parse("STATUS", command)));
}

void test_bad_arguments_are_rejected_for_known_commands() {
  const char* inputs[] = {
      "status now",       "connect",          "connect -1",
      "connect abc",     "connect 4294967296", "autoconnect yes",
      "output go",       "wave d",           "strength c add 1",
      "strength a grow 1", "strength a add -1", "strength a add 1 extra"};
  for (const char* input : inputs) {
    CliCommand command{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(CliParseError::InvalidArguments),
        static_cast<int>(parse(input, command)));
  }
}

void test_unknown_and_empty_commands() {
  CliCommand command{};
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(CliParseError::UnknownCommand),
      static_cast<int>(parse("nope", command)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(CliParseError::UnknownCommand),
      static_cast<int>(parse("   ", command)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_simple_commands);
  RUN_TEST(test_connect_and_switch_commands);
  RUN_TEST(test_strength_commands);
  RUN_TEST(test_whitespace_is_accepted_but_uppercase_is_not);
  RUN_TEST(test_bad_arguments_are_rejected_for_known_commands);
  RUN_TEST(test_unknown_and_empty_commands);
  return UNITY_END();
}
```

Change the native filter in `platformio.ini` so the parser source will be linked once it exists:

```ini
build_src_filter = +<Waveforms.cpp> +<CliParser.cpp>
```

- [ ] **Step 2: Run the parser suite and verify the red state**

Run:

```powershell
pio test -e native -f test_cli_parser
```

Expected: FAIL during compilation because `CliParser.h` does not exist.

- [ ] **Step 3: Add the parser interface**

Create `src/CliParser.h`:

```cpp
#pragma once

#include <stdint.h>

enum class CliCommandType : uint8_t {
  Help,
  Status,
  Watch,
  Scan,
  Devices,
  Connect,
  Disconnect,
  AutoConnect,
  Output,
  Wave,
  Strength,
  Logs
};

enum class CliParseError : uint8_t {
  None,
  UnknownCommand,
  InvalidArguments
};

enum class CliStrengthAction : uint8_t { Add, Subtract, Set };

struct CliCommand {
  CliCommandType type = CliCommandType::Help;
  uint32_t value = 0;
  char channel = '\0';
  char wave = '\0';
  CliStrengthAction strengthAction = CliStrengthAction::Add;
  bool enabled = false;
  bool start = false;
};

CliParseError parseCliCommand(char* line, CliCommand& command);
```

- [ ] **Step 4: Implement in-place parsing**

Create `src/CliParser.cpp`:

```cpp
#include "CliParser.h"

#include <ctype.h>
#include <limits.h>
#include <string.h>

namespace {
char* nextToken(char*& cursor) {
  while (*cursor && isspace(static_cast<unsigned char>(*cursor))) ++cursor;
  if (!*cursor) return nullptr;
  char* token = cursor;
  while (*cursor && !isspace(static_cast<unsigned char>(*cursor))) ++cursor;
  if (*cursor) *cursor++ = '\0';
  return token;
}

bool noMoreTokens(char*& cursor) { return nextToken(cursor) == nullptr; }

bool parseUnsigned(const char* token, uint32_t& value) {
  if (!token || !*token) return false;
  uint32_t parsed = 0;
  for (const char* p = token; *p; ++p) {
    if (*p < '0' || *p > '9') return false;
    const uint32_t digit = static_cast<uint32_t>(*p - '0');
    if (parsed > (UINT32_MAX - digit) / 10U) return false;
    parsed = parsed * 10U + digit;
  }
  value = parsed;
  return true;
}

CliParseError parseNoArgs(char*& cursor) {
  return noMoreTokens(cursor) ? CliParseError::None
                              : CliParseError::InvalidArguments;
}
}

CliParseError parseCliCommand(char* line, CliCommand& command) {
  command = CliCommand{};
  char* cursor = line;
  char* name = nextToken(cursor);
  if (!name) return CliParseError::UnknownCommand;

  struct SimpleCommand {
    const char* name;
    CliCommandType type;
  };
  static const SimpleCommand simple[] = {
      {"help", CliCommandType::Help},
      {"status", CliCommandType::Status},
      {"watch", CliCommandType::Watch},
      {"scan", CliCommandType::Scan},
      {"devices", CliCommandType::Devices},
      {"disconnect", CliCommandType::Disconnect},
      {"logs", CliCommandType::Logs},
  };
  for (const auto& item : simple) {
    if (strcmp(name, item.name) == 0) {
      command.type = item.type;
      return parseNoArgs(cursor);
    }
  }

  if (strcmp(name, "connect") == 0) {
    command.type = CliCommandType::Connect;
    char* index = nextToken(cursor);
    return parseUnsigned(index, command.value) && noMoreTokens(cursor)
               ? CliParseError::None
               : CliParseError::InvalidArguments;
  }

  if (strcmp(name, "autoconnect") == 0) {
    command.type = CliCommandType::AutoConnect;
    char* value = nextToken(cursor);
    if (!value || !noMoreTokens(cursor)) return CliParseError::InvalidArguments;
    if (strcmp(value, "on") == 0) command.enabled = true;
    else if (strcmp(value, "off") == 0) command.enabled = false;
    else return CliParseError::InvalidArguments;
    return CliParseError::None;
  }

  if (strcmp(name, "output") == 0) {
    command.type = CliCommandType::Output;
    char* value = nextToken(cursor);
    if (!value || !noMoreTokens(cursor)) return CliParseError::InvalidArguments;
    if (strcmp(value, "start") == 0) command.start = true;
    else if (strcmp(value, "stop") == 0) command.start = false;
    else return CliParseError::InvalidArguments;
    return CliParseError::None;
  }

  if (strcmp(name, "wave") == 0) {
    command.type = CliCommandType::Wave;
    char* value = nextToken(cursor);
    if (!value || value[1] || !noMoreTokens(cursor) ||
        (value[0] != 'a' && value[0] != 'b' && value[0] != 'c')) {
      return CliParseError::InvalidArguments;
    }
    command.wave = value[0];
    return CliParseError::None;
  }

  if (strcmp(name, "strength") == 0) {
    command.type = CliCommandType::Strength;
    char* channel = nextToken(cursor);
    char* action = nextToken(cursor);
    char* value = nextToken(cursor);
    if (!channel || channel[1] ||
        (channel[0] != 'a' && channel[0] != 'b') || !action ||
        !parseUnsigned(value, command.value) || !noMoreTokens(cursor)) {
      return CliParseError::InvalidArguments;
    }
    command.channel = channel[0];
    if (strcmp(action, "add") == 0) command.strengthAction = CliStrengthAction::Add;
    else if (strcmp(action, "sub") == 0) command.strengthAction = CliStrengthAction::Subtract;
    else if (strcmp(action, "set") == 0) command.strengthAction = CliStrengthAction::Set;
    else return CliParseError::InvalidArguments;
    return CliParseError::None;
  }

  return CliParseError::UnknownCommand;
}
```

- [ ] **Step 5: Run the parser tests and full native suite**

Run:

```powershell
pio test -e native -f test_cli_parser
pio test -e native
```

Expected: the parser suite reports 6 passed tests; the existing `test_dglab_control` suite also remains PASS.

- [ ] **Step 6: Commit the parser foundation**

```powershell
git add src/CliParser.h src/CliParser.cpp test/test_cli_parser/test_main.cpp platformio.ini
git commit -m "feat: add fixed-buffer CLI parser"
```

---

### Task 2: Complete serial CLI, live view, and application wiring

**Files:**
- Create: `src/SerialCli.h`
- Create: `src/SerialCli.cpp`
- Create: `test/serial_cli_contract_test.py`
- Modify: `src/AppLog.h:9-16`
- Modify: `src/AppLog.cpp:5-10`
- Modify: `src/main.cpp:5-61`
- Modify: `platformio.ini:5`

**Interfaces:**
- Consumes: `parseCliCommand(char*, CliCommand&)`, `AppState`, `AppLog`, `BleManager`, and `OutputController` public APIs from the existing firmware.
- Produces: `SerialCli(AppState&, AppLog&, BleManager&, OutputController&)`, `void begin()`, and non-blocking `void handleInput()`.
- Produces for watch integration: `AppLog::setSerialMirrorEnabled(bool)` and `AppLog::serialMirrorEnabled() const`.

- [ ] **Step 1: Write the failing integration contract test**

Create `test/serial_cli_contract_test.py`:

```python
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SERIAL_H = ROOT / "src" / "SerialCli.h"
SERIAL_CPP = ROOT / "src" / "SerialCli.cpp"
APP_LOG_H = ROOT / "src" / "AppLog.h"
APP_LOG_CPP = ROOT / "src" / "AppLog.cpp"
MAIN_CPP = ROOT / "src" / "main.cpp"
PLATFORMIO = ROOT / "platformio.ini"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


class SerialCliStructureTest(unittest.TestCase):
    def test_fixed_input_and_non_blocking_watch(self):
        header = read(SERIAL_H)
        source = read(SERIAL_CPP)
        self.assertIn("kInputCapacity = 96", header)
        self.assertIn("char input_[kInputCapacity]", header)
        self.assertIn("kWatchIntervalMs = 500", header)
        self.assertIn("kBarWidth = 20", header)
        self.assertNotIn("delay(", source)
        self.assertNotIn("xTaskCreate", source)
        self.assertNotIn("ArduinoJson", header + source)

    def test_all_commands_are_dispatched(self):
        source = read(SERIAL_CPP)
        for command in (
            "Help", "Status", "Watch", "Scan", "Devices", "Connect",
            "Disconnect", "AutoConnect", "Output", "Wave", "Strength",
            "Logs",
        ):
            self.assertIn(f"CliCommandType::{command}", source)

    def test_actions_use_existing_controllers(self):
        source = read(SERIAL_CPP)
        for call in (
            "ble_.startBleScan()", "ble_.connectToDevice(",
            "ble_.disconnectDevice()", "output_.onManualConnectionAttempt()",
            "output_.onConnected(true)", "output_.onConnected(false)",
            "output_.startSending()", "output_.stopSending()",
            "output_.selectWave(", "output_.adjustStrength(",
        ):
            self.assertIn(call, source)

    def test_watch_controls_log_mirroring_and_ansi(self):
        source = read(SERIAL_CPP)
        self.assertIn("setSerialMirrorEnabled(false)", source)
        self.assertIn("setSerialMirrorEnabled(true)", source)
        self.assertIn("\\033[2J", source)
        self.assertIn("\\033[H", source)
        self.assertIn('F("█")', source)
        self.assertIn('F("░")', source)


class SerialCliWiringTest(unittest.TestCase):
    def test_log_mirror_is_conditional(self):
        header = read(APP_LOG_H)
        source = read(APP_LOG_CPP)
        self.assertIn("setSerialMirrorEnabled", header)
        self.assertIn("serialMirrorEnabled_", header + source)
        self.assertIn("if (serialMirrorEnabled_)", source)

    def test_cli_runs_after_due_output_and_before_http(self):
        source = read(MAIN_CPP)
        output_at = source.index("outputController.handleWaveSend();")
        cli_at = source.index("serialCli.handleInput();")
        web_at = source.index("webUi.handleClient();")
        self.assertLess(output_at, cli_at)
        self.assertLess(cli_at, web_at)

    def test_baud_rate_is_consistent(self):
        self.assertIn("Serial.begin(115200)", read(MAIN_CPP))
        self.assertIn("monitor_speed = 115200", read(PLATFORMIO))


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the contract test and verify the red state**

Run:

```powershell
python test/serial_cli_contract_test.py
```

Expected: ERROR because `src/SerialCli.h` and `src/SerialCli.cpp` do not exist.

- [ ] **Step 3: Add the minimal AppLog mirror switch**

Change `src/AppLog.h` public and private members to:

```cpp
class AppLog {
 public:
  static constexpr size_t kCapacity = 10;
  void add(const String& message);
  size_t capacity() const { return kCapacity; }
  const String& newest(size_t offset) const;
  void setSerialMirrorEnabled(bool enabled) { serialMirrorEnabled_ = enabled; }
  bool serialMirrorEnabled() const { return serialMirrorEnabled_; }

 private:
  String entries_[kCapacity];
  size_t next_ = 0;
  bool serialMirrorEnabled_ = true;
};
```

Change the final line of `AppLog::add()` in `src/AppLog.cpp`:

```cpp
  if (serialMirrorEnabled_) Serial.println(message);
```

- [ ] **Step 4: Add the SerialCli class declaration**

Create `src/SerialCli.h`:

```cpp
#pragma once

#include "AppLog.h"
#include "AppState.h"
#include "BleManager.h"
#include "CliParser.h"
#include "OutputController.h"

#include <stddef.h>
#include <stdint.h>

class SerialCli {
 public:
  SerialCli(AppState& state, AppLog& log, BleManager& ble,
            OutputController& output);
  void begin();
  void handleInput();

 private:
  static constexpr size_t kInputCapacity = 96;
  static constexpr uint32_t kWatchIntervalMs = 500;
  static constexpr int kBarWidth = 20;

  AppState& state_;
  AppLog& log_;
  BleManager& ble_;
  OutputController& output_;
  char input_[kInputCapacity] = {};
  size_t inputLength_ = 0;
  bool discardLine_ = false;
  bool ignoreNextLf_ = false;
  bool watching_ = false;
  uint32_t lastWatchRefresh_ = 0;

  void processNormalByte(char ch);
  void processWatchByte(char ch);
  void submitLine();
  void execute(const CliCommand& command);
  void printUsage(CliCommandType type);
  void printPrompt();
  void printHelp();
  void printStatus();
  void printDevices();
  void printLogs();
  void enterWatch();
  void exitWatch();
  void renderWatch();
  void printWatchChannel(char channel, int value, int maximum);
  void printBar(int value, int maximum);
  static int humanV2Strength(int raw);
  static const char* versionLabel(DeviceType type);
  const char* feedbackLabel(bool connected) const;
};
```

- [ ] **Step 5: Implement line input, rendering, and command dispatch**

Create `src/SerialCli.cpp` with the following complete implementation:

```cpp
#include "SerialCli.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

SerialCli::SerialCli(AppState& state, AppLog& log, BleManager& ble,
                     OutputController& output)
    : state_(state), log_(log), ble_(ble), output_(output) {}

void SerialCli::begin() {
  Serial.println();
  Serial.println(F("DG-LAB CLI ready. Run 'help'."));
  printPrompt();
}

void SerialCli::printPrompt() { Serial.print(F("$ ")); }

void SerialCli::handleInput() {
  while (Serial.available() > 0) {
    const char ch = static_cast<char>(Serial.read());
    if (watching_) processWatchByte(ch);
    else processNormalByte(ch);
  }
  if (watching_) {
    const uint32_t now = millis();
    if (now - lastWatchRefresh_ >= kWatchIntervalMs) {
      lastWatchRefresh_ = now;
      renderWatch();
    }
  }
}

void SerialCli::processNormalByte(char ch) {
  if (ch == '\n' && ignoreNextLf_) {
    ignoreNextLf_ = false;
    return;
  }
  if (ch == '\r' || ch == '\n') {
    ignoreNextLf_ = ch == '\r';
    Serial.println();
    if (discardLine_) {
      discardLine_ = false;
      inputLength_ = 0;
      Serial.println(F("Command too long."));
    } else {
      input_[inputLength_] = '\0';
      submitLine();
      inputLength_ = 0;
    }
    if (!watching_) printPrompt();
    return;
  }
  ignoreNextLf_ = false;
  if (ch == '\b' || ch == 0x7F) {
    if (!discardLine_ && inputLength_ > 0) {
      --inputLength_;
      Serial.print(F("\b \b"));
    }
    return;
  }
  if (ch < 0x20 || ch > 0x7E || discardLine_) return;
  if (inputLength_ >= kInputCapacity - 1) {
    discardLine_ = true;
    return;
  }
  input_[inputLength_++] = ch;
  Serial.write(static_cast<uint8_t>(ch));
}

void SerialCli::processWatchByte(char ch) {
  if (ch == '\n' && ignoreNextLf_) {
    ignoreNextLf_ = false;
    return;
  }
  if (ch == '\r' || ch == '\n') {
    ignoreNextLf_ = ch == '\r';
    input_[inputLength_] = '\0';
    const bool quit = !discardLine_ && strcmp(input_, "q") == 0;
    inputLength_ = 0;
    discardLine_ = false;
    if (quit) {
      exitWatch();
    } else {
      Serial.println(F("\r\nPress q then Enter to exit"));
      lastWatchRefresh_ = millis() - kWatchIntervalMs;
    }
    return;
  }
  ignoreNextLf_ = false;
  if (ch < 0x20 || ch > 0x7E || discardLine_) return;
  if (inputLength_ >= kInputCapacity - 1) {
    discardLine_ = true;
    return;
  }
  input_[inputLength_++] = ch;
}

void SerialCli::submitLine() {
  char* cursor = input_;
  while (*cursor == ' ' || *cursor == '\t') ++cursor;
  if (!*cursor) return;
  CliCommand command{};
  const CliParseError error = parseCliCommand(cursor, command);
  if (error == CliParseError::UnknownCommand) {
    Serial.println(F("Unknown command. Run 'help'."));
    return;
  }
  if (error == CliParseError::InvalidArguments) {
    printUsage(command.type);
    return;
  }
  execute(command);
}

const char* SerialCli::versionLabel(DeviceType type) {
  if (type == DeviceType::DG2) return "2.0";
  if (type == DeviceType::DG3) return "3.0";
  return "-";
}

int SerialCli::humanV2Strength(int raw) {
  const int value = raw / 7;
  return value > 292 ? 292 : value;
}

const char* SerialCli::feedbackLabel(bool connected) const {
  if (!connected) return "-";
  if (state_.waitingForResponse) return "waiting";
  return state_.strengthConfirmed ? "confirmed" : "unconfirmed";
}

void SerialCli::printHelp() {
  Serial.println(F("Commands"));
  Serial.println(F("  status"));
  Serial.println(F("  watch"));
  Serial.println(F("  scan"));
  Serial.println(F("  devices"));
  Serial.println(F("  connect <index>"));
  Serial.println(F("  disconnect"));
  Serial.println(F("  autoconnect <on|off>"));
  Serial.println(F("  output <start|stop>"));
  Serial.println(F("  wave <a|b|c>"));
  Serial.println(F("  strength <a|b> <add|sub|set> <value>"));
  Serial.println(F("  logs"));
  Serial.println(F("  help"));
}

void SerialCli::printStatus() {
  const bool connected = state_.deviceConnected.load(std::memory_order_acquire);
  const bool ready = connected &&
      state_.linkReady.load(std::memory_order_acquire);
  Serial.println(F("DG-LAB Controller ────────────────────────"));
  Serial.print(F("Device     "));
  if (connected) Serial.println(state_.connectedDeviceName);
  else Serial.println(F("-"));
  Serial.print(F("Version    "));
  Serial.println(versionLabel(connected ? state_.deviceType : DeviceType::None));
  Serial.print(F("Connected  "));
  Serial.println(connected ? F("yes") : F("no"));
  Serial.print(F("Ready      "));
  Serial.println(ready ? F("yes") : F("no"));
  if (!connected) {
    Serial.println(F("A          -"));
    Serial.println(F("B          -"));
  } else if (state_.deviceType == DeviceType::DG2) {
    Serial.printf("A          %d / 292  (raw %d / 2047)\r\n",
                  humanV2Strength(state_.strengthA), state_.strengthA);
    Serial.printf("B          %d / 292  (raw %d / 2047)\r\n",
                  humanV2Strength(state_.strengthB), state_.strengthB);
  } else {
    Serial.printf("A          %d / 200\r\n", state_.strengthA);
    Serial.printf("B          %d / 200\r\n", state_.strengthB);
  }
  Serial.print(F("Feedback   "));
  Serial.println(feedbackLabel(connected));
  Serial.print(F("Wave       "));
  Serial.println(static_cast<char>(state_.selectedWave - 'a' + 'A'));
  Serial.print(F("Output     "));
  Serial.println(connected && state_.isSending ? F("running") : F("stopped"));
  Serial.print(F("Auto       "));
  Serial.println(state_.autoConnectEnabled ? F("on") : F("off"));
}

void SerialCli::printDevices() {
  const auto& devices = ble_.scannedDevices();
  if (devices.empty()) {
    Serial.println(F("No scanned devices."));
    return;
  }
  Serial.println(F("#  Device       Version  RSSI  Address"));
  for (size_t i = 0; i < devices.size(); ++i) {
    Serial.printf("%u  %-12s %-8s %4d  %s\r\n",
                  static_cast<unsigned>(i + 1), devices[i].name.c_str(),
                  versionLabel(devices[i].type), devices[i].rssi,
                  devices[i].address.c_str());
  }
}

void SerialCli::printLogs() {
  bool any = false;
  for (size_t i = 0; i < log_.capacity(); ++i) {
    const String& entry = log_.newest(i);
    if (!entry.length()) continue;
    any = true;
    Serial.println(entry);
  }
  if (!any) Serial.println(F("No logs."));
}

void SerialCli::printBar(int value, int maximum) {
  int filled = maximum > 0 ? (value * kBarWidth + maximum / 2) / maximum : 0;
  filled = constrain(filled, 0, kBarWidth);
  for (int i = 0; i < kBarWidth; ++i) {
    Serial.print(i < filled ? F("█") : F("░"));
  }
}

void SerialCli::printWatchChannel(char channel, int value, int maximum) {
  Serial.print(F("\033[2K"));
  Serial.printf("%c  %3d / %-3d  ", channel, value, maximum);
  printBar(value, maximum);
  Serial.print(F("\r\n"));
}

void SerialCli::renderWatch() {
  const bool connected = state_.deviceConnected.load(std::memory_order_acquire);
  const bool ready = connected &&
      state_.linkReady.load(std::memory_order_acquire);
  Serial.print(F("\033[H\033[2KDG-LAB Live ──────────────────────────────\r\n"));
  if (!connected) {
    printWatchChannel('A', 0, 200);
    printWatchChannel('B', 0, 200);
  } else if (state_.deviceType == DeviceType::DG2) {
    printWatchChannel('A', humanV2Strength(state_.strengthA), 292);
    printWatchChannel('B', humanV2Strength(state_.strengthB), 292);
  } else {
    printWatchChannel('A', state_.strengthA, 200);
    printWatchChannel('B', state_.strengthB, 200);
  }
  Serial.printf("\033[2KWave %c · %s · %s · %s\r\n\r\n",
                static_cast<char>(state_.selectedWave - 'a' + 'A'),
                connected && state_.isSending ? "sending" : "stopped",
                connected ? (ready ? "ready" : "not ready") : "disconnected",
                feedbackLabel(connected));
  Serial.print(F("Press q then Enter to exit\033[J"));
}

void SerialCli::enterWatch() {
  watching_ = true;
  inputLength_ = 0;
  discardLine_ = false;
  log_.setSerialMirrorEnabled(false);
  Serial.print(F("\033[2J\033[H"));
  lastWatchRefresh_ = millis();
  renderWatch();
}

void SerialCli::exitWatch() {
  watching_ = false;
  log_.setSerialMirrorEnabled(true);
  Serial.print(F("\033[2J\033[H"));
  Serial.println(F("Watch stopped."));
  printPrompt();
}

void SerialCli::printUsage(CliCommandType type) {
  switch (type) {
    case CliCommandType::Connect:
      Serial.println(F("Usage: connect <index>")); break;
    case CliCommandType::AutoConnect:
      Serial.println(F("Usage: autoconnect <on|off>")); break;
    case CliCommandType::Output:
      Serial.println(F("Usage: output <start|stop>")); break;
    case CliCommandType::Wave:
      Serial.println(F("Usage: wave <a|b|c>")); break;
    case CliCommandType::Strength:
      Serial.println(F("Usage: strength <a|b> <add|sub|set> <value>")); break;
    default:
      Serial.println(F("This command takes no arguments.")); break;
  }
}

void SerialCli::execute(const CliCommand& command) {
  switch (command.type) {
    case CliCommandType::Help: printHelp(); return;
    case CliCommandType::Status: printStatus(); return;
    case CliCommandType::Watch: enterWatch(); return;
    case CliCommandType::Devices: printDevices(); return;
    case CliCommandType::Logs: printLogs(); return;
    case CliCommandType::Scan: {
      if (state_.deviceConnected || state_.scanInProgress ||
          state_.clientCleanupPending) {
        Serial.println(F("Controller is busy."));
        return;
      }
      Serial.println(F("Scanning..."));
      if (ble_.startBleScan()) output_.onConnected(false);
      Serial.printf("Found %u devices. Run 'devices' to list them.\r\n",
                    static_cast<unsigned>(ble_.scannedDevices().size()));
      return;
    }
    case CliCommandType::Connect: {
      if (state_.deviceConnected || state_.clientCleanupPending) {
        Serial.println(F("Controller is busy."));
        return;
      }
      const auto& devices = ble_.scannedDevices();
      if (command.value == 0 || command.value > devices.size()) {
        Serial.println(F("Device index out of range."));
        return;
      }
      const ScannedDevice& device = devices[command.value - 1];
      output_.onManualConnectionAttempt();
      if (!ble_.connectToDevice(device.address, device.type, device.identity, true)) {
        Serial.println(F("BLE operation failed."));
        return;
      }
      output_.onConnected(true);
      Serial.print(F("Connected to "));
      Serial.print(device.name);
      Serial.println('.');
      return;
    }
    case CliCommandType::Disconnect:
      if (!state_.deviceConnected) {
        Serial.println(F("Device is not connected."));
      } else {
        ble_.disconnectDevice();
        Serial.println(F("Disconnect requested."));
      }
      return;
    case CliCommandType::AutoConnect:
      state_.autoConnectEnabled = command.enabled;
      log_.add(String("自动连接功能") + (command.enabled ? "已开启" : "已关闭"));
      Serial.println(command.enabled ? F("Auto-connect enabled.")
                                     : F("Auto-connect disabled."));
      return;
    case CliCommandType::Output:
      if (command.start) {
        if (!state_.deviceConnected) {
          Serial.println(F("Device is not connected."));
          return;
        }
        if (!state_.linkReady.load(std::memory_order_acquire)) {
          Serial.println(F("BLE link is not ready."));
          return;
        }
        output_.startSending();
        Serial.println(F("Output started."));
      } else {
        output_.stopSending();
        Serial.println(F("Output stopped."));
      }
      return;
    case CliCommandType::Wave:
      output_.selectWave(command.wave);
      Serial.printf("Wave %c selected.\r\n",
                    static_cast<char>(command.wave - 'a' + 'A'));
      return;
    case CliCommandType::Strength: {
      if (!state_.deviceConnected) {
        Serial.println(F("Device is not connected."));
        return;
      }
      if (!state_.linkReady.load(std::memory_order_acquire)) {
        Serial.println(F("BLE link is not ready."));
        return;
      }
      const uint32_t maximum = state_.deviceType == DeviceType::DG3 ? 200 : 292;
      if (command.value > maximum) {
        Serial.printf("Strength must be between 0 and %u.\r\n",
                      static_cast<unsigned>(maximum));
        return;
      }
      uint8_t method = 0;
      if (command.channel == 'a') {
        method = command.strengthAction == CliStrengthAction::Add ? 4
            : command.strengthAction == CliStrengthAction::Subtract ? 8 : 12;
      } else {
        method = command.strengthAction == CliStrengthAction::Add ? 1
            : command.strengthAction == CliStrengthAction::Subtract ? 2 : 3;
      }
      const dglab::RequestDisposition disposition = output_.adjustStrength(
          command.channel, static_cast<int>(command.value), method);
      if (disposition == dglab::RequestDisposition::Prepared) {
        Serial.println(F("Strength command prepared."));
      } else if (disposition == dglab::RequestDisposition::Queued) {
        Serial.println(F("Strength command queued."));
      } else {
        Serial.println(F("BLE operation failed."));
      }
      return;
    }
  }
}
```

- [ ] **Step 6: Wire the CLI into setup and loop at 115200 baud**

In `src/main.cpp`, add the include:

```cpp
#include "SerialCli.h"
```

Construct the CLI after `OutputController` and before `WebUi`:

```cpp
SerialCli serialCli(appState, appLog, bleManager, outputController);
WebUi webUi(appState, appLog, bleManager, outputController);
```

Change setup and loop to the exact ordering below:

```cpp
void setup() {
  Serial.begin(115200);
  Serial.println("DG-LAB 控制器启动");
  WiFi.softAP(kSsid, kPassword, 1, 1);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
  BLEDevice::init("ESP32_DGLAB_Client");
  bleManager.begin();
  webUi.begin();
  appLog.add("系统初始化完成");
  appState.lastScanFinished = millis();
  serialCli.begin();
}

void loop() {
  processBleEvents();
  bool manualDisconnect = false;
  if (bleManager.handleDisconnectedClient(manualDisconnect)) {
    outputController.onDisconnected(manualDisconnect);
  }

  outputController.handleWaveSend();
  serialCli.handleInput();
  webUi.handleClient();
  if (bleManager.handleAutoScan()) outputController.onConnected(false);
  delay(10);
}
```

Change `platformio.ini`:

```ini
monitor_speed = 115200
```

- [ ] **Step 7: Run the new contract and firmware build**

Run:

```powershell
python test/serial_cli_contract_test.py
pio run -e esp32dev
```

Expected: contract reports 7 passing tests; firmware build reports SUCCESS. Record the RAM and Flash percentages from the build output for the task review.

- [ ] **Step 8: Run all existing automated checks for regression coverage**

Run:

```powershell
python test/web_ui_contract_test.py
pio test -e native
```

Expected: all Web UI contract tests and both native suites PASS.

- [ ] **Step 9: Commit the complete CLI implementation**

```powershell
git add src/SerialCli.h src/SerialCli.cpp src/AppLog.h src/AppLog.cpp src/main.cpp platformio.ini test/serial_cli_contract_test.py
git commit -m "feat: add serial controller CLI"
```

---

### Task 3: CI and user documentation

**Files:**
- Modify: `.github/workflows/platformio.yml:41-48`
- Modify: `README.md:7-87`

**Interfaces:**
- Consumes: The finished CLI command grammar and 115200 baud configuration from Task 2.
- Produces: CI coverage for `test/serial_cli_contract_test.py` and user-facing CLI setup/command documentation.

- [ ] **Step 1: Add the CLI contract to CI**

Insert this step immediately after the WebUi contract step in `.github/workflows/platformio.yml`:

```yaml
      - name: Check serial CLI contracts
        run: python test/serial_cli_contract_test.py
```

- [ ] **Step 2: Document the CLI feature and code structure**

Add this feature bullet after the mobile Web UI bullet in `README.md`:

```markdown
- **串口 CLI**：通过 115200 baud 串口执行扫描、编号连接、强度、波形和输出控制，并提供英文 `status` 与 ANSI `watch` 状态面板。
```

Add these entries under “代码结构”:

```markdown
- `src/CliParser.*`、`src/SerialCli.*`：固定缓冲命令解析、串口控制命令和实时状态面板。
```

- [ ] **Step 3: Add exact serial usage documentation**

Insert the following section after the installation steps and before the partition note:

````markdown
### 串口 CLI

固件和 PlatformIO 串口监视器统一使用 115200 baud：

```bash
pio device monitor
```

输入 `help` 查看命令：

```text
status
watch
scan
devices
connect <index>
disconnect
autoconnect <on|off>
output <start|stop>
wave <a|b|c>
strength <a|b> <add|sub|set> <value>
logs
help
```

手动选择设备时，先关闭自动连接：

```text
$ autoconnect off
$ scan
$ devices
$ connect 1
```

`watch` 使用 ANSI 转义每 500 ms 原地刷新；输入 `q` 后回车退出。退出监视不会停止设备输出。CLI 自身输出为英文，`logs` 显示的现有固件日志可能包含中文。
````

- [ ] **Step 4: Extend the README verification commands**

Replace the test command block with:

```bash
python test/web_ui_contract_test.py
python test/serial_cli_contract_test.py
pio test -e native
pio run -e esp32dev
```

Replace the paragraph after the test commands with:

```markdown
SPA 和串口 CLI 不增加运行时依赖，V2/V3 BLE 协议和输出行为保持不变。GitHub Actions 会依次运行 WebUi 契约、串口 CLI 契约、Native 测试和 ESP32 固件构建。本机运行 Native 测试需要系统中可用的 `gcc` / `g++`。
```

- [ ] **Step 5: Run documentation-sensitive checks**

Run:

```powershell
python test/web_ui_contract_test.py
python test/serial_cli_contract_test.py
git diff --check
```

Expected: both Python suites PASS and `git diff --check` prints no errors.

- [ ] **Step 6: Commit CI and documentation**

```powershell
git add .github/workflows/platformio.yml README.md
git commit -m "docs: document serial CLI workflow"
```

---

### Task 4: Full verification and hardware acceptance

**Files:**
- Verify only; modify implementation files only if a failing check exposes a defect, then repeat that task's red/green cycle and commit the focused fix.

**Interfaces:**
- Consumes: All implementation and documentation from Tasks 1-3.
- Produces: Evidence that automated checks pass, resource usage remains acceptable, and V2/V3 hardware behavior matches the approved design.

- [ ] **Step 1: Run the complete automated verification sequence**

Run:

```powershell
python test/web_ui_contract_test.py
python test/serial_cli_contract_test.py
pio test -e native
pio run -e esp32dev
git diff --check
```

Expected: both Python suites PASS, all native suites PASS, ESP32 firmware build SUCCESS, and no whitespace errors.

- [ ] **Step 2: Record firmware resource use**

From the final `pio run -e esp32dev` output, record RAM bytes/percentage and Flash bytes/percentage in the execution report. Compare with the pre-task build if available; investigate any unexpectedly large increase before claiming completion.

- [ ] **Step 3: Verify the terminal shell on hardware**

Upload and monitor:

```powershell
pio run -e esp32dev --target upload
pio device monitor
```

At 115200 baud verify:

```text
help
status
autoconnect off
scan
devices
connect 1
status
```

Expected: prompt echo and backspace work; CR, LF, and CRLF each submit once; the scan list is 1-based; `status` is English and matches the connected device.

- [ ] **Step 4: Verify V3 control and watch behavior**

With a V3 device connected, run:

```text
strength a add 5
strength b set 50
wave b
output start
watch
```

Expected: V3 shows `/ 200`, strength reports prepared/queued as applicable, the 20-cell bars refresh in place every 500 ms, Web UI actions appear on the next refresh, runtime logs do not break the panel, and `q` plus Enter returns to `$ ` without stopping output.

- [ ] **Step 5: Verify V2 scale and shared control behavior**

With a V2 device connected, run `status`, `watch`, and representative `strength` commands.

Expected: status shows `human / 292 (raw value / 2047)`, watch scales bars against 292, Web UI and CLI can alternate actions, and V2 notification feedback remains correct.

- [ ] **Step 6: Verify disconnect and recovery semantics**

During `watch`, cause an unexpected BLE disconnect and allow auto-reconnect; then separately use the `disconnect` command.

Expected: unexpected disconnect displays `disconnected` and retains the existing same-device output recovery policy; manual disconnect stops output intent and does not restore it. `watch` remains active through both state changes.

- [ ] **Step 7: Verify logs retained during watch**

Generate connection and strength events while watching, exit with `q`, then run:

```text
logs
```

Expected: up to ten newest stored logs appear in the existing source language, including events produced while immediate serial mirroring was disabled.

- [ ] **Step 8: Inspect final repository state**

Run:

```powershell
git status --short
git log -5 --oneline
```

Expected: no uncommitted implementation changes; commits for parser, CLI, and documentation are present. Do not claim V2/V3 hardware acceptance if either physical device was unavailable—report that part explicitly as pending.
