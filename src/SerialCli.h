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
  static constexpr size_t kMaxInputBytesPerLoop = 8;
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
