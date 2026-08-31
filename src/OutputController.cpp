#include "OutputController.h"

#include "Waveforms.h"
#include <algorithm>
#include <cstdlib>

using dglab::B0Frame;
using dglab::Channel;
using dglab::PreparedStrengthCommand;
using dglab::StrengthOperation;
using dglab::WaveBlock;

OutputController::OutputController(AppState& state, AppLog& log, BleManager& ble)
    : state_(state), log_(log), ble_(ble) {}

std::vector<uint8_t> OutputController::hexToBytes(const String& hex) {
  std::vector<uint8_t> v;
  if (hex.length() % 2 != 0) {
    log_.add("hexToBytes: 无效长度 " + hex);
    return v;
  }

  for (size_t i = 0; i < hex.length(); i += 2) {
    String part = hex.substring(i, i + 2);
    char* endPtr = nullptr;
    long value = strtol(part.c_str(), &endPtr, 16);
    if (endPtr == nullptr || *endPtr != '\0' || value < 0 || value > 0xFF) {
      log_.add("hexToBytes: 无效数据 " + part);
      v.clear();
      return v;
    }
    v.push_back(static_cast<uint8_t>(value));
  }
  return v;
}

bool OutputController::sendWaveV2(const String& hexA, const String& hexB) {
  if (!state_.deviceConnected || state_.deviceType != DeviceType::DG2) return false;

  std::vector<uint8_t> bytesA = hexToBytes(hexA);
  if (hexA.length() > 0 && bytesA.empty()) return false;
  std::vector<uint8_t> bytesB = hexToBytes(hexB);
  if (hexB.length() > 0 && bytesB.empty()) return false;
  return ble_.writeV2WaveBytes(bytesA, bytesB);
}

bool OutputController::setStrengthV2(int channelA, int channelB) {
  if (!state_.deviceConnected || state_.deviceType != DeviceType::DG2) {
    log_.add("设备未连接或非2.0设备");
    return false;
  }
  channelA = constrain(channelA, 0, 2047);
  channelB = constrain(channelB, 0, 2047);

  uint32_t value = ((channelA & 0x7FF) << 11) | (channelB & 0x7FF);
  uint8_t data[3] = { uint8_t(value & 0xFF),
                      uint8_t((value >> 8) & 0xFF),
                      uint8_t((value >> 16) & 0xFF) };

  if (!ble_.hasV2StrengthCharacteristic()) {
    log_.add("PWM_AB2 特性获取失败");
    return false;
  }
  bool success = ble_.writeV2StrengthBytes(data);

  if (success) {
    state_.strengthA = channelA;
    state_.strengthB = channelB;
    log_.add("设置2.0强度: A=" + String(channelA) + ", B=" + String(channelB));
  } else {
    log_.add("设置2.0强度失败");
  }
  return success;
}

dglab::RequestDisposition OutputController::adjustStrengthA(int value,
                                                            uint8_t method) {
  if (!state_.deviceConnected || value < 0) {
    return dglab::RequestDisposition::Rejected;
  }

  if (state_.deviceType == DeviceType::DG3) {  // 3.0
    StrengthOperation op;
    if (method == 0x04) op = StrengthOperation::Increase;
    else if (method == 0x08) op = StrengthOperation::Decrease;
    else if (method == 0x0C) op = StrengthOperation::Absolute;
    else return dglab::RequestDisposition::Rejected;
    return strengthController_.requestStrength(Channel::A, op, value, millis());
  } else {  // 2.0
    int newA = state_.strengthA;
    if (method == 0x04) newA = min(2047, state_.strengthA + value * 7);
    else if (method == 0x08) newA = max(0, state_.strengthA - value * 7);
    else if (method == 0x0C) newA = constrain(value * 7, 0, 2047);
    else return dglab::RequestDisposition::Rejected;
    return setStrengthV2(newA, state_.strengthB)
               ? dglab::RequestDisposition::Prepared
               : dglab::RequestDisposition::Rejected;
  }
}

dglab::RequestDisposition OutputController::adjustStrengthB(int value,
                                                              uint8_t method) {
  if (!state_.deviceConnected || value < 0) {
    return dglab::RequestDisposition::Rejected;
  }

  if (state_.deviceType == DeviceType::DG3) {  // 3.0
    StrengthOperation op;
    if (method == 0x01) op = StrengthOperation::Increase;
    else if (method == 0x02) op = StrengthOperation::Decrease;
    else if (method == 0x03) op = StrengthOperation::Absolute;
    else return dglab::RequestDisposition::Rejected;
    return strengthController_.requestStrength(Channel::B, op, value, millis());
  } else {  // 2.0
    int newB = state_.strengthB;
    if (method == 0x01) newB = min(2047, state_.strengthB + value * 7);
    else if (method == 0x02) newB = max(0, state_.strengthB - value * 7);
    else if (method == 0x03) newB = constrain(value * 7, 0, 2047);
    else return dglab::RequestDisposition::Rejected;
    return setStrengthV2(state_.strengthA, newB)
               ? dglab::RequestDisposition::Prepared
               : dglab::RequestDisposition::Rejected;
  }
}

dglab::WaveBlock OutputController::currentWaveBlock() {
  WaveBlock block = {{10, 10, 10, 10, 0, 0, 0, 101}};
  const char* current = waveforms::current(state_.deviceType, state_.selectedWave,
                                            state_.waveIndex);
  std::vector<uint8_t> bytes = hexToBytes(String(current));
  if (bytes.size() >= 8) std::copy(bytes.begin(), bytes.begin() + 8, block.bytes);
  return block;
}

