#include <WiFi.h>
#include <WebServer.h>
#include <BLEDevice.h>
#include <BLEClient.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <stdint.h>
#include <vector>

//WiFi 配置
const char* ssid = "ESP32-Controller";
const char* password = "12345678";  // ≥ 8 字符
WebServer server(80);

//DG-LAB 设备参数
#define SERVICE_UUID_2_0 "955a180b-0fe2-f5aa-a094-84b8d4f3e8ad"
#define CHARACTERISTIC_A_UUID_2_0 "955a1506-0fe2-f5aa-a094-84b8d4f3e8ad"
#define CHARACTERISTIC_B_UUID_2_0 "955a1505-0fe2-f5aa-a094-84b8d4f3e8ad"

#define SERVICE_UUID_3_0 "0000180c-0000-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_WRITE_3_0 "0000150a-0000-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_NOTIFY_3_0 "0000150b-0000-1000-8000-00805f9b34fb"

const char* devicePrefix_2_0 = "D-LAB";
const char* devicePrefix_3_0 = "47";

//设备类型
enum class DeviceType : uint8_t {
  None = 0,
  DG2 = 1,
  DG3 = 2,
};

DeviceType deviceType = DeviceType::None;

const char* deviceTypeLabel(DeviceType type) {
  switch (type) {
    case DeviceType::DG2: return "2.0版本";
    case DeviceType::DG3: return "3.0版本";
    default: return "未知版本";
  }
}

int deviceTypeToInt(DeviceType type) {
  switch (type) {
    case DeviceType::DG2: return 1;
    case DeviceType::DG3: return 2;
    default: return 0;
  }
}

DeviceType parseDeviceType(int value) {
  switch (value) {
    case 1: return DeviceType::DG2;
    case 2: return DeviceType::DG3;
    default: return DeviceType::None;
  }
}

//BLE 全局
BLEClient* pClient = nullptr;
BLERemoteCharacteristic* pCharacteristicA_2_0 = nullptr;
BLERemoteCharacteristic* pCharacteristicB_2_0 = nullptr;
BLERemoteCharacteristic* pCharacteristic_3_0_Write = nullptr;
BLERemoteCharacteristic* pCharacteristic_3_0_Notify = nullptr;

bool deviceConnected = false;
String connectedDeviceName = "";
String connectedDeviceAddress = "";
bool autoConnectEnabled = true;

// 自动扫描控制
const unsigned long autoScanIntervalMs = 10000;  // 扫描间隔 10 秒
unsigned long lastScanFinished = 0;
bool scanInProgress = false;

//通道强度控制
int strengthA = 0;                // A通道强度 (2.0: 0-2047, 3.0: 0-200)
int strengthB = 0;                // B通道强度
uint8_t orderNo = 0;              // 序列号 (0-15), 用于3.0设备
bool isInputAllowed = true;       // 是否允许输入新的强度调整
bool waitingForResponse = false;  // 是否正在等待 B1 回应

//波形数据
const char* wave_2_0_A[] = {
  "210100", "210102", "210104", "210106", "210108", "21010A", "21010A", "21010A",
  "000000", "000000", "000000", "000000"
};
const char* wave_2_0_B[] = {
  "C4080A", "24080A", "84070A", "03070A", "63060A", "E3050A", "43050A", "A3040A",
  "22040A", "82030A", "02030A", "21010A", "21010A", "21010A", "21010A", "21010A",
  "21010A", "21010A", "21010A"
};
const char* wave_2_0_C[] = {
  "210100", "618102", "A10105", "E18107", "21020A", "81020A", "C1020A", "010300",
  "410300", "A10300", "210100", "618102", "A10105", "E18107", "21020A", "81020A",
  "C1020A", "010300", "410300", "A10300"
};

