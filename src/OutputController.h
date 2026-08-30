#pragma once

#include "AppLog.h"
#include "AppState.h"
#include "BleManager.h"
#include <DgLabControl.h>
#include <vector>

class OutputController {
 public:
  OutputController(AppState& state, AppLog& log, BleManager& ble);
  void onConnected(bool manualSelection);
  void onDisconnected(bool manualDisconnect);
  void onStrengthResponse(const BleEvent& event);
  bool adjustStrength(char channel, int value, uint8_t method);
  void startSending();
  void stopSending();
  void selectWave(char wave);
  void drainStrengthCommand();
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
  bool adjustStrengthA(int value, uint8_t method);
  bool adjustStrengthB(int value, uint8_t method);
  bool sendWaveV2(const String& hexA, const String& hexB);
};
