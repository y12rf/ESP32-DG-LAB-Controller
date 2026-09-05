#include <atomic>
#include <cassert>
#include <thread>
#include <iostream>
#include "BleDispatch.h"

std::atomic<bool> deletionBlocked{false}, callbackStarted{false};
std::atomic<bool> callbackFinished{false}, destroyed{false};
struct esp_ble_gattc_cb_param_t {};
struct BLEDevice {
  static void gattClientEventHandler(int,int,esp_ble_gattc_cb_param_t*);
};

// The build overlay wraps this dispatcher just as it wraps Arduino's one.
/* STATIC */ void BLEDevice::gattClientEventHandler(
    int event, int gattcIf, esp_ble_gattc_cb_param_t* param) {
  callbackStarted=true;
  // Model connect() returning failure while the callback still uses client.
  while(!deletionBlocked) std::this_thread::yield();
  assert(!destroyed);
  callbackFinished=true;
}

int main() {
  std::thread dispatcher([]{BLEDevice::gattClientEventHandler(0,0,nullptr);});
  while(!callbackStarted) std::this_thread::yield();
  std::thread cleanup([]{
    BleDispatchGuard guard;
    assert(callbackFinished);
    destroyed=true;
  });
  dispatcher.join(); cleanup.join();
  assert(destroyed && deletionBlocked);
  std::cout << "Client destruction waited for callback completion\n";
}