const char* wave_3_0_A[] = {
  "0A0A0A0A00000000", "0A0A0A0A14141414", "0A0A0A0A28282828",
  "0A0A0A0A3C3C3C3C", "0A0A0A0A50505050", "0A0A0A0A64646464",
  "0A0A0A0A64646464", "0A0A0A0A64646464", "0A0A0A0A00000000",
  "0A0A0A0A00000000", "0A0A0A0A00000000", "0A0A0A0A00000000"
};
const char* wave_3_0_B[] = {
  "4A4A4A4A64646464", "4545454564646464", "4040404064646464", "3B3B3B3B64646464",
  "3636363664646464", "3232323264646464", "2D2D2D2D64646464", "2828282864646464",
  "2323232364646464", "1E1E1E1E64646464", "1A1A1A1A64646464", "0A0A0A0A64646464",
  "0A0A0A0A64646464", "0A0A0A0A64646464", "0A0A0A0A64646464", "0A0A0A0A64646464",
  "0A0A0A0A64646464"
};
const char* wave_3_0_C[] = {
  "0A0A0A0A00000000", "0A0A0A0A32323232", "0A0A0A0A64646464", "0A0A0A0A46464646",
  "1515151500000000", "1515151532323232", "1515151564646464", "1515151546464646",
  "2020202000000000", "2020202032323232", "2020202064646464", "2020202064646464",
  "2B2B2B2B00000000", "2B2B2B2B32323232", "2B2B2B2B64646464", "2B2B2B2B64646464",
  "3636363600000000", "3636363632323232", "3636363664646464", "3636363646464646"
};

const int WAVE_2_0_A_LENGTH = 12;
const int WAVE_2_0_B_LENGTH = 19;
const int WAVE_2_0_C_LENGTH = 20;
const int WAVE_3_0_A_LENGTH = 12;
const int WAVE_3_0_B_LENGTH = 17;
const int WAVE_3_0_C_LENGTH = 20;

//发送控制
bool isSending = false;
int waveIndex = 0;
char selectedWave = 'a';
unsigned long lastSendTime = 0;
const int sendInterval = 200;  // ms

//logs
const int MAX_LOGS = 10;
String logs[MAX_LOGS];
int logIndex = 0;
void addLog(const String& msg) {
  unsigned long ts = millis() / 1000;
  logs[logIndex] = String(ts) + "s: " + msg;
  logIndex = (logIndex + 1) % MAX_LOGS;
  Serial.println(msg);
}

//扫描设备列表
struct ScannedDevice {
  String name;
  String address;
  DeviceType type;
  int rssi;
};
std::vector<ScannedDevice> scannedDevices;

bool connectToDevice(const String& address, DeviceType type);

bool autoConnectNearestDevice() {
  if (deviceConnected) {
    addLog("已连接设备，跳过自动连接");
    return false;
  }
  if (scannedDevices.empty()) {
    addLog("未扫描到可连接的设备");
    return false;
  }

  const ScannedDevice* best = &scannedDevices[0];
  for (auto& d : scannedDevices) {
    if (d.rssi > best->rssi) best = &d;
  }

  addLog("自动连接距离最近的设备: " + best->name + " RSSI=" + String(best->rssi));
  if (!connectToDevice(best->address, best->type)) {
    addLog("自动连接失败");
    return false;
  }
  return true;
}

//BLE扫描回调
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    if (!advertisedDevice.haveName()) return;

    String name = advertisedDevice.getName().c_str();
    String address = advertisedDevice.getAddress().toString().c_str();

    if (name.startsWith(devicePrefix_2_0) || name.startsWith(devicePrefix_3_0)) {
      DeviceType type = name.startsWith(devicePrefix_2_0) ? DeviceType::DG2 : DeviceType::DG3;
      int rssi = advertisedDevice.getRSSI();
      bool exist = false;
      for (auto& d : scannedDevices)
        if (d.address == address) {
          exist = true;
          break;
        }
      if (!exist) {
        scannedDevices.push_back({ name, address, type, rssi });
        addLog("发现设备: " + name + " RSSI=" + String(rssi));
      }
    }
  }
};

static MyAdvertisedDeviceCallbacks scanCallbacks;

// ---------- BLE 连接 / 断开回调 ----------
class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient*) override {
    addLog("设备连接成功");
  }
  void onDisconnect(BLEClient*) override {
    deviceConnected = false;
    addLog("设备连接断开");
  }
};

static MyClientCallback clientCallbacks;

// ---------- Notify 数据回调（3.0） ----------
void notifyCallback(BLERemoteCharacteristic*, uint8_t* pData, size_t length, bool isNotify) {
  if (!isNotify || length < 4) return;
  if (pData[0] != 0xB1) return;  // 只处理 B1 回应

  uint8_t responseOrderNo = pData[1];
  uint8_t currentStrengthA = pData[2];
  uint8_t currentStrengthB = pData[3];
  strengthA = currentStrengthA;
  strengthB = currentStrengthB;

  addLog("收到强度回应: 序列号=" + String(responseOrderNo) + ", A=" + String(strengthA) + ", B=" + String(strengthB));

  if (responseOrderNo == orderNo && waitingForResponse) {
    isInputAllowed = true;
    waitingForResponse = false;
  }
}

