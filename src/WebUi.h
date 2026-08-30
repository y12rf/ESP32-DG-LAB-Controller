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
