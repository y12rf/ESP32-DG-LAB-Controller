#include "DgLabControl.h"

#include <string.h>

namespace dglab {

const WaveBlock kDisabledWave = {{10, 10, 10, 10, 0, 0, 0, 101}};

void encodeB0(uint8_t channel, uint8_t sequenceMethod, uint8_t strengthA,
              uint8_t strengthB, const WaveBlock& waveA,
              const WaveBlock& waveB, B0Frame& out) {
  (void)channel;
  out.bytes[0] = 0xB0;
  out.bytes[1] = sequenceMethod;
  out.bytes[2] = strengthA;
  out.bytes[3] = strengthB;
  memcpy(out.bytes + 4, waveA.bytes, kWaveBlockSize);
  memcpy(out.bytes + 12, waveB.bytes, kWaveBlockSize);
}

bool isWaveSendDue(uint32_t now, uint32_t lastSend) {
  return static_cast<uint32_t>(now - lastSend) >= kWaveIntervalMs;
}

StrengthController::StrengthController()
    : pendingA_{false, StrengthOperation::Increase, 0},
      pendingB_{false, StrengthOperation::Increase, 0}, strengthA_(0),
      strengthB_(0), sequence_(0), inFlightSequence_(0), waiting_(false),
      sentAt_(0), preparedA_{false, StrengthOperation::Increase, 0},
      preparedB_{false, StrengthOperation::Increase, 0}, hasPrepared_(false) {}

int StrengthController::clamp(int value) {
  if (value < 0) return 0;
  if (value > 200) return 200;
  return value;
}

bool StrengthController::elapsed(uint32_t now, uint32_t then, uint32_t interval) {
  return static_cast<uint32_t>(now - then) >= interval;
}

StrengthController::Intent& StrengthController::intent(Channel channel) {
  return channel == Channel::A ? pendingA_ : pendingB_;
}

const StrengthController::Intent& StrengthController::intent(Channel channel) const {
  return channel == Channel::A ? pendingA_ : pendingB_;
}

void StrengthController::merge(Intent& target, StrengthOperation operation, int value) {
  if (value < 0) value = 0;
  if (operation == StrengthOperation::Absolute) {
    target = {true, operation, clamp(value)};
    return;
  }
  if (!target.pending || target.operation == StrengthOperation::Absolute) {
    target = {true, operation, value};
    return;
  }
  if (target.operation == operation) {
    target.value += value;
  } else {
    target.value -= value;
    if (target.value < 0) {
      target.value = -target.value;
      target.operation = operation;
    }
  }
}

RequestDisposition StrengthController::requestStrength(Channel channel,
                                                        StrengthOperation operation,
                                                        int value, uint32_t) {
  if (value < 0) return RequestDisposition::Rejected;
  merge(intent(channel), operation, value);
  return waiting_ ? RequestDisposition::Queued : RequestDisposition::Prepared;
}

bool StrengthController::consume(Intent& target, int current, int& next,
                                 StrengthOperation& op, int& amount) {
  if (!target.pending) return false;
  op = target.operation;
  amount = target.value;
  if (op == StrengthOperation::Absolute) {
    next = clamp(amount);
    target.pending = false;
    return true;
  }
  amount = amount > 200 ? 200 : amount;
  next = op == StrengthOperation::Increase ? clamp(current + amount)
                                           : clamp(current - amount);
  target.value -= amount;
  if (target.value <= 0) target.pending = false;
  return true;
}

bool StrengthController::prepareCommand(uint32_t now, PreparedStrengthCommand& out) {
  (void)now;
  out = {0, strengthA_, strengthB_, false};
  if (waiting_) return false;
  Intent copyA = pendingA_;
  Intent copyB = pendingB_;
  int nextA = strengthA_, nextB = strengthB_, amount = 0;
  StrengthOperation opA = StrengthOperation::Absolute;
  StrengthOperation opB = StrengthOperation::Absolute;
  bool hasA = consume(copyA, strengthA_, nextA, opA, amount);
  bool hasB = consume(copyB, strengthB_, nextB, opB, amount);
  if (!hasA && !hasB) return false;
  preparedA_ = copyA;
  preparedB_ = copyB;
  hasPrepared_ = true;
  uint8_t method = 0;
  if (hasA) method |= opA == StrengthOperation::Increase ? 0x04 : opA == StrengthOperation::Decrease ? 0x08 : 0x0C;
  if (hasB) method |= opB == StrengthOperation::Increase ? 0x01 : opB == StrengthOperation::Decrease ? 0x02 : 0x03;
  uint8_t seq = static_cast<uint8_t>((sequence_ % 15) + 1);
  out = {static_cast<uint8_t>((seq << 4) | method), static_cast<uint8_t>(nextA),
         static_cast<uint8_t>(nextB), true};
  return true;
}

void StrengthController::commitPrepared(const PreparedStrengthCommand& command,
                                         uint32_t now) {
  if (!command.valid) return;
  if (hasPrepared_) {
    pendingA_ = preparedA_;
    pendingB_ = preparedB_;
    hasPrepared_ = false;
  }
  sequence_ = static_cast<uint8_t>(command.sequenceMethod >> 4);
  inFlightSequence_ = sequence_;
  strengthA_ = command.strengthA;
  strengthB_ = command.strengthB;
  waiting_ = true;
  sentAt_ = now;
}

void StrengthController::rollbackPrepared(const PreparedStrengthCommand& command) {
  if (!command.valid) return;
  hasPrepared_ = false;
  waiting_ = false;
  inFlightSequence_ = 0;
  hasPrepared_ = false;
}

void StrengthController::onStrengthResponse(uint8_t sequence, uint8_t currentA,
                                             uint8_t currentB, uint32_t) {
  strengthA_ = currentA;
  strengthB_ = currentB;
  if (waiting_ && sequence == inFlightSequence_) {
    waiting_ = false;
    inFlightSequence_ = 0;
  }
}

void StrengthController::tick(uint32_t now) {
  if (waiting_ && elapsed(now, sentAt_, kStrengthResponseTimeoutMs)) {
    waiting_ = false;
    inFlightSequence_ = 0;
  }
}

void StrengthController::resetConnection() {
  pendingA_ = {false, StrengthOperation::Increase, 0};
  pendingB_ = {false, StrengthOperation::Increase, 0};
  strengthA_ = strengthB_ = 0;
  sequence_ = inFlightSequence_ = 0;
  waiting_ = false;
  sentAt_ = 0;
  preparedA_ = {false, StrengthOperation::Increase, 0};
  preparedB_ = {false, StrengthOperation::Increase, 0};
  hasPrepared_ = false;
}

bool sameIdentity(const DeviceIdentity& lhs, const DeviceIdentity& rhs) {
  return lhs.addressType == rhs.addressType &&
         memcmp(lhs.address, rhs.address, sizeof(lhs.address)) == 0;
}

}  // namespace dglab
