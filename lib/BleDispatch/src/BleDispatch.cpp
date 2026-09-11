#include "BleDispatch.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {
SemaphoreHandle_t dispatchMutex() {
  static SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
  configASSERT(mutex);
  return mutex;
}
}

BleDispatchGuard::BleDispatchGuard() {
  xSemaphoreTake(dispatchMutex(), portMAX_DELAY);
}

BleDispatchGuard::~BleDispatchGuard() {
  xSemaphoreGive(dispatchMutex());
}
