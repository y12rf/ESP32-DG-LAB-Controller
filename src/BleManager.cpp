#include "BleManager.h"

#include <algorithm>
#include <BLEScan.h>

namespace {
constexpr const char* kServiceUuid2 = "955a180b-0fe2-f5aa-a094-84b8d4f3e8ad";
constexpr const char* kCharacteristicAUuid2 = "955a1506-0fe2-f5aa-a094-84b8d4f3e8ad";
constexpr const char* kCharacteristicBUuid2 = "955a1505-0fe2-f5aa-a094-84b8d4f3e8ad";
constexpr const char* kCharacteristicPwmUuid2 = "955a1504-0fe2-f5aa-a094-84b8d4f3e8ad";
constexpr const char* kServiceUuid3 = "0000180c-0000-1000-8000-00805f9b34fb";
constexpr const char* kCharacteristicWriteUuid3 = "0000150a-0000-1000-8000-00805f9b34fb";
constexpr const char* kCharacteristicNotifyUuid3 = "0000150b-0000-1000-8000-00805f9b34fb";
constexpr const char* devicePrefix_2_0 = "D-LAB";
constexpr const char* devicePrefix_3_0 = "47";
constexpr unsigned long kAutoScanIntervalMs = 10000;
constexpr int kServiceRetries = 10;
constexpr unsigned long kServiceRetryDelayMs = 150;
}

BleManager* BleManager::notifyOwner_ = nullptr;

BleManager::BleManager(AppState& state, AppLog& log)
    : state_(state), log_(log), scanCallbacks_(*this), clientCallbacks_(*this) {}

bool BleManager::begin() {
  notifyOwner_ = this;
  eventQueue_ = xQueueCreate(16, sizeof(BleEvent));
  return eventQueue_ != nullptr;
}

void BleManager::enqueueEvent(const BleEvent& event) {
  if (!eventQueue_ || xQueueSend(eventQueue_, &event, 0) != pdPASS) {
    droppedEvents_.fetch_add(1, std::memory_order_relaxed);
  }
}

bool BleManager::pollEvent(BleEvent& event) {
  return eventQueue_ && xQueueReceive(eventQueue_, &event, 0) == pdPASS;
}

dglab::DeviceIdentity BleManager::makeIdentity(BLEAddress address, uint8_t addressType) {
  dglab::DeviceIdentity identity = {{0, 0, 0, 0, 0, 0}, addressType};
  esp_bd_addr_t* native = address.getNative();
  if (native) std::copy(*native, *native + 6, identity.address);
  return identity;
}

void BleManager::ScanCallbacks::onResult(BLEAdvertisedDevice advertisedDevice) {
  if (!advertisedDevice.haveName()) return;

  String name = advertisedDevice.getName().c_str();
  String address = advertisedDevice.getAddress().toString().c_str();

  if (name.startsWith(devicePrefix_2_0) || name.startsWith(devicePrefix_3_0)) {
    DeviceType type = name.startsWith(devicePrefix_2_0) ? DeviceType::DG2 : DeviceType::DG3;
    int rssi = advertisedDevice.getRSSI();
    dglab::DeviceIdentity identity = owner_.makeIdentity(
        advertisedDevice.getAddress(), static_cast<uint8_t>(advertisedDevice.getAddressType()));
    bool exist = false;
    for (auto& d : owner_.scannedDevices_)
      if (d.address == address) {
        exist = true;
        break;
      }
    if (!exist) owner_.scannedDevices_.push_back({ name, address, type, rssi, identity });
  }
}

void BleManager::ClientCallbacks::onConnect(BLEClient*) {
  owner_.state_.bleLinkAlive.store(true, std::memory_order_release);
}

void BleManager::ClientCallbacks::onDisconnect(BLEClient*) {
  owner_.state_.bleLinkAlive.store(false, std::memory_order_release);
  owner_.enqueueEvent({BleEventType::Disconnected, 0, 0, 0});
}

void BleManager::notifyCallback(BLERemoteCharacteristic*, uint8_t* data,
                                size_t length, bool isNotify) {
  if (notifyOwner_) notifyOwner_->handleNotification(data, length, isNotify);
}

void BleManager::handleNotification(uint8_t* data, size_t length, bool isNotify) {
  if (!isNotify || length < 4) return;
  if (data[0] != 0xB1) return;
  enqueueEvent({BleEventType::StrengthResponse, data[1], data[2], data[3]});
}

void BleManager::handleDisconnectEvent() {
  log_.add("设备连接断开");
  state_.deviceConnected.store(false, std::memory_order_release);
  state_.linkReady.store(false, std::memory_order_release);
  state_.clientCleanupPending = true;
}

