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

void processBleEvents() {
  BleEvent event{};
  while (bleManager.pollEvent(event)) {
    if (event.type == BleEventType::StrengthResponse) {
      outputController.onStrengthResponse(event);
    } else {
      bleManager.handleDisconnectEvent();
    }
  }
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
  processBleEvents();
  bool manualDisconnect = false;
  if (bleManager.handleDisconnectedClient(manualDisconnect)) {
    outputController.onDisconnected(manualDisconnect);
  }

  webUi.handleClient();
  outputController.handleWaveSend();
  if (bleManager.handleAutoScan()) outputController.onConnected(false);
  delay(10);
}
