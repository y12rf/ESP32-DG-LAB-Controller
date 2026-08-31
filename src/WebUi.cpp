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
  String payload;
  payload.reserve(320);
  payload = F("{\"connected\":");
  payload += state_.deviceConnected ? F("true") : F("false");
  payload += F(",\"ready\":");
  payload += state_.linkReady.load(std::memory_order_acquire) ? F("true") : F("false");
  payload += F(",\"type\":");
  payload += apiDeviceType(state_.deviceType);
  payload += F(",\"name\":");
  appendJsonString(payload, state_.deviceConnected ? state_.connectedDeviceName : String());
  payload += F(",\"strengthA\":"); payload += state_.deviceConnected ? state_.strengthA : 0;
  payload += F(",\"strengthB\":"); payload += state_.deviceConnected ? state_.strengthB : 0;
  payload += F(",\"confirmed\":");
  payload += state_.deviceConnected && state_.strengthConfirmed ? F("true") : F("false");
  payload += F(",\"waiting\":");
  payload += state_.deviceConnected && state_.waitingForResponse ? F("true") : F("false");
  payload += F(",\"wave\":\""); payload += state_.selectedWave; payload += '"';
  payload += F(",\"sending\":");
  payload += state_.deviceConnected && state_.isSending ? F("true") : F("false");
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

  server_.on("/scan", HTTP_GET, [this]() {
    if (ble_.startBleScan()) output_.onConnected(false);
    sendIndex();
  });

  server_.on("/auto-connect", HTTP_GET, [this]() {
    if (server_.hasArg("enabled")) {
      state_.autoConnectEnabled = server_.arg("enabled").toInt() != 0;
      log_.add(String("自动连接功能") +
               (state_.autoConnectEnabled ? "已开启" : "已关闭"));
    }
    sendIndex();
  });

  server_.on("/connect", HTTP_GET, [this]() {
    if (server_.hasArg("address") && server_.hasArg("type")) {
      const DeviceType type = parseDeviceType(server_.arg("type").toInt());
      if (type == DeviceType::None) {
        log_.add("连接失败: 未知设备类型");
      } else {
        const String address = server_.arg("address");
        const ScannedDevice* selected = nullptr;
        for (const auto& device : ble_.scannedDevices()) {
          if (device.address == address && device.type == type) {
            selected = &device;
            break;
          }
        }
        if (!selected) {
          log_.add("连接失败: 类型无效或连接错误");
        } else {
          output_.onManualConnectionAttempt();
          if (!ble_.connectToDevice(address, type, selected->identity, true)) {
            log_.add("连接失败: 类型无效或连接错误");
          } else {
            output_.onConnected(true);
          }
        }
      }
    }
    sendIndex();
  });

  server_.on("/disconnect", HTTP_GET, [this]() {
    ble_.disconnectDevice();
    sendIndex();
  });

  server_.on("/start", HTTP_GET, [this]() {
    output_.startSending();
    sendIndex();
  });

  server_.on("/stop", HTTP_GET, [this]() {
    output_.stopSending();
    sendIndex();
  });

  server_.on("/wave", HTTP_GET, [this]() {
    if (server_.hasArg("type")) output_.selectWave(server_.arg("type").charAt(0));
    sendIndex();
  });

  server_.on("/strength", HTTP_GET, [this]() {
    if (state_.deviceConnected && server_.hasArg("channel") &&
        server_.hasArg("value") && server_.hasArg("method")) {
      const char channel = server_.arg("channel").charAt(0);
      const String rawValue = server_.arg("value");
      const int value = rawValue.toInt();
      const int methodValue = server_.arg("method").toInt();
      if ((channel != 'a' && channel != 'b') || rawValue.startsWith("-") ||
          value < 0 ||
          (state_.deviceType == DeviceType::DG3 &&
           (methodValue == 4 || methodValue == 8 || methodValue == 1 ||
            methodValue == 2) &&
           value > 200)) {
        log_.add("强度参数无效");
        sendIndex();
        return;
      }
      const uint8_t method = static_cast<uint8_t>(methodValue);
      const dglab::RequestDisposition disposition =
          output_.adjustStrength(channel, value, method);
      if (disposition == dglab::RequestDisposition::Rejected) {
        log_.add("强度请求未发送");
      } else if (state_.deviceType == DeviceType::DG2) {
        log_.add(String("调整") + (channel == 'a' ? "A" : "B") +
                 "强度: " + String(value) + ", 方法:" + String(method));
      } else if (disposition == dglab::RequestDisposition::Queued) {
        log_.add("强度命令已排队");
      } else if (disposition == dglab::RequestDisposition::Prepared) {
        log_.add("强度命令待发送");
      }
    }
    sendIndex();
  });

  server_.begin();
  log_.add("HTTP 服务器已启动");
}
