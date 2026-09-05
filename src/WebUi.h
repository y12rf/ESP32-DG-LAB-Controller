#pragma once

#include "AppLog.h"
#include "AppState.h"
#include "BleManager.h"
#include "OutputController.h"
#include <WebServer.h>
#include <freertos/queue.h>
#include <freertos/task.h>

class WebUi {
 public:
  WebUi(AppState& state, AppLog& log, BleManager& ble,
        OutputController& output);
  bool begin();
  void processRequest();

 private:
  AppState& state_;
  AppLog& log_;
  BleManager& ble_;
  OutputController& output_;
  WebServer server_{80};
  QueueHandle_t requests_ = nullptr;
  TaskHandle_t httpTask_ = nullptr;
  int responseCode_ = 200;
  String responsePayload_;

  void onJson(const char* uri, HTTPMethod method,
              WebServer::THandlerFunction handler);
  static void runHttp(void* context);

  void sendIndex();
  void sendStatus();
  void sendDevices();
  void sendLogs();
  void sendJson(int code, const String& payload);
  void sendOk(const char* extra = nullptr);
  void sendError(int code, const char* error);
  static void appendJsonString(String& output, const String& value);
  static int apiDeviceType(DeviceType type);
  static DeviceType parseApiDeviceType(int value);
  static bool parseNonNegativeInt(const String& raw, int& value);
};
