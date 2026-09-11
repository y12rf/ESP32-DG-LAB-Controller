#pragma once

// Serializes destruction with the entire Arduino BLE GATT dispatcher.
// Never hold this guard while waiting for a BLE operation to complete.
class BleDispatchGuard {
 public:
  BleDispatchGuard();
  ~BleDispatchGuard();
  BleDispatchGuard(const BleDispatchGuard&) = delete;
  BleDispatchGuard& operator=(const BleDispatchGuard&) = delete;
};