// ---------- HEX → bytes ----------
std::vector<uint8_t> hexToBytes(const String& hex) {
  std::vector<uint8_t> v;
  if (hex.length() % 2 != 0) {
    addLog("hexToBytes: 无效长度 " + hex);
    return v;
  }

  for (size_t i = 0; i < hex.length(); i += 2) {
    String part = hex.substring(i, i + 2);
    char* endPtr = nullptr;
    long value = strtol(part.c_str(), &endPtr, 16);
    if (endPtr == nullptr || *endPtr != '\0' || value < 0 || value > 0xFF) {
      addLog("hexToBytes: 无效数据 " + part);
      v.clear();
      return v;
    }
    v.push_back(static_cast<uint8_t>(value));
  }
  return v;
}

// ---------- 获取当前波形 ----------
const char* getCurrentWave() {
  if (deviceType == DeviceType::DG2) {
    switch (selectedWave) {
      case 'a': return wave_2_0_A[waveIndex % WAVE_2_0_A_LENGTH];
      case 'b': return wave_2_0_B[waveIndex % WAVE_2_0_B_LENGTH];
      case 'c': return wave_2_0_C[waveIndex % WAVE_2_0_C_LENGTH];
    }
  } else if (deviceType == DeviceType::DG3) {
    switch (selectedWave) {
      case 'a': return wave_3_0_A[waveIndex % WAVE_3_0_A_LENGTH];
      case 'b': return wave_3_0_B[waveIndex % WAVE_3_0_B_LENGTH];
      case 'c': return wave_3_0_C[waveIndex % WAVE_3_0_C_LENGTH];
    }
  }
  return "000000";
}

/* ========== 2.0 设备数据发送 ========== */
bool sendData_2_0(const String& hexA, const String& hexB) {
  if (!deviceConnected || deviceType != DeviceType::DG2) return false;

  auto writeBuf = [&](BLERemoteCharacteristic* ch, const String& hex) -> bool {
    if (!ch) return false;
    bool canWriteRsp = ch->canWrite();
    bool canWriteNR = ch->canWriteNoResponse();
    if (!(canWriteRsp || canWriteNR)) return false;

    std::vector<uint8_t> bytes = hexToBytes(hex);
    if (hex.length() > 0 && bytes.empty()) return false;
#ifdef CONFIG_BT_NIMBLE_ROLE_CENTRAL
    return ch->writeValue(bytes.data(), bytes.size(), canWriteRsp);
#else
    uint8_t* data = new uint8_t[bytes.size()];
    std::copy(bytes.begin(), bytes.end(), data);
    ch->writeValue(data, bytes.size(), canWriteRsp);
    delete[] data;
    return true;
#endif
  };

  return writeBuf(pCharacteristicA_2_0, hexA) && writeBuf(pCharacteristicB_2_0, hexB);
}

/* ========== 2.0 设备强度设置 ========== */
bool setStrength_2_0(int channelA, int channelB) {
  if (!deviceConnected || deviceType != DeviceType::DG2) {
    addLog("设备未连接或非2.0设备");
    return false;
  }
  channelA = constrain(channelA, 0, 2047);
  channelB = constrain(channelB, 0, 2047);

  BLERemoteCharacteristic* pPwmAB2 =
    pClient->getService(BLEUUID(SERVICE_UUID_2_0))
      ->getCharacteristic(BLEUUID("955a1504-0fe2-f5aa-a094-84b8d4f3e8ad"));
  if (!pPwmAB2) {
    addLog("PWM_AB2 特性获取失败");
    return false;
  }

  uint32_t value = ((channelA & 0x7FF) << 11) | (channelB & 0x7FF);
  uint8_t data[3] = { uint8_t(value & 0xFF),
                      uint8_t((value >> 8) & 0xFF),
                      uint8_t((value >> 16) & 0xFF) };

#ifdef CONFIG_BT_NIMBLE_ROLE_CENTRAL
  bool success = pPwmAB2->writeValue(data, 3, true);
#else
  pPwmAB2->writeValue(data, 3, true);
  bool success = true;
#endif

  if (success) {
    strengthA = channelA;
    strengthB = channelB;
    addLog("设置2.0强度: A=" + String(channelA) + ", B=" + String(channelB));
  } else {
    addLog("设置2.0强度失败");
  }
  return success;
}