bool BleManager::handleDisconnectedClient(bool& manualDisconnect) {
  if (!state_.clientCleanupPending) return false;
  state_.clientCleanupPending = false;
  manualDisconnect = state_.manualDisconnectRequested;
  state_.manualDisconnectRequested = false;
  state_.deviceConnected.store(false, std::memory_order_release);
  state_.linkReady.store(false, std::memory_order_release);
  if (client_) {
    delete client_;
    client_ = nullptr;
  }
  characteristicA2_ = nullptr;
  characteristicB2_ = nullptr;
  characteristicPwmAB2_ = nullptr;
  characteristicWrite3_ = nullptr;
  characteristicNotify3_ = nullptr;
  state_.deviceType = DeviceType::None;
  if (manualDisconnect) state_.connectedIdentityValid = false;
  return true;
}

bool BleManager::autoConnectNearestDevice() {
  if (state_.deviceConnected || state_.clientCleanupPending) {
    log_.add("已连接设备，跳过自动连接");
    return false;
  }
  if (scannedDevices_.empty()) {
    log_.add("未扫描到可连接的设备");
    return false;
  }

  const ScannedDevice* best = nullptr;
  if (state_.desiredSending && state_.connectedIdentityValid) {
    for (auto& d : scannedDevices_) {
      if (d.type == state_.resumeDeviceType && dglab::sameIdentity(d.identity, state_.connectedIdentity)) {
        best = &d;
        break;
      }
    }
    if (!best) return false;
  } else {
    best = &scannedDevices_[0];
    for (auto& d : scannedDevices_)
      if (d.rssi > best->rssi) best = &d;
  }

  log_.add("自动连接距离最近的设备: " + best->name + " RSSI=" + String(best->rssi));
  if (!connectToDevice(best->address, best->type, &best->identity, false)) {
    log_.add("自动连接失败");
    return false;
  }
  return true;
}

bool BleManager::connectToDevice(const String& address, DeviceType type,
                                 const dglab::DeviceIdentity* identity,
                                 bool) {
  if (type == DeviceType::None) {
    log_.add("未知设备类型");
    return false;
  }
  if (state_.deviceConnected || state_.clientCleanupPending) return false;
  BLEDevice::getScan()->stop();

  log_.add("连接: " + address);

  BLEAddress bleAddress(address.c_str());
  client_ = BLEDevice::createClient();
  client_->setClientCallbacks(&clientCallbacks_);

  const uint8_t addressType = identity ? identity->addressType : 1;
  if (!client_->connect(bleAddress, static_cast<esp_ble_addr_type_t>(addressType))) {
    log_.add("连接失败");
    delete client_;
    client_ = nullptr;
    state_.deviceType = DeviceType::None;
    return false;
  }

  log_.add("连接成功，MTU=517");
  client_->setMTU(517);
  bool ok = false;

  auto getServiceWithRetry = [&](const BLEUUID& uuid) -> BLERemoteService* {
    BLERemoteService* service = nullptr;
    for (int attempt = 0; attempt < kServiceRetries && !service; ++attempt) {
      service = client_->getService(uuid);
      if (service || attempt == kServiceRetries - 1) break;
      delay(kServiceRetryDelayMs);
    }
    return service;
  };

  if (type == DeviceType::DG2) {
    auto service = getServiceWithRetry(BLEUUID(kServiceUuid2));
    if (service) {
      characteristicA2_ = service->getCharacteristic(BLEUUID(kCharacteristicAUuid2));
      characteristicB2_ = service->getCharacteristic(BLEUUID(kCharacteristicBUuid2));
      ok = (characteristicA2_ && characteristicB2_);

      characteristicPwmAB2_ = service->getCharacteristic(BLEUUID(kCharacteristicPwmUuid2));
      if (characteristicPwmAB2_ && characteristicPwmAB2_->canRead()) {
        auto value = characteristicPwmAB2_->readValue();
        if (value.length() >= 3) {
          uint8_t valueBytes[3] = {
            static_cast<uint8_t>(value[0]), static_cast<uint8_t>(value[1]), static_cast<uint8_t>(value[2])
          };
          uint32_t data = (valueBytes[2] << 16) | (valueBytes[1] << 8) | valueBytes[0];
          state_.strengthA = (data >> 11) & 0x7FF;
          state_.strengthB = data & 0x7FF;
          log_.add("获取当前强度: A=" + String(state_.strengthA) + ", B=" + String(state_.strengthB));
        }
      }
    }
  } else if (type == DeviceType::DG3) {
    auto service = getServiceWithRetry(BLEUUID(kServiceUuid3));
    if (service) {
      characteristicWrite3_ = service->getCharacteristic(BLEUUID(kCharacteristicWriteUuid3));
      characteristicNotify3_ = service->getCharacteristic(BLEUUID(kCharacteristicNotifyUuid3));
      ok = (characteristicWrite3_ && characteristicNotify3_ &&
            (characteristicWrite3_->canWrite() || characteristicWrite3_->canWriteNoResponse()) &&
            characteristicNotify3_->canNotify());

      if (ok && characteristicNotify3_->canNotify()) {
        characteristicNotify3_->registerForNotify(notifyCallback);
        log_.add("已注册通知回调");
        std::vector<uint8_t> bfCommand = { 0xBF, 200, 200, 128, 0, 128, 0 };
#ifdef CONFIG_BT_NIMBLE_ROLE_CENTRAL
        characteristicWrite3_->writeValue(bfCommand.data(), bfCommand.size(), true);
#else
        uint8_t* data = new uint8_t[bfCommand.size()];
        std::copy(bfCommand.begin(), bfCommand.end(), data);
        characteristicWrite3_->writeValue(data, bfCommand.size(), true);
        delete[] data;
#endif
        log_.add("已发送软上限设置");
      }
    }
  }

  if (!ok) {
    log_.add("服务/特性获取失败");
    client_->disconnect();
    state_.clientCleanupPending = true;
    state_.deviceType = DeviceType::None;
    return false;
  }

  state_.deviceType = type;
  state_.resumeDeviceType = type;
  state_.deviceConnected.store(true, std::memory_order_release);
  state_.linkReady.store(true, std::memory_order_release);
  state_.bleLinkAlive.store(true, std::memory_order_release);
  for (auto& d : scannedDevices_)
    if (d.address == address) state_.connectedDeviceName = d.name;
  state_.connectedDeviceAddress = address;
  if (state_.connectedDeviceName.length() == 0) state_.connectedDeviceName = address;
  if (identity) {
    state_.connectedIdentity = *identity;
    state_.connectedIdentityValid = true;
  }
  return true;
}

