#include <atomic>
#include <cassert>
#include <cstdint>
#include <functional>
#include <iostream>

using esp_gatt_if_t = int;
using esp_gatt_status_t = int;
enum esp_gattc_cb_event_t {
  ESP_GATTC_REG_FOR_NOTIFY_EVT, ESP_GATTC_WRITE_DESCR_EVT,
  ESP_GATTC_NOTIFY_EVT, ESP_GATTC_DISCONNECT_EVT
};
constexpr int ESP_GATT_IF_NONE=-1, ESP_GATT_ERROR=1, ESP_GATT_OK=0, ESP_OK=0;
constexpr int ESP_GATT_WRITE_TYPE_RSP=0, ESP_GATT_AUTH_REQ_NONE=0, pdPASS=1;
int pdMS_TO_TICKS(int ms) { return ms; }
struct esp_ble_gattc_cb_param_t {
  struct { uint16_t handle; int status; } reg_for_notify{}, write{};
  struct { uint16_t handle; uint8_t* value; int value_len; bool is_notify; } notify{};
};
int lockDepth=0, waits=0, calls=0;
struct BleDispatchGuard {
  BleDispatchGuard() { assert(lockDepth++==0); }
  ~BleDispatchGuard() { --lockDepth; }
};
struct Queue { bool full=false; int value=0; } queue;
std::function<void()> onWait;
int xQueueReceive(Queue* q, int* value, int ticks) {
  assert(lockDepth==0); assert(ticks==1500); ++waits;
  onWait();
  if (!q->full) return 0;
  *value=q->value; q->full=false; return pdPASS;
}
int xQueueSend(Queue* q, const int* value, int) {
  if(q->full) return 0;
  q->full=true; q->value=*value; return pdPASS;
}
void xQueueReset(Queue* q) { q->full=false; }
struct BLEUUID { explicit BLEUUID(uint16_t) {} };
struct Descriptor { uint16_t getHandle() { return 8; } } descriptor;
struct BLERemoteCharacteristic {
  Descriptor* getDescriptor(BLEUUID) { return &descriptor; }
  uint16_t getHandle() { return 7; }
} characteristic;
struct Address { uint8_t bytes[6]{}; uint8_t (*getNative())[6] { return &bytes; } };
struct Client {
  int getGattcIf() { return 2; }
  int getConnId() { return 3; }
  Address getPeerAddress() { return {}; }
} client;
int syncFailureStage=0;
int esp_ble_gattc_register_for_notify(int, uint8_t*, uint16_t) {
  assert(lockDepth==1); ++calls;
  return syncFailureStage==1 ? 1 : ESP_OK;
}
int esp_ble_gattc_write_char_descr(int,int,uint16_t,int length,uint8_t* data,int,int) {
  assert(lockDepth==1); ++calls;
  assert(length==2 && data[0]==1 && data[1]==0);
  return syncFailureStage==2 ? 1 : ESP_OK;
}
class BleManager {
 public:
  Client* client_=&client;
  Queue* subscriptionQueue_=&queue;
  esp_gatt_if_t notifyGattIf_=ESP_GATT_IF_NONE;
  uint16_t notifyHandle_=0, subscriptionHandle_=0;
  bool subscriptionWaiting_=false, disconnectCallbackPending_=false;
  esp_gattc_cb_event_t subscriptionEvent_=ESP_GATTC_REG_FOR_NOTIFY_EVT;
  std::atomic<bool> disconnectComplete_{false};
  struct { std::atomic<bool> bleLinkAlive{true}; } state_;
  int notifications=0;
  static BleManager* notifyOwner_;
  void handleNotification(uint8_t*,int,bool) { ++notifications; }
  static void afterGattEvent(esp_gattc_cb_event_t,int,esp_ble_gattc_cb_param_t*);
  bool subscribe(BLERemoteCharacteristic*);
  bool waitSubscription();
};
BleManager* BleManager::notifyOwner_=nullptr;

// PRODUCTION_METHODS

int main() {
  // 0 = success, 1/2 = synchronous failure, 3/4 = asynchronous failure,
  // 5/6 = timeout, 7/8 = disconnect, at registration/descriptor stages.
  for(int scenario=0; scenario<=8; ++scenario) {
    BleManager manager;
    BleManager::notifyOwner_=&manager;
    waits=calls=0; queue.full=false;
    syncFailureStage=scenario<=2 ? scenario : 0;
    onWait=[&] {
      BleDispatchGuard callbackGuard;
      const bool second=waits==2;
      if((scenario==5 && !second) || (scenario==6 && second)) return;
      esp_ble_gattc_cb_param_t param{};
      auto event=second ? ESP_GATTC_WRITE_DESCR_EVT : ESP_GATTC_REG_FOR_NOTIFY_EVT;
      if((scenario==7 && !second) || (scenario==8 && second)) {
        manager.state_.bleLinkAlive=false;
        manager.disconnectCallbackPending_=true;
        event=ESP_GATTC_DISCONNECT_EVT;
      }
      const int status=((scenario==3 && !second) || (scenario==4 && second))
                          ? ESP_GATT_ERROR : ESP_GATT_OK;
      param.reg_for_notify={7,status}; param.write={8,status};
      BleManager::afterGattEvent(event,2,&param);
    };
    assert(manager.subscribe(&characteristic)==(scenario==0));
    assert(!manager.subscriptionWaiting_ && lockDepth==0);
    if(scenario==1) assert(waits==0 && calls==1);
    if(scenario==2) assert(waits==1 && calls==2);
    if(scenario>=7) assert(manager.disconnectComplete_);
    // A failed attempt must not contaminate a subsequent subscription.
    manager.state_.bleLinkAlive=true; syncFailureStage=0;
    onWait=[&] {
      BleDispatchGuard callbackGuard;
      esp_ble_gattc_cb_param_t p{};
      p.reg_for_notify={7,ESP_GATT_OK}; p.write={8,ESP_GATT_OK};
      BleManager::afterGattEvent(manager.subscriptionEvent_,2,&p);
    };
    assert(manager.subscribe(&characteristic));
    esp_ble_gattc_cb_param_t p{}; p.notify.handle=7;
    BleManager::afterGattEvent(ESP_GATTC_NOTIFY_EVT,99,&p);
    assert(manager.notifications==0);
    BleManager::afterGattEvent(ESP_GATTC_NOTIFY_EVT,2,&p);
    assert(manager.notifications==1);
  }
  std::cout << "9 subscription scenarios and recovery passed\n";
}