/* ========== 3.0 设备数据发送 ========== */
bool sendData_3_0(const String& waveData,
                  bool changeStrength = false,
                  int strA = 0, int strB = 0,
                  uint8_t method = 0) {
  if (!deviceConnected || deviceType != DeviceType::DG3) return false;

  auto writeBuf = [&](BLERemoteCharacteristic* ch, const std::vector<uint8_t>& bytes) -> bool {
    if (!ch) return false;
    bool canWriteRsp = ch->canWrite();
    bool canWriteNR = ch->canWriteNoResponse();
    if (!(canWriteRsp || canWriteNR)) return false;
#ifdef CONFIG_BT_NIMBLE_ROLE_CENTRAL
    return ch->writeValue(bytes.data(), bytes.size(), canWriteRsp);
#else
    uint8_t* data = new uint8_t[bytes.size()];
    std::copy(bytes.begin(), bytes.end(), data);
    ch->writeValue(data, bytes.size(), canWriteRsp);
    delete[] data;
    return true;
#endif
  };

  std::vector<uint8_t> commandBytes;
  commandBytes.push_back(0xB0);  // 指令头

  bool issuedChange = changeStrength && isInputAllowed;
  uint8_t seqMethod = 0x00;
  if (issuedChange) {
    orderNo = (orderNo + 1) & 0x0F;  // 序列号循环 1-15
    if (orderNo == 0) orderNo = 1;
    seqMethod = (orderNo << 4) | (method & 0x0F);
    waitingForResponse = true;
    isInputAllowed = false;
  }
  commandBytes.push_back(seqMethod);
  commandBytes.push_back(strA & 0xFF);
  commandBytes.push_back(strB & 0xFF);

  std::vector<uint8_t> waveBytes = hexToBytes(waveData);
  if (waveData.length() > 0 && waveBytes.empty()) return false;
  commandBytes.insert(commandBytes.end(), waveBytes.begin(), waveBytes.end());
  while (commandBytes.size() < 20) commandBytes.push_back(0x00);

  bool success = writeBuf(pCharacteristic_3_0_Write, commandBytes);
  if (!success && issuedChange) {
    isInputAllowed = true;
    waitingForResponse = false;
    addLog("写入失败，已回滚输入状态");
  }

  return success;
}

/* ========== 统一数据发送 ========== */
bool sendData(const String& hexData, const String& hexDataB) {
  if (!deviceConnected) {
    addLog("设备未连接");
    return false;
  }
  return (deviceType == DeviceType::DG2) ? sendData_2_0(hexData, hexDataB)
                                         : sendData_3_0(hexData);
}

/* ========== 强度设置包装 ========== */
bool setStrength(int channelA, int channelB, uint8_t method) {
  if (!deviceConnected || deviceType != DeviceType::DG3) {
    addLog("设备未连接或非3.0设备");
    return false;
  }
  channelA = constrain(channelA, 0, 200);
  channelB = constrain(channelB, 0, 200);
  return sendData_3_0(getCurrentWave(), true, channelA, channelB, method);
}

/* ========== A/B 通道相对/绝对调整 ========== */
bool adjustStrengthA(int value, uint8_t method) {
  if (!deviceConnected) return false;

  if (deviceType == DeviceType::DG3) {  // 3.0
    int newA = strengthA;
    if (method == 0x04) newA = min(200, strengthA + value);
    else if (method == 0x08) newA = max(0, strengthA - value);
    else if (method == 0x0C) newA = constrain(value, 0, 200);
    return setStrength(newA, strengthB, method);
  } else {  // 2.0
    int newA = strengthA;
    if (method == 0x04) newA = min(2047, strengthA + value * 7);
    else if (method == 0x08) newA = max(0, strengthA - value * 7);
    else if (method == 0x0C) newA = constrain(value * 7, 0, 2047);
    return setStrength_2_0(newA, strengthB);
  }
}

