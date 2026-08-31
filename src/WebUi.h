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

  void sendIndex();
  void sendStatus();
  void sendDevices();
  void sendLogs();
  void sendJson(int code, const String& payload);
  void sendOk(const char* extra = nullptr);
  void sendError(int code, const char* error);
  static void appendJsonString(String& output, const String& value);
  static int apiDeviceType(DeviceType type);
  static bool parseNonNegativeInt(const String& raw, int& value);
};