void OutputController::handleWaveSend() {
  if (!state_.deviceConnected || !state_.linkReady.load(std::memory_order_acquire)) return;

  const uint32_t now = millis();
  if (state_.deviceType == DeviceType::DG3) {
    if (strengthController_.tick(now)) {
      state_.strengthConfirmed = false;
      log_.add("B1 回应超时，强度状态未确认");
    }
    state_.waitingForResponse = strengthController_.waitingForResponse();
    state_.isInputAllowed = !state_.waitingForResponse;
  }

  if (state_.deviceType == DeviceType::DG2) {
    if (!state_.isSending || !dglab::isWaveSendDue(now, state_.lastSendTime)) return;
    state_.lastSendTime = now;
    const char* data = waveforms::current(state_.deviceType, state_.selectedWave,
                                           state_.waveIndex);
    if (!sendWaveV2(data, data)) {
      state_.isSending = false;
      log_.add("波形发送失败");
    } else if (state_.waveIndex % 100 == 0) {
      log_.add("波形发送 index=" + String(state_.waveIndex));
    }
    ++state_.waveIndex;
    return;
  }

  const WaveBlock wave = state_.isSending ? currentWaveBlock() : dglab::kDisabledWave;
  PreparedStrengthCommand command = {};
  B0Frame frame = {};
  if (!dglab::prepareB0Cycle(now, state_.lastSendTime, strengthController_,
                             wave, command, frame)) {
    return;
  }
  state_.lastSendTime = now;
  const bool success = ble_.writeV3Frame(frame);
  if (success) {
    if (command.valid) {
      strengthController_.commitPrepared(command, now);
      state_.strengthA = command.targetStrengthA;
      state_.strengthB = command.targetStrengthB;
      state_.strengthConfirmed = false;
      state_.orderNo = static_cast<uint8_t>(command.sequenceMethod >> 4);
      state_.waitingForResponse = true;
      state_.isInputAllowed = false;
      log_.add("已发送强度指令");
    }
  } else {
    if (command.valid) {
      strengthController_.rollbackPrepared(command);
      log_.add("强度指令写入失败");
    }
    if (state_.isSending) {
      state_.isSending = false;
      log_.add("波形发送失败");
    } else {
      state_.linkReady.store(false, std::memory_order_release);
    }
  }
  if (state_.isSending) {
    if (success && state_.waveIndex % 100 == 0) {
      log_.add("波形发送 index=" + String(state_.waveIndex));
    }
    ++state_.waveIndex;
  }
}

void OutputController::onConnected(bool manualSelection) {
  if (state_.deviceType == DeviceType::DG3) {
    state_.strengthA = 0;
    state_.strengthB = 0;
  }
  state_.strengthConfirmed = (state_.deviceType == DeviceType::DG2);
  if (state_.connectedIdentityValid) resumePolicy_.remember(state_.connectedIdentity);
  if (manualSelection) {
    state_.desiredSending = false;
    resumePolicy_.setDesiredSending(false);
    state_.isSending = false;
  } else {
    state_.isSending = state_.connectedIdentityValid &&
                       resumePolicy_.shouldRestore(state_.connectedIdentity);
  }
  state_.desiredSending = state_.isSending;
  resumePolicy_.setDesiredSending(state_.desiredSending);
  if (state_.deviceType == DeviceType::DG3) strengthController_.resetConnection();
  state_.orderNo = 0;
  state_.isInputAllowed = true;
  state_.waitingForResponse = false;
}

void OutputController::onDisconnected(bool manualDisconnect) {
  if (manualDisconnect) {
    resumePolicy_.clearIdentity();
    state_.desiredSending = false;
    resumePolicy_.setDesiredSending(false);
  } else {
    state_.isSending = state_.desiredSending;
  }
  strengthController_.resetConnection();
  state_.strengthA = 0;
  state_.strengthB = 0;
  state_.strengthConfirmed = false;
  state_.waitingForResponse = false;
  state_.isInputAllowed = true;
}

void OutputController::onStrengthResponse(const BleEvent& event) {
  strengthController_.onStrengthResponse(event.sequence, event.strengthA, event.strengthB, millis());
  state_.strengthA = strengthController_.strengthA();
  state_.strengthB = strengthController_.strengthB();
  state_.strengthConfirmed = strengthController_.feedbackSynchronized();
  state_.waitingForResponse = strengthController_.waitingForResponse();
  state_.isInputAllowed = !state_.waitingForResponse;
  log_.add("收到强度回应: 序列号=" + String(event.sequence) + ", A=" + String(state_.strengthA) + ", B=" + String(state_.strengthB));
}

dglab::RequestDisposition OutputController::adjustStrength(char channel,
                                                            int value,
                                                            uint8_t method) {
  if (channel != 'a' && channel != 'b') {
    return dglab::RequestDisposition::Rejected;
  }
  return channel == 'a' ? adjustStrengthA(value, method)
                        : adjustStrengthB(value, method);
}

void OutputController::startSending() {
  if (state_.deviceConnected) {
    state_.isSending = true;
    state_.desiredSending = true;
    resumePolicy_.setDesiredSending(true);
    state_.waveIndex = 0;
    log_.add("开始发送");
  }
}

void OutputController::stopSending() {
  state_.isSending = false;
  state_.desiredSending = false;
  resumePolicy_.setDesiredSending(false);
  log_.add("停止发送");
}

void OutputController::selectWave(char wave) {
  state_.selectedWave = wave;
  log_.add("切换波形 " + String(state_.selectedWave));
}
