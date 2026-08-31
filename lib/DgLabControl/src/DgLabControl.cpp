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

void decodeV2Strength(const uint8_t (&bytes)[3], uint16_t& strengthA,
                      uint16_t& strengthB) {
  const uint32_t data = (static_cast<uint32_t>(bytes[2]) << 16) |
                        (static_cast<uint32_t>(bytes[1]) << 8) | bytes[0];
  strengthA = static_cast<uint16_t>((data >> 11) & 0x7FF);
  strengthB = static_cast<uint16_t>(data & 0x7FF);
}

bool isWaveSendDue(uint32_t now, uint32_t lastSend) {
  return static_cast<uint32_t>(now - lastSend) >= kWaveIntervalMs;
}

bool prepareB0Cycle(uint32_t now, uint32_t lastSend,
                    StrengthController& controller, const WaveBlock& wave,
                    PreparedStrengthCommand& command, B0Frame& frame) {
  if (!isWaveSendDue(now, lastSend)) return false;

  command = {};
  if (controller.prepareCommand(now, command)) {
    encodeB0(2, command.sequenceMethod, command.strengthA, command.strengthB,
             wave, wave, frame);
  } else {
    encodeB0(2, 0, 0, 0, wave, wave, frame);
  }
  return true;
}

StrengthController::StrengthController()
    : pendingA_{false, StrengthOperation::Increase, 0},
      pendingB_{false, StrengthOperation::Increase, 0}, strengthA_(0),
      strengthB_(0), sequence_(0), inFlightSequence_(0), waiting_(false),
      feedbackSynchronized_(false),
      sentAt_(0), preparedA_{false, StrengthOperation::Increase, 0},
      preparedB_{false, StrengthOperation::Increase, 0},
      preparedRemainderA_{false, StrengthOperation::Increase, 0},
      preparedRemainderB_{false, StrengthOperation::Increase, 0},
      hasPrepared_(false) {}

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
  if (operation != StrengthOperation::Absolute && value == 0) return;
  if (operation == StrengthOperation::Absolute) {
    target = {true, operation, clamp(value)};
    return;
  }
  if (!target.pending) {
    target = {true, operation, value};
    return;
  }
  if (target.operation == StrengthOperation::Absolute) {
    target.value = clamp(target.value +
                         (operation == StrengthOperation::Increase ? value : -value));
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
  if (target.value == 0) target.pending = false;
}

RequestDisposition StrengthController::requestStrength(Channel channel,
                                                        StrengthOperation operation,
                                                        int value, uint32_t) {
  if (value < 0) return RequestDisposition::Rejected;
  merge(intent(channel), operation, value);
  return waiting_ || hasPrepared_ ? RequestDisposition::Queued
                                  : RequestDisposition::Prepared;
}

