#include <Arduino.h>

#include <WiFi.h>
#include <WebServer.h>
#include <BLEDevice.h>
#include <stdint.h>
#include "AppState.h"
#include "AppLog.h"
#include "BleManager.h"
#include "OutputController.h"
#include "Waveforms.h"

//WiFi 配置
const char* ssid = "ESP32-Controller";
const char* password = "12345678";  // ≥ 8 字符
WebServer server(80);

AppState appState;
AppLog appLog;
BleManager bleManager(appState, appLog);
OutputController outputController(appState, appLog, bleManager);

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

/* ========== WEBUI ========== */
String makeHTML() {
  String html = F(R"rawliteral(
<!DOCTYPE html>
<html><head>
  <meta charset='UTF-8'>
  <meta name='viewport' content='width=device-width,initial-scale=1'>
  <meta http-equiv='refresh' content='5'>
  <title>DG-LAB 控制器</title>
  <style>
    body{font-family:Arial;background:#121212;color:#ffe99d;margin:0;}
    .container{max-width:800px;padding:20px;margin:auto;}
    h1,h2{text-align:center;}
    .panel{background:#1e1e1e;padding:15px;border-radius:8px;margin-bottom:20px;}
    .btn{display:inline-block;padding:10px 20px;margin:5px;border-radius:4px;text-decoration:none;font-weight:bold;}
    .btn-success{background:#4caf50;color:#fff}
    .btn-danger{background:#f44336;color:#fff}
    .btn-primary{background:#2196F3;color:#fff}
    .btn-sm{padding:5px 10px;font-size:80%;}
    .device-item{display:block;background:#2d2d2d;padding:10px;border-radius:4px;margin-top:10px;color:#ffe99d;text-decoration:none;}
    select{width:100%;padding:10px;background:#2d2d2d;color:#ffe99d;border:1px solid #ffe99d;border-radius:4px;margin-top:10px;}
    .log-item{border-bottom:1px solid #333;margin-bottom:5px;}
    .strength-control{margin:15px 0;text-align:center;}
    .strength-display{font-size:24px;margin:10px 0;}
    .ctrl-btn{margin:5px;width:40px;height:40px;font-size:20px;}
  </style>
</head><body><div class='container'>
<h1>DG-LAB 设备控制器</h1>
)rawliteral");

  /* -------- 设备状态 -------- */
  html += "<div class='panel'><h2>设备状态</h2><div class='center'>";
  if (appState.deviceConnected) {
    html += "<p>已连接: " + appState.connectedDeviceName + " (" + String(deviceTypeLabel(appState.deviceType)) + ")</p>";
    if (appState.deviceType == DeviceType::DG3) {
      html += "<p>通道A强度: " + String(appState.strengthA) + "/200" + (appState.strengthConfirmed ? "" : "（未确认）") + "</p>";
      html += "<p>通道B强度: " + String(appState.strengthB) + "/200" + (appState.strengthConfirmed ? "" : "（未确认）") + "</p>";
    } else {
      html += "<p>通道A强度: " + String(appState.strengthA / 7) + " (原:" + String(appState.strengthA) + ")</p>";
      html += "<p>通道B强度: " + String(appState.strengthB / 7) + " (原:" + String(appState.strengthB) + ")</p>";
    }
    html += "<a class='btn btn-danger' href='/disconnect'>断开连接</a>";
  } else {
    html += "<p>未连接</p><!-- <a class='btn' href='/scan'>扫描设备</a> -->";
  }
  html += "<p>自动连接: " + String(appState.autoConnectEnabled ? "已开启" : "已关闭") + "</p>";
  html += appState.autoConnectEnabled
              ? "<a class='btn btn-sm btn-danger' href='/auto-connect?enabled=0'>关闭自动连接</a>"
              : "<a class='btn btn-sm btn-success' href='/auto-connect?enabled=1'>开启自动连接</a>";
  html += "</div></div>";

  /* -------- 控制面板 -------- */
  if (appState.deviceConnected) {
    /* --- 强度控制 --- */
    html += "<div class='panel'><h2>通道强度控制</h2>";

    auto strengthBlock = [&](char ch, int strength) -> String {
      String s = "";
      s += "<div class='strength-control'><h3>通道";
      s += (ch == 'a' ? 'A' : 'B');
      s += "</h3><div class='strength-display'>";
      s += String(appState.deviceType == DeviceType::DG2 ? strength / 7 : strength);
      if (appState.deviceType == DeviceType::DG3 && !appState.strengthConfirmed) s += "（未确认）";
      s += "</div><div>";
      bool isA = (ch == 'a');
      s += "<a class='btn btn-primary ctrl-btn' href='/strength?channel=";
      s += ch;
      s += "&value=1&method=";
      s += (isA ? 4 : 1);
      s += "'>+1</a>";
      s += "<a class='btn btn-primary ctrl-btn' href='/strength?channel=";
      s += ch;
      s += "&value=5&method=";
      s += (isA ? 4 : 1);
      s += "'>+5</a>";
      s += "<a class='btn btn-primary ctrl-btn' href='/strength?channel=";
      s += ch;
      s += "&value=10&method=";
      s += (isA ? 4 : 1);
      s += "'>+10</a>";
      s += "</div><div>";
      s += "<a class='btn btn-primary ctrl-btn' href='/strength?channel=";
      s += ch;
      s += "&value=1&method=";
      s += (isA ? 8 : 2);
      s += "'>-1</a>";
      s += "<a class='btn btn-primary ctrl-btn' href='/strength?channel=";
      s += ch;
      s += "&value=5&method=";
      s += (isA ? 8 : 2);
      s += "'>-5</a>";
      s += "<a class='btn btn-primary ctrl-btn' href='/strength?channel=";
      s += ch;
      s += "&value=10&method=";
      s += (isA ? 8 : 2);
      s += "'>-10</a>";
      s += "</div><div style='margin-top:10px;'>";
      s += "<a class='btn btn-sm btn-danger' href='/strength?channel=";
      s += ch;
      s += "&value=0&method=";
      s += (isA ? 12 : 3);
      s += "'>归零</a>";
      if (appState.deviceType == DeviceType::DG3) {
        s += "<a class='btn btn-sm btn-success' href='/strength?channel=";
        s += ch;
        s += "&value=100&method=";
        s += (isA ? 12 : 3);
        s += "'>50%</a>";
        //         s += "<a class='btn btn-sm btn-success' href='/strength?channel=";
        //         s += ch;
        //         s += "&value=200&method=";
        //         s += (isA ? 12 : 3);
        //         s += "'>最大</a>"; //我不可能告诉你任何事情~啊
      } else {
        int half = 146, max = 292;
        s += "<a class='btn btn-sm btn-success' href='/strength?channel=";
        s += ch;
        s += "&value=";
        s += half;
        s += "&method=";
        s += (isA ? 12 : 3);
        s += "'>50%</a>";
        //         s += "<a class='btn btn-sm btn-success' href='/strength?channel=";
        //         s += ch;
        //         s += "&value=";
        //         s += max;
        //         s += "&method=";
        //         s += (isA ? 12 : 3);
        //         s += "'>最大</a>";
      }
      s += "</div></div>";
      return s;
    };

    html += strengthBlock('a', appState.strengthA);
    html += strengthBlock('b', appState.strengthB);
    html += "</div>";  // panel 结束

    /* --- 波形控制 --- */
    html += "<div class='panel'><h2>波形控制</h2><form action='/wave'>";
    html += "<select name='type'>";
    html += "<option value='a'";
    if (appState.selectedWave == 'a') html += " selected";
    html += ">波形A</option>";
    html += "<option value='b'";
    if (appState.selectedWave == 'b') html += " selected";
    html += ">波形B</option>";
    html += "<option value='c'";
    if (appState.selectedWave == 'c') html += " selected";
    html += ">波形C</option>";
    html += "</select><div class='center'><input class='btn' type='submit' value='切换波形'></div></form><div class='center'>";
    if (appState.isSending)
      html += "<a class='btn btn-danger' href='/stop'>停止发送</a>";
    else
      html += "<a class='btn btn-success' href='/start'>开始发送</a>";
    html += "</div></div>";
  }

  /* -------- 设备列表 -------- */
  if (!appState.deviceConnected && !bleManager.scannedDevices().empty()) {
    html += "<div class='panel'><h2>可用设备</h2>";
    for (auto& d : bleManager.scannedDevices()) {
      html += "<a class='device-item' href='/connect?address=";
      html += d.address + "&type=" + String(deviceTypeToInt(d.type)) + "'>";
      html += d.name + " (" + String(deviceTypeLabel(d.type)) + ", RSSI=" + String(d.rssi) + "dBm)</a>";
    }
    html += "</div>";
  }

  /* -------- 日志 -------- */
  html += "<div class='panel'><h2>操作日志</h2>";
  for (size_t i = 0; i < appLog.capacity(); ++i) {
    if (appLog.newest(i).length()) html += "<div class='log-item'>" + appLog.newest(i) + "</div>";
  }
  html += "</div></div></body></html>";
  return html;
}

/* ========== WEB 路由 ========== */
void setupWeb() {

  server.on("/", HTTP_GET, []() {
    String page = makeHTML();
    server.send(200, "text/html", page);
  });

  /* ---- 扫描设备 ---- */
  server.on("/scan", HTTP_GET, []() {
    if (bleManager.startBleScan()) outputController.onConnected(false);
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
  });

  server.on("/auto-connect", HTTP_GET, []() {
    if (server.hasArg("enabled")) {
      appState.autoConnectEnabled = server.arg("enabled").toInt() != 0;
      appLog.add(String("自动连接功能") + (appState.autoConnectEnabled ? "已开启" : "已关闭"));
    }
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
  });

  /* ---- 连接设备 ---- */
  server.on("/connect", HTTP_GET, []() {
    if (server.hasArg("address") && server.hasArg("type")) {
      DeviceType type = parseDeviceType(server.arg("type").toInt());
      if (type == DeviceType::None) {
        appLog.add("连接失败: 未知设备类型");
      } else {
        const String address = server.arg("address");
        const ScannedDevice* selected = nullptr;
        for (auto& d : bleManager.scannedDevices()) {
          if (d.address == address && d.type == type) { selected = &d; break; }
        }
        if (!selected || !bleManager.connectToDevice(address, type, &selected->identity, true)) {
          appLog.add("连接失败: 类型无效或连接错误");
        } else {
          outputController.onConnected(true);
        }
      }
    }
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
  });

  /* ---- 断开连接 ---- */
  server.on("/disconnect", HTTP_GET, []() {
    bleManager.disconnectDevice();
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
  });

  /* ---- 开始 / 停止波形发送 ---- */
  server.on("/start", HTTP_GET, []() {
    outputController.startSending();
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
  });

  server.on("/stop", HTTP_GET, []() {
    outputController.stopSending();
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
  });

  /* ---- 切换波形 ---- */
  server.on("/wave", HTTP_GET, []() {
    if (server.hasArg("type")) {
      outputController.selectWave(server.arg("type").charAt(0));
    }
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
  });

  /* ---- 强度调整 ---- */
  server.on("/strength", HTTP_GET, []() {
    if (appState.deviceConnected && server.hasArg("channel") && server.hasArg("value") && server.hasArg("method")) {

      char ch = server.arg("channel").charAt(0);
      const String rawValue = server.arg("value");
      const int val = rawValue.toInt();
      const int methodValue = server.arg("method").toInt();
      if ((ch != 'a' && ch != 'b') || rawValue.startsWith("-") || val < 0 ||
          (appState.deviceType == DeviceType::DG3 &&
           (methodValue == 4 || methodValue == 8 || methodValue == 1 || methodValue == 2) && val > 200)) {
        appLog.add("强度参数无效");
        server.sendHeader("Location", "/");
        server.send(302, "text/plain", "");
        return;
      }
      uint8_t m = static_cast<uint8_t>(methodValue);

      bool ok = outputController.adjustStrength(ch, val, m);

      if (ok)
        appLog.add(String("调整") + (ch == 'a' ? "A" : "B") + "强度: " + String(val) + ", 方法:" + String(m));
      else
        appLog.add("强度请求未发送");
    }
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
  });

  server.begin();
  appLog.add("HTTP 服务器已启动");
}


/* ========== SETUP / LOOP ========== */
void setup() {
  Serial.begin(19200);
  Serial.println("DG-LAB 控制器启动");
  // 隐藏热点 SSID 以避免被直接发现
  WiFi.softAP(ssid, password, 1, 1);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
  BLEDevice::init("ESP32_DGLAB_Client");
  bleManager.begin();
  setupWeb();
  appLog.add("系统初始化完成");
  appState.lastScanFinished = millis();
}

void loop() {
  server.handleClient();
  processBleEvents();
  bool manualDisconnect = false;
  if (bleManager.handleDisconnectedClient(manualDisconnect)) {
    outputController.onDisconnected(manualDisconnect);
  }
  outputController.drainStrengthCommand();
  outputController.handleWaveSend();
  if (bleManager.handleAutoScan()) outputController.onConnected(false);
  delay(10);
}