bool adjustStrengthB(int value, uint8_t method) {
  if (!deviceConnected) return false;

  if (deviceType == DeviceType::DG3) {  // 3.0
    int newB = strengthB;
    if (method == 0x01) newB = min(200, strengthB + value);
    else if (method == 0x02) newB = max(0, strengthB - value);
    else if (method == 0x03) newB = constrain(value, 0, 200);
    return setStrength(strengthA, newB, method);
  } else {  // 2.0
    int newB = strengthB;
    if (method == 0x01) newB = min(2047, strengthB + value * 7);
    else if (method == 0x02) newB = max(0, strengthB - value * 7);
    else if (method == 0x03) newB = constrain(value * 7, 0, 2047);
    return setStrength_2_0(strengthA, newB);
  }
}

/* ========== 波形发送循环 ========== */
void handleWaveSend() {
  if (!isSending || !deviceConnected) return;

  unsigned long now = millis();
  if (now - lastSendTime >= sendInterval) {
    lastSendTime = now;

    const char* data = getCurrentWave();
    bool success = false;

    if (deviceType == DeviceType::DG2) {  // ---- V2 ----
      success = sendData_2_0(data, data);
    } else {                                  // ---- V3 ----
      String combined = String(data) + data;  // 复制一次
      success = sendData_3_0(combined, false);
    }

    if (!success) {
      isSending = false;
      addLog("波形发送失败");
    } else if (waveIndex % 100 == 0) {
      addLog("波形发送 index=" + String(waveIndex));
    }
    ++waveIndex;
  }
}

/* ========== 断开 / 连接 ========== */
void disconnectDevice() {
  if (deviceConnected && pClient) {
    isSending = false;
    pClient->disconnect();
    deviceConnected = false;
    deviceType = DeviceType::None;
    connectedDeviceName = "";
    connectedDeviceAddress = "";
    pCharacteristicA_2_0 = nullptr;
    pCharacteristicB_2_0 = nullptr;
    pCharacteristic_3_0_Write = nullptr;
    pCharacteristic_3_0_Notify = nullptr;
    strengthA = 0;
    strengthB = 0;
    orderNo = 0;
    isInputAllowed = true;
    waitingForResponse = false;
    addLog("已断开连接");
  }
}

bool connectToDevice(const String& address, DeviceType type) {
  if (type == DeviceType::None) {
    addLog("未知设备类型");
    return false;
  }
  if (deviceConnected) disconnectDevice();
  BLEDevice::getScan()->stop();  // 避免连接时仍在扫描

  addLog("连接: " + address);

  BLEAddress bleAddress(address.c_str());
  pClient = BLEDevice::createClient();
  pClient->setClientCallbacks(&clientCallbacks);

  if (!pClient->connect(bleAddress, BLE_ADDR_TYPE_RANDOM)) {
    addLog("连接失败");
    deviceType = DeviceType::None;
    return false;
  }

  addLog("连接成功，MTU=517");
  pClient->setMTU(517);
  bool ok = false;

  if (type == DeviceType::DG2) {  // ----- 2.0 -----
    auto service = pClient->getService(BLEUUID(SERVICE_UUID_2_0));
    if (service) {
      pCharacteristicA_2_0 = service->getCharacteristic(BLEUUID(CHARACTERISTIC_A_UUID_2_0));
      pCharacteristicB_2_0 = service->getCharacteristic(BLEUUID(CHARACTERISTIC_B_UUID_2_0));
      ok = (pCharacteristicA_2_0 && pCharacteristicB_2_0);

      // 读取初始强度
      BLERemoteCharacteristic* pPwmAB2 =
        service->getCharacteristic(BLEUUID("955a1504-0fe2-f5aa-a094-84b8d4f3e8ad"));
      if (pPwmAB2 && pPwmAB2->canRead()) {
        String valueStr = pPwmAB2->readValue();  // ← 用 Arduino String
        if (valueStr.length() >= 3) {
          uint8_t valueBytes[3];
          valueStr.getBytes(valueBytes, 4);
          uint32_t data = (valueBytes[2] << 16) | (valueBytes[1] << 8) | valueBytes[0];
          strengthA = (data >> 11) & 0x7FF;
          strengthB = data & 0x7FF;
          addLog("获取当前强度: A=" + String(strengthA) + ", B=" + String(strengthB));
        }
      }
    }
  } else if (type == DeviceType::DG3) {  // ----- 3.0 -----
    auto service = pClient->getService(BLEUUID(SERVICE_UUID_3_0));
    if (service) {
      pCharacteristic_3_0_Write = service->getCharacteristic(BLEUUID(CHARACTERISTIC_WRITE_3_0));
      pCharacteristic_3_0_Notify = service->getCharacteristic(BLEUUID(CHARACTERISTIC_NOTIFY_3_0));
      ok = (pCharacteristic_3_0_Write && pCharacteristic_3_0_Notify);

      if (ok && pCharacteristic_3_0_Notify->canNotify()) {
        pCharacteristic_3_0_Notify->registerForNotify(notifyCallback);
        addLog("已注册通知回调");

        // 发送 BF 指令设定软上限
        std::vector<uint8_t> bfCommand = { 0xBF, 200, 200, 128, 0, 128, 0 };
#ifdef CONFIG_BT_NIMBLE_ROLE_CENTRAL
        pCharacteristic_3_0_Write->writeValue(bfCommand.data(), bfCommand.size(), true);
#else
        uint8_t* data = new uint8_t[bfCommand.size()];
        std::copy(bfCommand.begin(), bfCommand.end(), data);
        pCharacteristic_3_0_Write->writeValue(data, bfCommand.size(), true);
        delete[] data;
#endif
        addLog("已发送软上限设置");
      }
    }
  }

  if (!ok) {
    addLog("服务/特性获取失败");
    pClient->disconnect();
    deviceType = DeviceType::None;
    return false;
  }

  deviceType = type;
  deviceConnected = true;
  for (auto& d : scannedDevices)
    if (d.address == address) connectedDeviceName = d.name;
  connectedDeviceAddress = address;
  if (connectedDeviceName.length() == 0) connectedDeviceName = address;
  if (type == DeviceType::DG3) { strengthA = strengthB = 0; }
  orderNo = 0;
  isInputAllowed = true;
  waitingForResponse = false;
  return true;
}

