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
  if (eventQueue_ && xQueueReceive(eventQueue_, &event, 0) == pdPASS) return true;
  // A disconnect callback can be dropped when the fixed queue is full.  The
  // loop still has an authoritative link-alive flag to recover that event.
  if (state_.deviceConnected.load(std::memory_order_acquire) &&
      !state_.bleLinkAlive.load(std::memory_order_acquire)) {
    event = {BleEventType::Disconnected, 0, 0, 0};
    return true;
  }
  return false;
}

uint32_t BleManager::takeDroppedEventCount() {
  return droppedEvents_.exchange(0, std::memory_order_relaxed);
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

void BleManager::notifyCallback(BLERemoteCharacteristic* characteristic,
                                uint8_t* data,
                                size_t length, bool isNotify) {
  if (notifyOwner_) {
    notifyOwner_->handleNotification(characteristic, data, length, isNotify);
  }
}

void BleManager::handleNotification(BLERemoteCharacteristic* characteristic,
                                    uint8_t* data, size_t length,
                                    bool isNotify) {
  if (!isNotify) return;
  if (characteristic == characteristicPwmAB2_) {
    if (length < 3) return;
    uint8_t bytes[3] = {data[0], data[1], data[2]};
    uint16_t strengthA = 0;
    uint16_t strengthB = 0;
    dglab::decodeV2Strength(bytes, strengthA, strengthB);
    enqueueEvent({BleEventType::V2StrengthFeedback, 0, strengthA, strengthB});
    return;
  }
  if (characteristic != characteristicNotify3_ || length < 4 ||
      data[0] != 0xB1) {
    return;
  }
  enqueueEvent({BleEventType::StrengthResponse, data[1], data[2], data[3]});
}

void BleManager::handleDisconnectEvent() {
  if (!state_.deviceConnected.load(std::memory_order_acquire) &&
      state_.clientCleanupPending) {
    return;
  }
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
  state_.bleLinkAlive.store(false, std::memory_order_release);
  characteristicA2_ = nullptr;
  characteristicB2_ = nullptr;
  characteristicPwmAB2_ = nullptr;
  characteristicWrite3_ = nullptr;
  characteristicNotify3_ = nullptr;
  if (client_) {
    delete client_;
    client_ = nullptr;
  }
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
  if (!connectToDevice(best->address, best->type, best->identity, false)) {
    log_.add("自动连接失败");
    return false;
  }
  return true;
}

bool BleManager::connectToDevice(const String& address, DeviceType type,
                                 const dglab::DeviceIdentity& identity,
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

  const uint8_t addressType = identity.addressType;
  if (!client_->connect(bleAddress, static_cast<esp_ble_addr_type_t>(addressType))) {
    log_.add("连接失败");
    delete client_;
    client_ = nullptr;
    state_.deviceType = DeviceType::None;
    return false;
  }

  // The client is now link-connected even though profile discovery is not
  // ready yet.  Keeping this state visible lets a dropped disconnect event
  // use the link-alive fallback during discovery failure as well.
  state_.deviceConnected.store(true, std::memory_order_release);
  state_.bleLinkAlive.store(true, std::memory_order_release);
  state_.strengthConfirmed = false;

  log_.add("连接成功，MTU=517");
  client_->setMTU(517);
  bool ok = false;
  bool initialStrengthConfirmed = false;

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
      characteristicPwmAB2_ = service->getCharacteristic(BLEUUID(kCharacteristicPwmUuid2));
      ok = characteristicA2_ && characteristicB2_ && characteristicPwmAB2_ &&
           (characteristicA2_->canWrite() || characteristicA2_->canWriteNoResponse()) &&
           (characteristicB2_->canWrite() || characteristicB2_->canWriteNoResponse()) &&
           characteristicPwmAB2_->canRead() && characteristicPwmAB2_->canNotify() &&
           (characteristicPwmAB2_->canWrite() || characteristicPwmAB2_->canWriteNoResponse());
      if (ok) {
        characteristicPwmAB2_->registerForNotify(notifyCallback);
        auto value = characteristicPwmAB2_->readValue();
        if (value.length() >= 3) {
          const uint8_t valueBytes[3] = {
            static_cast<uint8_t>(value[0]), static_cast<uint8_t>(value[1]), static_cast<uint8_t>(value[2])
          };
          uint16_t strengthA = 0;
          uint16_t strengthB = 0;
          dglab::decodeV2Strength(valueBytes, strengthA, strengthB);
          state_.strengthA = strengthA;
          state_.strengthB = strengthB;
          initialStrengthConfirmed = true;
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
        const uint8_t bfCommand[7] = {0xBF, 200, 200, 128, 0, 128, 0};
        ok = writeBytes(characteristicWrite3_, bfCommand, sizeof(bfCommand));
        if (ok) log_.add("已发送软上限设置");
      }
    }
  }

  if (!ok) {
    log_.add("服务/特性获取失败");
    state_.linkReady.store(false, std::memory_order_release);
    client_->disconnect();
    state_.deviceType = DeviceType::None;
    return false;
  }

  state_.deviceType = type;
  state_.resumeDeviceType = type;
  state_.linkReady.store(true, std::memory_order_release);
  state_.strengthConfirmed = type == DeviceType::DG2 && initialStrengthConfirmed;
  for (auto& d : scannedDevices_)
    if (d.address == address) state_.connectedDeviceName = d.name;
  state_.connectedDeviceAddress = address;
  if (state_.connectedDeviceName.length() == 0) state_.connectedDeviceName = address;
  state_.connectedIdentity = identity;
  state_.connectedIdentityValid = true;
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
    state_.linkReady.store(false, std::memory_order_release);
    client_->disconnect();
    log_.add("已断开连接");
  }
}

void BleManager::handleTransportFailure() {
  state_.linkReady.store(false, std::memory_order_release);
  state_.manualDisconnectRequested = false;
  if (client_ && state_.deviceConnected.load(std::memory_order_acquire)) {
    client_->disconnect();
  } else {
    state_.bleLinkAlive.store(false, std::memory_order_release);
    state_.deviceConnected.store(false, std::memory_order_release);
    state_.clientCleanupPending = false;
  }
}

bool BleManager::writeBytes(BLERemoteCharacteristic* characteristic,
                            const uint8_t* data, size_t length) {
  if (!characteristic || !data || length == 0) return false;
  const bool withResponse = characteristic->canWrite();
  if (!withResponse && !characteristic->canWriteNoResponse()) return false;
#ifdef CONFIG_BT_NIMBLE_ROLE_CENTRAL
  return characteristic->writeValue(const_cast<uint8_t*>(data), length, withResponse);
#else
  // ESP32 BLE Arduino's classic API returns void; reaching the call is the
  // only success signal available on that implementation.
  characteristic->writeValue(const_cast<uint8_t*>(data), length, withResponse);
  return true;
#endif
}

bool BleManager::writeV2WaveBytes(const std::vector<uint8_t>& bytesA,
                                  const std::vector<uint8_t>& bytesB) {
  return writeBytes(characteristicA2_, bytesA.data(), bytesA.size()) &&
         writeBytes(characteristicB2_, bytesB.data(), bytesB.size());
}

bool BleManager::writeV2StrengthBytes(const uint8_t (&bytes)[3]) {
  return writeBytes(characteristicPwmAB2_, bytes, 3);
}

bool BleManager::writeV3Frame(const dglab::B0Frame& frame) {
  if (!characteristicWrite3_ || !state_.deviceConnected || state_.deviceType != DeviceType::DG3) return false;
  return writeBytes(characteristicWrite3_, frame.bytes, sizeof(frame.bytes));
}
