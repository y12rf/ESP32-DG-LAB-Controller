#pragma once

#include "AppLog.h"
#include "AppState.h"
#include "BleManager.h"
#include <DgLabControl.h>
#include <vector>

class OutputController {
 public:
  OutputController(AppState& state, AppLog& log, BleManager& ble);
  void onManualConnectionAttempt();
  void onConnected(bool manualSelection);
  void onDisconnected(bool manualDisconnect);
  void onV2StrengthFeedback(const BleEvent& event);
  void onStrengthResponse(const BleEvent& event);
  dglab::RequestDisposition adjustStrength(char channel, int value,
                                           uint8_t method);
  void startSending();
  void stopSending();
  void selectWave(char wave);
  void handleWaveSend();

 private:
  AppState& state_;
  AppLog& log_;
  BleManager& ble_;
  dglab::StrengthController strengthController_;
  dglab::ResumePolicy resumePolicy_;

  std::vector<uint8_t> hexToBytes(const String& hex);
  dglab::WaveBlock currentWaveBlock();
  bool setStrengthV2(int channelA, int channelB);
  dglab::RequestDisposition adjustStrengthA(int value, uint8_t method);
  dglab::RequestDisposition adjustStrengthB(int value, uint8_t method);
  bool sendWaveV2(const String& hexA, const String& hexB);
};