/* ========== 扫描 ========== */
void startBleScan() {
  if (scanInProgress) {
    addLog("扫描进行中，忽略新的扫描请求");
    return;
  }
  scanInProgress = true;
  addLog("开始扫描");
  scannedDevices.clear();
  auto scan = BLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(&scanCallbacks);
  scan->setActiveScan(true);
  scan->start(3);
  scan->stop();
  scanInProgress = false;
  lastScanFinished = millis();
  if (autoConnectEnabled) {
    autoConnectNearestDevice();
  } else {
    addLog("自动连接已关闭，等待手动操作");
  }
}

void handleAutoScan() {
  if (scanInProgress) return;
  unsigned long now = millis();
  if (now - lastScanFinished >= autoScanIntervalMs) {
    startBleScan();
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
  if (deviceConnected) {
    html += "<p>已连接: " + connectedDeviceName + " (" + String(deviceTypeLabel(deviceType)) + ")</p>";
    if (deviceType == DeviceType::DG3) {
      html += "<p>通道A强度: " + String(strengthA) + "/200</p>";
      html += "<p>通道B强度: " + String(strengthB) + "/200</p>";
    } else {
      html += "<p>通道A强度: " + String(strengthA / 7) + " (原:" + String(strengthA) + ")</p>";
      html += "<p>通道B强度: " + String(strengthB / 7) + " (原:" + String(strengthB) + ")</p>";
    }
    html += "<a class='btn btn-danger' href='/disconnect'>断开连接</a>";
  } else {
    html += "<p>未连接</p><a class='btn' href='/scan'>扫描设备</a>";
  }
  html += "<p>自动连接: " + String(autoConnectEnabled ? "已开启" : "已关闭") + "</p>";
  html += autoConnectEnabled
              ? "<a class='btn btn-sm btn-danger' href='/auto-connect?enabled=0'>关闭自动连接</a>"
              : "<a class='btn btn-sm btn-success' href='/auto-connect?enabled=1'>开启自动连接</a>";
  html += "</div></div>";

  /* -------- 控制面板 -------- */
  if (deviceConnected) {
    /* --- 强度控制 --- */
    html += "<div class='panel'><h2>通道强度控制</h2>";

    auto strengthBlock = [&](char ch, int strength) -> String {
      String s = "";
      s += "<div class='strength-control'><h3>通道";
      s += (ch == 'a' ? 'A' : 'B');
      s += "</h3><div class='strength-display'>";
      s += String(deviceType == DeviceType::DG2 ? strength / 7 : strength);
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
      if (deviceType == DeviceType::DG3) {
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

    html += strengthBlock('a', strengthA);
    html += strengthBlock('b', strengthB);
    html += "</div>";  // panel 结束

    /* --- 波形控制 --- */
    html += "<div class='panel'><h2>波形控制</h2><form action='/wave'>";
    html += "<select name='type'>";
    html += "<option value='a'";
    if (selectedWave == 'a') html += " selected";
    html += ">波形A</option>";
    html += "<option value='b'";
    if (selectedWave == 'b') html += " selected";
    html += ">波形B</option>";
    html += "<option value='c'";
    if (selectedWave == 'c') html += " selected";
    html += ">波形C</option>";
    html += "</select><div class='center'><input class='btn' type='submit' value='切换波形'></div></form><div class='center'>";
    if (isSending)
      html += "<a class='btn btn-danger' href='/stop'>停止发送</a>";
    else
      html += "<a class='btn btn-success' href='/start'>开始发送</a>";
    html += "</div></div>";
  }

  /* -------- 设备列表 -------- */
  if (!deviceConnected && !scannedDevices.empty()) {
    html += "<div class='panel'><h2>可用设备</h2>";
    for (auto& d : scannedDevices) {
      html += "<a class='device-item' href='/connect?address=";
      html += d.address + "&type=" + String(deviceTypeToInt(d.type)) + "'>";
      html += d.name + " (" + String(deviceTypeLabel(d.type)) + ", RSSI=" + String(d.rssi) + "dBm)</a>";
    }
    html += "</div>";
  }

  /* -------- 日志 -------- */
  html += "<div class='panel'><h2>操作日志</h2>";
  for (int i = 0; i < MAX_LOGS; ++i) {
    int idx = (logIndex - 1 - i + MAX_LOGS) % MAX_LOGS;
    if (logs[idx].length()) html += "<div class='log-item'>" + logs[idx] + "</div>";
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
    startBleScan();
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
  });

  server.on("/auto-connect", HTTP_GET, []() {
    if (server.hasArg("enabled")) {
      autoConnectEnabled = server.arg("enabled").toInt() != 0;
      addLog(String("自动连接功能") + (autoConnectEnabled ? "已开启" : "已关闭"));
    }
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
  });

  /* ---- 连接设备 ---- */
  server.on("/connect", HTTP_GET, []() {
    if (server.hasArg("address") && server.hasArg("type")) {
      DeviceType type = parseDeviceType(server.arg("type").toInt());
      if (type == DeviceType::None) {
        addLog("连接失败: 未知设备类型");
      } else if (!connectToDevice(server.arg("address"), type)) {
        addLog("连接失败: 类型无效或连接错误");
      }
    }
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
  });

  /* ---- 断开连接 ---- */
  server.on("/disconnect", HTTP_GET, []() {
    disconnectDevice();
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
  });

  /* ---- 开始 / 停止波形发送 ---- */
  server.on("/start", HTTP_GET, []() {
    if (deviceConnected) {
      isSending = true;
      waveIndex = 0;
      addLog("开始发送");
    }
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
  });

  server.on("/stop", HTTP_GET, []() {
    isSending = false;
    addLog("停止发送");
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
  });

  /* ---- 切换波形 ---- */
  server.on("/wave", HTTP_GET, []() {
    if (server.hasArg("type")) {
      selectedWave = server.arg("type").charAt(0);
      addLog("切换波形 " + String(selectedWave));
    }
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
  });

  /* ---- 强度调整 ---- */
  server.on("/strength", HTTP_GET, []() {
    if (deviceConnected && server.hasArg("channel") && server.hasArg("value") && server.hasArg("method")) {

      char ch = server.arg("channel").charAt(0);
      int val = server.arg("value").toInt();
      uint8_t m = (uint8_t)server.arg("method").toInt();

      bool ok = (ch == 'a') ? adjustStrengthA(val, m)
                            : adjustStrengthB(val, m);

      if (ok)
        addLog(String("调整") + (ch == 'a' ? "A" : "B") + "强度: " + String(val) + ", 方法:" + String(m));
    }
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
  });

  server.begin();
  addLog("HTTP 服务器已启动");
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
  setupWeb();
  addLog("系统初始化完成");
  lastScanFinished = millis();
}

void loop() {
  server.handleClient();
  handleWaveSend();
  handleAutoScan();
  delay(10);
}
