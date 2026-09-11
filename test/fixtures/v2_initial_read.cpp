#include <cassert>
#include <string>
#include <iostream>
#include "DgLabControl.h"

struct String:std::string {String(int n):std::string(std::to_string(n)) {}};
struct BLEUUID {explicit BLEUUID(const char*) {}};
const char *kServiceUuid2="", *kCharacteristicAUuid2="", *kCharacteristicBUuid2="", *kCharacteristicPwmUuid2="";
enum class DeviceType {DG2,DG3};
struct Characteristic {
  std::string value;
  int reads=0;
  bool canWrite(){return true;}
  bool canWriteNoResponse(){return true;}
  bool canNotify(){return true;}
  bool canRead(){return true;}
  std::string readValue(){++reads;return value;}
} characteristic;
struct Service {
  Characteristic* getCharacteristic(BLEUUID){return &characteristic;}
} service;
struct Controller {
  Characteristic *characteristicA2_=nullptr, *characteristicB2_=nullptr, *characteristicPwmAB2_=nullptr;
  struct {int strengthA=0,strengthB=0;} state_;
  struct {void add(const std::string&) {}} log_;
  bool subscribed=true;
  bool subscribe(Characteristic*) {return subscribed;}
  bool initialize() {
    auto getServiceWithRetry=[](BLEUUID){return &service;};
    DeviceType type=DeviceType::DG2;
    bool ok=false,initialStrengthConfirmed=false;
    // PRODUCTION_BRANCH
    return ok;
  }
};

int main(){
  Controller controller;
  characteristic.value="";
  assert(!controller.initialize());
  characteristic.value=std::string(2,'\0');
  assert(!controller.initialize());
  const uint32_t packed=(70u<<11)|140u;
  characteristic.value={char(packed),char(packed>>8),char(packed>>16)};
  assert(controller.initialize());
  assert(controller.state_.strengthA==70 && controller.state_.strengthB==140);
  controller.subscribed=false;
  const int reads=characteristic.reads;
  assert(!controller.initialize());
  assert(characteristic.reads==reads);
  std::cout << "V2 rejects empty/short reads and failed subscriptions; valid strengths preserved\n";
}
