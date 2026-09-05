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
  uint32_t takeDroppedEventCount();
  bool startBleScan();
  bool handleAutoScan();
  bool connectToDevice(const String& address, DeviceType type,
                       const dglab::DeviceIdentity& identity,
                       bool manualSelection = false);
  void disconnectDevice();
  void handleTransportFailure();
  void handleDisconnectEvent();
  bool handleDisconnectedClient(bool& manualDisconnect);
  const std::vector<ScannedDevice>& scannedDevices() const { return scannedDevices_; }
  bool writeV2WaveBytes(const uint8_t* bytesA, size_t lengthA,
                        const uint8_t* bytesB, size_t lengthB);
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
  QueueHandle_t subscriptionQueue_ = nullptr;
  // These fields are accessed under BleDispatchGuard (also held by callbacks).
  esp_gatt_if_t notifyGattIf_ = ESP_GATT_IF_NONE;
  uint16_t notifyHandle_ = 0;
  bool subscriptionWaiting_ = false;
  esp_gattc_cb_event_t subscriptionEvent_ = ESP_GATTC_REG_FOR_NOTIFY_EVT;
  uint16_t subscriptionHandle_ = 0;
  std::atomic<uint32_t> droppedEvents_{0};
  // Published only after BLEDevice has finished dispatching the disconnect.
  std::atomic<bool> disconnectComplete_{false};
  bool disconnectCallbackPending_ = false;  // BLE callback task only
  std::vector<ScannedDevice> scannedDevices_;
  ScanCallbacks scanCallbacks_;
  ClientCallbacks clientCallbacks_;
  static BleManager* notifyOwner_;

  void enqueueEvent(const BleEvent& event);
  bool writeBytes(BLERemoteCharacteristic* characteristic,
                  const uint8_t* data, size_t length);
  bool subscribe(BLERemoteCharacteristic* characteristic);
  bool waitSubscription();
  static void afterGattEvent(esp_gattc_cb_event_t event, esp_gatt_if_t gattcIf,
                             esp_ble_gattc_cb_param_t* param);
  void handleNotification(uint8_t* data, size_t length, bool isNotify);
  bool autoConnectNearestDevice();
  dglab::DeviceIdentity makeIdentity(BLEAddress address, uint8_t addressType);
};
