#include "WebUi.h"

WebUi::WebUi(AppState& state, AppLog& log, BleManager& ble,
             OutputController& output)
    : state_(state), log_(log), ble_(ble), output_(output) {}

String WebUi::makeHtml() {
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
  if (state_.deviceConnected) {
    html += "<p>已连接: " + state_.connectedDeviceName + " (" + String(deviceTypeLabel(state_.deviceType)) + ")</p>";
    if (state_.deviceType == DeviceType::DG3) {
      html += "<p>通道A强度: " + String(state_.strengthA) + "/200" + (state_.strengthConfirmed ? "" : "（未确认）") + "</p>";
      html += "<p>通道B强度: " + String(state_.strengthB) + "/200" + (state_.strengthConfirmed ? "" : "（未确认）") + "</p>";
    } else {
      html += "<p>通道A强度: " + String(state_.strengthA / 7) + " (原:" + String(state_.strengthA) + ")</p>";
      html += "<p>通道B强度: " + String(state_.strengthB / 7) + " (原:" + String(state_.strengthB) + ")</p>";
    }
    html += "<a class='btn btn-danger' href='/disconnect'>断开连接</a>";
  } else {
    html += "<p>未连接</p><!-- <a class='btn' href='/scan'>扫描设备</a> -->";
  }
  html += "<p>自动连接: " + String(state_.autoConnectEnabled ? "已开启" : "已关闭") + "</p>";
  html += state_.autoConnectEnabled
              ? "<a class='btn btn-sm btn-danger' href='/auto-connect?enabled=0'>关闭自动连接</a>"
              : "<a class='btn btn-sm btn-success' href='/auto-connect?enabled=1'>开启自动连接</a>";
  html += "</div></div>";

  /* -------- 控制面板 -------- */
  if (state_.deviceConnected) {
    /* --- 强度控制 --- */
    html += "<div class='panel'><h2>通道强度控制</h2>";

    auto strengthBlock = [&](char ch, int strength) -> String {
      String s = "";
      s += "<div class='strength-control'><h3>通道";
      s += (ch == 'a' ? 'A' : 'B');
      s += "</h3><div class='strength-display'>";
      s += String(state_.deviceType == DeviceType::DG2 ? strength / 7 : strength);
      if (state_.deviceType == DeviceType::DG3 && !state_.strengthConfirmed) s += "（未确认）";
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
      if (state_.deviceType == DeviceType::DG3) {
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

    html += strengthBlock('a', state_.strengthA);
    html += strengthBlock('b', state_.strengthB);
    html += "</div>";  // panel 结束

    /* --- 波形控制 --- */
    html += "<div class='panel'><h2>波形控制</h2><form action='/wave'>";
    html += "<select name='type'>";
    html += "<option value='a'";
    if (state_.selectedWave == 'a') html += " selected";
    html += ">波形A</option>";
    html += "<option value='b'";
    if (state_.selectedWave == 'b') html += " selected";
    html += ">波形B</option>";
    html += "<option value='c'";
    if (state_.selectedWave == 'c') html += " selected";
    html += ">波形C</option>";
    html += "</select><div class='center'><input class='btn' type='submit' value='切换波形'></div></form><div class='center'>";
    if (state_.isSending)
      html += "<a class='btn btn-danger' href='/stop'>停止发送</a>";
    else
      html += "<a class='btn btn-success' href='/start'>开始发送</a>";
    html += "</div></div>";
  }

  /* -------- 设备列表 -------- */
  if (!state_.deviceConnected && !ble_.scannedDevices().empty()) {
    html += "<div class='panel'><h2>可用设备</h2>";
    for (auto& d : ble_.scannedDevices()) {
      html += "<a class='device-item' href='/connect?address=";
      html += d.address + "&type=" + String(deviceTypeToInt(d.type)) + "'>";
      html += d.name + " (" + String(deviceTypeLabel(d.type)) + ", RSSI=" + String(d.rssi) + "dBm)</a>";
    }
    html += "</div>";
  }

  /* -------- 日志 -------- */
  html += "<div class='panel'><h2>操作日志</h2>";
  for (size_t i = 0; i < log_.capacity(); ++i) {
    if (log_.newest(i).length()) html += "<div class='log-item'>" + log_.newest(i) + "</div>";
  }
  html += "</div></div></body></html>";
  return html;
}

void WebUi::redirectHome() {
  server_.sendHeader("Location", "/");
  server_.send(302, "text/plain", "");
}

void WebUi::begin() {
  server_.on("/", HTTP_GET, [this]() {
    String page = makeHtml();
    server_.send(200, "text/html", page);
  });

  /* ---- 扫描设备 ---- */
  server_.on("/scan", HTTP_GET, [this]() {
    if (ble_.startBleScan()) output_.onConnected(false);
    redirectHome();
  });

  server_.on("/auto-connect", HTTP_GET, [this]() {
    if (server_.hasArg("enabled")) {
      state_.autoConnectEnabled = server_.arg("enabled").toInt() != 0;
      log_.add(String("自动连接功能") + (state_.autoConnectEnabled ? "已开启" : "已关闭"));
    }
    redirectHome();
  });

  /* ---- 连接设备 ---- */
  server_.on("/connect", HTTP_GET, [this]() {
    if (server_.hasArg("address") && server_.hasArg("type")) {
      DeviceType type = parseDeviceType(server_.arg("type").toInt());
      if (type == DeviceType::None) {
        log_.add("连接失败: 未知设备类型");
      } else {
        const String address = server_.arg("address");
        const ScannedDevice* selected = nullptr;
        for (auto& d : ble_.scannedDevices()) {
          if (d.address == address && d.type == type) { selected = &d; break; }
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
    redirectHome();
  });

  /* ---- 断开连接 ---- */
  server_.on("/disconnect", HTTP_GET, [this]() {
    ble_.disconnectDevice();
    redirectHome();
  });

  /* ---- 开始 / 停止波形发送 ---- */
  server_.on("/start", HTTP_GET, [this]() {
    output_.startSending();
    redirectHome();
  });

  server_.on("/stop", HTTP_GET, [this]() {
    output_.stopSending();
    redirectHome();
  });

  /* ---- 切换波形 ---- */
  server_.on("/wave", HTTP_GET, [this]() {
    if (server_.hasArg("type")) {
      output_.selectWave(server_.arg("type").charAt(0));
    }
    redirectHome();
  });

  /* ---- 强度调整 ---- */
  server_.on("/strength", HTTP_GET, [this]() {
    if (state_.deviceConnected && server_.hasArg("channel") && server_.hasArg("value") && server_.hasArg("method")) {

      char ch = server_.arg("channel").charAt(0);
      const String rawValue = server_.arg("value");
      const int val = rawValue.toInt();
      const int methodValue = server_.arg("method").toInt();
      if ((ch != 'a' && ch != 'b') || rawValue.startsWith("-") || val < 0 ||
          (state_.deviceType == DeviceType::DG3 &&
           (methodValue == 4 || methodValue == 8 || methodValue == 1 || methodValue == 2) && val > 200)) {
        log_.add("强度参数无效");
        redirectHome();
        return;
      }
      uint8_t m = static_cast<uint8_t>(methodValue);

      const dglab::RequestDisposition disposition =
          output_.adjustStrength(ch, val, m);
      if (disposition == dglab::RequestDisposition::Rejected) {
        log_.add("强度请求未发送");
      } else if (state_.deviceType == DeviceType::DG2) {
        log_.add(String("调整") + (ch == 'a' ? "A" : "B") + "强度: " +
                 String(val) + ", 方法:" + String(m));
      } else if (disposition == dglab::RequestDisposition::Queued) {
        log_.add("强度命令已排队");
      } else if (disposition == dglab::RequestDisposition::Prepared) {
        log_.add("强度命令待发送");
      }
    }
    redirectHome();
  });

  server_.begin();
  log_.add("HTTP 服务器已启动");
}
