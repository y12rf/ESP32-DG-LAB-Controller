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
  size_t processed = 0;
  while (processed < kMaxInputBytesPerLoop && Serial.available() > 0) {
    const char ch = static_cast<char>(Serial.read());
    if (watching_) processWatchByte(ch);
    else processNormalByte(ch);
    ++processed;
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