bool StrengthController::consume(Intent& target, int current, int& next,
                                 StrengthOperation& op, int& amount) {
  if (!target.pending) return false;
  op = target.operation;
  amount = target.value;
  if (op == StrengthOperation::Absolute) {
    next = clamp(amount);
    amount = next;
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
  out = {0, 0, 0, strengthA_, strengthB_, false};
  if (waiting_ || hasPrepared_) return false;
  Intent originalA = pendingA_;
  Intent originalB = pendingB_;
  Intent copyA = pendingA_;
  Intent copyB = pendingB_;
  int nextA = strengthA_, nextB = strengthB_, amountA = 0, amountB = 0;
  StrengthOperation opA = StrengthOperation::Absolute;
  StrengthOperation opB = StrengthOperation::Absolute;
  bool hasA = consume(copyA, strengthA_, nextA, opA, amountA);
  bool hasB = consume(copyB, strengthB_, nextB, opB, amountB);
  if (!hasA && !hasB) return false;
  pendingA_ = {false, StrengthOperation::Increase, 0};
  pendingB_ = {false, StrengthOperation::Increase, 0};
  preparedA_ = originalA;
  preparedB_ = originalB;
  preparedRemainderA_ = copyA;
  preparedRemainderB_ = copyB;
  hasPrepared_ = true;
  uint8_t method = 0;
  if (hasA) method |= opA == StrengthOperation::Increase ? 0x04 : opA == StrengthOperation::Decrease ? 0x08 : 0x0C;
  if (hasB) method |= opB == StrengthOperation::Increase ? 0x01 : opB == StrengthOperation::Decrease ? 0x02 : 0x03;
  uint8_t seq = static_cast<uint8_t>((sequence_ % 15) + 1);
  out = {static_cast<uint8_t>((seq << 4) | method),
         static_cast<uint8_t>(hasA && opA != StrengthOperation::Absolute ? amountA : hasA ? nextA : 0),
         static_cast<uint8_t>(hasB && opB != StrengthOperation::Absolute ? amountB : hasB ? nextB : 0),
         static_cast<uint8_t>(nextA), static_cast<uint8_t>(nextB), true};
  return true;
}

void StrengthController::commitPrepared(const PreparedStrengthCommand& command,
                                         uint32_t now) {
  if (!command.valid) return;
  if (hasPrepared_) {
    Intent queuedA = pendingA_;
    Intent queuedB = pendingB_;
    pendingA_ = preparedRemainderA_;
    pendingB_ = preparedRemainderB_;
    if (queuedA.pending) merge(pendingA_, queuedA.operation, queuedA.value);
    if (queuedB.pending) merge(pendingB_, queuedB.operation, queuedB.value);
    hasPrepared_ = false;
  }
  sequence_ = static_cast<uint8_t>(command.sequenceMethod >> 4);
  inFlightSequence_ = sequence_;
  strengthA_ = command.targetStrengthA;
  strengthB_ = command.targetStrengthB;
  waiting_ = true;
  feedbackSynchronized_ = false;
  sentAt_ = now;
}

void StrengthController::rollbackPrepared(const PreparedStrengthCommand& command) {
  if (!command.valid) return;
  if (hasPrepared_) {
    Intent queuedA = pendingA_;
    Intent queuedB = pendingB_;
    pendingA_ = preparedA_;
    pendingB_ = preparedB_;
    if (queuedA.pending) merge(pendingA_, queuedA.operation, queuedA.value);
    if (queuedB.pending) merge(pendingB_, queuedB.operation, queuedB.value);
    hasPrepared_ = false;
  }
}

bool StrengthController::onStrengthResponse(uint8_t sequence, uint8_t currentA,
                                             uint8_t currentB, uint32_t) {
  strengthA_ = currentA;
  strengthB_ = currentB;
  if (waiting_ && sequence == inFlightSequence_) {
    waiting_ = false;
    inFlightSequence_ = 0;
    feedbackSynchronized_ = true;
    return true;
  }
  if (!waiting_) feedbackSynchronized_ = true;
  return false;
}

bool StrengthController::tick(uint32_t now) {
  if (waiting_ && elapsed(now, sentAt_, kStrengthResponseTimeoutMs)) {
    waiting_ = false;
    inFlightSequence_ = 0;
    feedbackSynchronized_ = false;
    return true;
  }
  return false;
}

void StrengthController::resetConnection() {
  pendingA_ = {false, StrengthOperation::Increase, 0};
  pendingB_ = {false, StrengthOperation::Increase, 0};
  strengthA_ = strengthB_ = 0;
  sequence_ = inFlightSequence_ = 0;
  waiting_ = false;
  feedbackSynchronized_ = false;
  sentAt_ = 0;
  preparedA_ = {false, StrengthOperation::Increase, 0};
  preparedB_ = {false, StrengthOperation::Increase, 0};
  preparedRemainderA_ = {false, StrengthOperation::Increase, 0};
  preparedRemainderB_ = {false, StrengthOperation::Increase, 0};
  hasPrepared_ = false;
}

bool sameIdentity(const DeviceIdentity& lhs, const DeviceIdentity& rhs) {
  return lhs.addressType == rhs.addressType &&
         memcmp(lhs.address, rhs.address, sizeof(lhs.address)) == 0;
}

}  // namespace dglab
