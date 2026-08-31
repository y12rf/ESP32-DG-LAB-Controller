#include "WebUi.h"
#include "WebAssets.h"

#include <stdio.h>

WebUi::WebUi(AppState& state, AppLog& log, BleManager& ble,
             OutputController& output)
    : state_(state), log_(log), ble_(ble), output_(output) {}

void WebUi::sendJson(int code, const String& payload) {
  server_.sendHeader("Cache-Control", "no-store");
  server_.send(code, "application/json; charset=utf-8", payload);
}

void WebUi::sendOk(const char* extra) {
  String payload;
  payload.reserve(extra ? 64 : 16);
  payload = F("{\"ok\":true");
  if (extra) payload += extra;
  payload += '}';
  sendJson(200, payload);
}

void WebUi::sendError(int code, const char* error) {
  String payload;
  payload.reserve(48);
  payload = F("{\"ok\":false,\"error\":\"");
  payload += error;
  payload += F("\"}");
  sendJson(code, payload);
}

void WebUi::appendJsonString(String& output, const String& value) {
  output += '"';
  for (size_t i = 0; i < value.length(); ++i) {
    const uint8_t ch = static_cast<uint8_t>(value[i]);
    switch (ch) {
      case '"': output += F("\\\""); break;
      case '\\': output += F("\\\\"); break;
      case '\b': output += F("\\b"); break;
      case '\f': output += F("\\f"); break;
      case '\n': output += F("\\n"); break;
      case '\r': output += F("\\r"); break;
      case '\t': output += F("\\t"); break;
      default:
        if (ch < 0x20) {
          char escaped[7];
          snprintf(escaped, sizeof(escaped), "\\u%04x", ch);
          output += escaped;
        } else {
          output += static_cast<char>(ch);
        }
    }
  }
  output += '"';
}

int WebUi::apiDeviceType(DeviceType type) {
  if (type == DeviceType::DG2) return 2;
  if (type == DeviceType::DG3) return 3;
  return 0;
}

DeviceType WebUi::parseApiDeviceType(int value) {
  if (value == 2) return DeviceType::DG2;
  if (value == 3) return DeviceType::DG3;
  return DeviceType::None;
}

bool WebUi::parseNonNegativeInt(const String& raw, int& value) {
  if (raw.length() == 0) return false;
  unsigned long parsed = 0;
  for (size_t i = 0; i < raw.length(); ++i) {
    if (raw[i] < '0' || raw[i] > '9') return false;
    const unsigned long digit = static_cast<unsigned long>(raw[i] - '0');
    if (parsed > (2147483647UL - digit) / 10UL) return false;
    parsed = parsed * 10UL + digit;
  }
  value = static_cast<int>(parsed);
  return true;
}

void WebUi::sendIndex() {
  server_.sendHeader("Cache-Control", "no-cache");
  server_.send_P(200, "text/html; charset=utf-8", web_assets::kIndexHtml);
}

void WebUi::sendStatus() {
  const bool connected =
      state_.deviceConnected.load(std::memory_order_acquire);
  const bool ready =
      connected && state_.linkReady.load(std::memory_order_acquire);
  String payload;
  payload.reserve(320);
  payload = F("{\"connected\":");
  payload += connected ? F("true") : F("false");
  payload += F(",\"ready\":");
  payload += ready ? F("true") : F("false");
  payload += F(",\"type\":");
  payload += apiDeviceType(connected ? state_.deviceType : DeviceType::None);
  payload += F(",\"name\":");
  appendJsonString(payload, connected ? state_.connectedDeviceName : String());
  payload += F(",\"strengthA\":"); payload += connected ? state_.strengthA : 0;
  payload += F(",\"strengthB\":"); payload += connected ? state_.strengthB : 0;
  payload += F(",\"confirmed\":");
  payload += connected && state_.strengthConfirmed ? F("true") : F("false");
  payload += F(",\"waiting\":");
  payload += connected && state_.waitingForResponse ? F("true") : F("false");
  payload += F(",\"wave\":\""); payload += state_.selectedWave; payload += '"';
  payload += F(",\"sending\":");
  payload += connected && state_.isSending ? F("true") : F("false");
  payload += F(",\"autoConnect\":");
  payload += state_.autoConnectEnabled ? F("true") : F("false");
  payload += F(",\"scanRevision\":"); payload += state_.lastScanFinished;
  payload += '}';
  sendJson(200, payload);
}

void WebUi::sendDevices() {
  const auto& devices = ble_.scannedDevices();
  String payload;
  payload.reserve(32 + devices.size() * 112);
  payload = F("{\"devices\":[");
  for (size_t i = 0; i < devices.size(); ++i) {
    if (i) payload += ',';
    payload += F("{\"name\":"); appendJsonString(payload, devices[i].name);
    payload += F(",\"address\":"); appendJsonString(payload, devices[i].address);
    payload += F(",\"type\":"); payload += apiDeviceType(devices[i].type);
    payload += F(",\"rssi\":"); payload += devices[i].rssi;
    payload += '}';
  }
  payload += F("]}");
  sendJson(200, payload);
}

void WebUi::sendLogs() {
  String payload;
  payload.reserve(640);
  payload = F("{\"logs\":[");
  bool first = true;
  for (size_t i = 0; i < log_.capacity(); ++i) {
    const String& entry = log_.newest(i);
    if (entry.length() == 0) continue;
    if (!first) payload += ',';
    first = false;
    appendJsonString(payload, entry);
  }
  payload += F("]}");
  sendJson(200, payload);
}

