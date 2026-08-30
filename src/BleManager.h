#pragma once

#include "AppLog.h"
#include "AppState.h"
#include <BLEAdvertisedDevice.h>
#include <BLEClient.h>
#include <BLEDevice.h>
#include <DgLabControl.h>
#include <atomic>
#include <freertos/queue.h>
#include <vector>

class BleManager {
 public:
  BleManager(AppState& state, AppLog& log);
  bool begin();
  bool pollEvent(BleEvent& event);
  bool startBleScan();
  bool handleAutoScan();
  bool connectToDevice(const String& address, DeviceType type,
                       const dglab::DeviceIdentity* identity = nullptr,
                       bool manualSelection = false);
  void disconnectDevice();
  void handleDisconnectEvent();
  bool handleDisconnectedClient(bool& manualDisconnect);
  const std::vector<ScannedDevice>& scannedDevices() const { return scannedDevices_; }
  bool writeV2WaveBytes(const std::vector<uint8_t>& bytesA,
                        const std::vector<uint8_t>& bytesB);
  bool hasV2StrengthCharacteristic() const { return characteristicPwmAB2_ != nullptr; }
  bool writeV2StrengthBytes(const uint8_t (&bytes)[3]);
  bool writeV3Frame(const dglab::B0Frame& frame);

 private:
  class ScanCallbacks final : public BLEAdvertisedDeviceCallbacks {
   public:
    explicit ScanCallbacks(BleManager& owner) : owner_(owner) {}
    void onResult(BLEAdvertisedDevice device) override;
   private:
    BleManager& owner_;
  };
  class ClientCallbacks final : public BLEClientCallbacks {
   public:
    explicit ClientCallbacks(BleManager& owner) : owner_(owner) {}
    void onConnect(BLEClient* client) override;
    void onDisconnect(BLEClient* client) override;
   private:
    BleManager& owner_;
  };
  AppState& state_;
  AppLog& log_;
  BLEClient* client_ = nullptr;
  BLERemoteCharacteristic* characteristicA2_ = nullptr;
  BLERemoteCharacteristic* characteristicB2_ = nullptr;
  BLERemoteCharacteristic* characteristicWrite3_ = nullptr;
  BLERemoteCharacteristic* characteristicNotify3_ = nullptr;
  BLERemoteCharacteristic* characteristicPwmAB2_ = nullptr;
  QueueHandle_t eventQueue_ = nullptr;
  std::atomic<uint32_t> droppedEvents_{0};
  std::vector<ScannedDevice> scannedDevices_;
  ScanCallbacks scanCallbacks_;
  ClientCallbacks clientCallbacks_;
  static BleManager* notifyOwner_;

  void enqueueEvent(const BleEvent& event);
  static void notifyCallback(BLERemoteCharacteristic*, uint8_t* data,
                             size_t length, bool isNotify);
  void handleNotification(uint8_t* data, size_t length, bool isNotify);
  bool autoConnectNearestDevice();
  dglab::DeviceIdentity makeIdentity(BLEAddress address, uint8_t addressType);
};
