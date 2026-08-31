#pragma once

#include <Arduino.h>
#include <DgLabControl.h>
#include <atomic>
#include <stdint.h>

enum class DeviceType : uint8_t { None = 0, DG2 = 1, DG3 = 2 };

inline const char* deviceTypeLabel(DeviceType type) {
  switch (type) {
    case DeviceType::DG2: return "2.0版本";
    case DeviceType::DG3: return "3.0版本";
    default: return "未知版本";
  }
}

inline int deviceTypeToInt(DeviceType type) {
  switch (type) {
    case DeviceType::DG2: return 1;
    case DeviceType::DG3: return 2;
    default: return 0;
  }
}

inline DeviceType parseDeviceType(int value) {
  switch (value) {
    case 1: return DeviceType::DG2;
    case 2: return DeviceType::DG3;
    default: return DeviceType::None;
  }
}

struct ScannedDevice {
  String name;
  String address;
  DeviceType type;
  int rssi;
  dglab::DeviceIdentity identity;
};

enum class BleEventType : uint8_t {
  StrengthResponse,
  V2StrengthFeedback,
  Disconnected
};

struct BleEvent {
  BleEventType type;
  uint8_t sequence;
  uint16_t strengthA;
  uint16_t strengthB;
};

struct AppState {
  DeviceType deviceType = DeviceType::None;
  std::atomic<bool> deviceConnected{false};
  String connectedDeviceName;
  String connectedDeviceAddress;
  bool autoConnectEnabled = true;
  unsigned long lastScanFinished = 0;
  bool scanInProgress = false;
  bool autoScanSuppressedLogged = false;
  int strengthA = 0;
  int strengthB = 0;
  uint8_t orderNo = 0;
  bool isInputAllowed = true;
  bool waitingForResponse = false;
  bool strengthConfirmed = false;
  std::atomic<bool> linkReady{false};
  std::atomic<bool> bleLinkAlive{false};
  bool desiredSending = false;
  bool manualDisconnectRequested = false;
  bool clientCleanupPending = false;
  dglab::DeviceIdentity connectedIdentity = {{0, 0, 0, 0, 0, 0}, 0};
  bool connectedIdentityValid = false;
  DeviceType resumeDeviceType = DeviceType::None;
  bool isSending = false;
  int waveIndex = 0;
  char selectedWave = 'a';
  unsigned long lastSendTime = 0;
};