bool BleManager::startBleScan() {
  if (state_.deviceConnected) {
    log_.add("设备已连接，跳过扫描请求");
    return false;
  }
  if (state_.scanInProgress) {
    log_.add("扫描进行中，忽略新的扫描请求");
    return false;
  }
  state_.scanInProgress = true;
  log_.add("开始扫描");
  scannedDevices_.clear();
  auto scan = BLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(&scanCallbacks_);
  scan->setActiveScan(true);
  scan->start(3);
  scan->stop();
  state_.scanInProgress = false;
  state_.lastScanFinished = millis();
  if (state_.autoConnectEnabled) return autoConnectNearestDevice();
  log_.add("自动连接已关闭，等待手动操作");
  return false;
}

bool BleManager::handleAutoScan() {
  if (state_.deviceConnected) {
    if (!state_.autoScanSuppressedLogged) {
      log_.add("设备已连接，跳过自动扫描");
      state_.autoScanSuppressedLogged = true;
    }
    return false;
  }
  state_.autoScanSuppressedLogged = false;
  if (state_.scanInProgress) return false;
  unsigned long now = millis();
  if (now - state_.lastScanFinished >= kAutoScanIntervalMs) return startBleScan();
  return false;
}

void BleManager::disconnectDevice() {
  if (state_.deviceConnected && client_) {
    state_.manualDisconnectRequested = true;
    state_.desiredSending = false;
    state_.isSending = false;
    client_->disconnect();
    log_.add("已断开连接");
  }
}

bool BleManager::writeV2WaveBytes(const std::vector<uint8_t>& bytesA,
                                  const std::vector<uint8_t>& bytesB) {
  auto writeBuf = [](BLERemoteCharacteristic* characteristic,
                     const std::vector<uint8_t>& bytes) -> bool {
    if (!characteristic) return false;
    bool canWriteRsp = characteristic->canWrite();
    bool canWriteNR = characteristic->canWriteNoResponse();
    if (!(canWriteRsp || canWriteNR)) return false;
#ifdef CONFIG_BT_NIMBLE_ROLE_CENTRAL
    return characteristic->writeValue(const_cast<uint8_t*>(bytes.data()), bytes.size(), canWriteRsp);
#else
    uint8_t* data = new uint8_t[bytes.size()];
    std::copy(bytes.begin(), bytes.end(), data);
    characteristic->writeValue(data, bytes.size(), canWriteRsp);
    delete[] data;
    return true;
#endif
  };
  return writeBuf(characteristicA2_, bytesA) && writeBuf(characteristicB2_, bytesB);
}

bool BleManager::writeV2StrengthBytes(const uint8_t (&bytes)[3]) {
  if (!characteristicPwmAB2_) {
    log_.add("PWM_AB2 特性获取失败");
    return false;
  }
#ifdef CONFIG_BT_NIMBLE_ROLE_CENTRAL
  return characteristicPwmAB2_->writeValue(const_cast<uint8_t*>(bytes), 3, true);
#else
  characteristicPwmAB2_->writeValue(const_cast<uint8_t*>(bytes), 3, true);
  return true;
#endif
}

bool BleManager::writeV3Frame(const dglab::B0Frame& frame) {
  if (!characteristicWrite3_ || !state_.deviceConnected || state_.deviceType != DeviceType::DG3) return false;
  if (!characteristicWrite3_->canWrite() && !characteristicWrite3_->canWriteNoResponse()) return false;
#ifdef CONFIG_BT_NIMBLE_ROLE_CENTRAL
  return characteristicWrite3_->writeValue(frame.bytes, sizeof(frame.bytes), true);
#else
  characteristicWrite3_->writeValue(const_cast<uint8_t*>(frame.bytes), sizeof(frame.bytes), true);
  return true;
#endif
}