void WebUi::begin() {
  server_.on("/", HTTP_GET, [this]() { sendIndex(); });
  server_.on("/api/status", HTTP_GET, [this]() { sendStatus(); });
  server_.on("/api/devices", HTTP_GET, [this]() { sendDevices(); });
  server_.on("/api/logs", HTTP_GET, [this]() { sendLogs(); });

  server_.on("/api/scan", HTTP_POST, [this]() {
    if (state_.deviceConnected || state_.scanInProgress ||
        state_.clientCleanupPending) {
      sendError(409, "invalid_state");
      return;
    }
    if (ble_.startBleScan()) output_.onConnected(false);
    sendOk();
  });

  server_.on("/api/connect", HTTP_POST, [this]() {
    if (!server_.hasArg("address") || !server_.hasArg("type") ||
        state_.deviceConnected || state_.clientCleanupPending) {
      sendError(state_.deviceConnected || state_.clientCleanupPending ? 409 : 400,
                state_.deviceConnected || state_.clientCleanupPending
                    ? "invalid_state" : "invalid_argument");
      return;
    }
    int typeValue = 0;
    if (!parseNonNegativeInt(server_.arg("type"), typeValue)) {
      sendError(400, "invalid_argument");
      return;
    }
    const DeviceType type = parseApiDeviceType(typeValue);
    if (type == DeviceType::None) {
      sendError(400, "invalid_argument");
      return;
    }
    const String address = server_.arg("address");
    const ScannedDevice* selected = nullptr;
    for (const auto& device : ble_.scannedDevices()) {
      if (device.address == address && device.type == type) {
        selected = &device;
        break;
      }
    }
    if (!selected) {
      sendError(404, "device_not_found");
      return;
    }
    output_.onManualConnectionAttempt();
    if (!ble_.connectToDevice(address, type, selected->identity, true)) {
      sendError(503, "ble_failure");
      return;
    }
    output_.onConnected(true);
    sendOk();
  });

  server_.on("/api/disconnect", HTTP_POST, [this]() {
    if (!state_.deviceConnected) {
      sendError(409, "invalid_state");
      return;
    }
    ble_.disconnectDevice();
    sendOk();
  });

  server_.on("/api/auto-connect", HTTP_POST, [this]() {
    if (!server_.hasArg("enabled")) {
      sendError(400, "invalid_argument");
      return;
    }
    int enabled = 0;
    if (!parseNonNegativeInt(server_.arg("enabled"), enabled) || enabled > 1) {
      sendError(400, "invalid_argument");
      return;
    }
    state_.autoConnectEnabled = enabled == 1;
    log_.add(String("自动连接功能") +
             (state_.autoConnectEnabled ? "已开启" : "已关闭"));
    sendOk();
  });

  server_.on("/api/output", HTTP_POST, [this]() {
    if (!server_.hasArg("sending")) {
      sendError(400, "invalid_argument");
      return;
    }
    int sending = 0;
    if (!parseNonNegativeInt(server_.arg("sending"), sending) || sending > 1) {
      sendError(400, "invalid_argument");
      return;
    }
    if (sending && (!state_.deviceConnected ||
                    !state_.linkReady.load(std::memory_order_acquire))) {
      sendError(409, "invalid_state");
      return;
    }
    if (sending) output_.startSending();
    else output_.stopSending();
    sendOk();
  });

  server_.on("/api/wave", HTTP_POST, [this]() {
    if (!server_.hasArg("type") || server_.arg("type").length() != 1) {
      sendError(400, "invalid_argument");
      return;
    }
    const char wave = server_.arg("type")[0];
    if (wave != 'a' && wave != 'b' && wave != 'c') {
      sendError(400, "invalid_argument");
      return;
    }
    output_.selectWave(wave);
    sendOk();
  });

  server_.on("/api/strength", HTTP_POST, [this]() {
    if (!state_.deviceConnected ||
        !state_.linkReady.load(std::memory_order_acquire)) {
      sendError(409, "invalid_state");
      return;
    }
    if (!server_.hasArg("channel") || !server_.hasArg("value") ||
        !server_.hasArg("method") || server_.arg("channel").length() != 1) {
      sendError(400, "invalid_argument");
      return;
    }
    const char channel = server_.arg("channel")[0];
    int value = 0;
    int method = 0;
    if ((channel != 'a' && channel != 'b') ||
        !parseNonNegativeInt(server_.arg("value"), value) ||
        !parseNonNegativeInt(server_.arg("method"), method)) {
      sendError(400, "invalid_argument");
      return;
    }
    const bool methodValid =
        channel == 'a' ? (method == 4 || method == 8 || method == 12)
                       : (method == 1 || method == 2 || method == 3);
    const int maximum = state_.deviceType == DeviceType::DG3 ? 200 : 292;
    if (!methodValid || value > maximum) {
      sendError(400, "invalid_argument");
      return;
    }
    const dglab::RequestDisposition disposition =
        output_.adjustStrength(channel, value, static_cast<uint8_t>(method));
    if (disposition == dglab::RequestDisposition::Rejected) {
      sendError(503, "ble_failure");
      return;
    }
    sendOk(disposition == dglab::RequestDisposition::Queued
               ? ",\"disposition\":\"queued\""
               : ",\"disposition\":\"prepared\"");
  });

  server_.begin();
  log_.add("HTTP 服务器已启动");
}
