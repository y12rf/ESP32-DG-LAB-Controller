#pragma once

#include <stdint.h>
#include <stddef.h>

namespace dglab {

constexpr size_t kWaveBlockSize = 8;
constexpr size_t kB0FrameSize = 20;
constexpr uint32_t kWaveIntervalMs = 100;
constexpr uint32_t kStrengthResponseTimeoutMs = 500;

struct WaveBlock {
  uint8_t bytes[kWaveBlockSize];
};

struct B0Frame {
  uint8_t bytes[kB0FrameSize];
};

extern const WaveBlock kDisabledWave;

void encodeB0(uint8_t channel, uint8_t sequenceMethod, uint8_t strengthA,
              uint8_t strengthB, const WaveBlock& waveA,
              const WaveBlock& waveB, B0Frame& out);

bool isWaveSendDue(uint32_t now, uint32_t lastSend);

enum class Channel : uint8_t { A = 0, B = 1 };
enum class StrengthOperation : uint8_t { Increase, Decrease, Absolute };
enum class RequestDisposition : uint8_t { Rejected, Queued, Prepared };

struct PreparedStrengthCommand {
  uint8_t sequenceMethod;
  uint8_t strengthA;
  uint8_t strengthB;
  bool valid;
};

class StrengthController {
 public:
  StrengthController();
  RequestDisposition requestStrength(Channel channel, StrengthOperation operation,
                                     int value, uint32_t now);
  bool prepareCommand(uint32_t now, PreparedStrengthCommand& out);
  void commitPrepared(const PreparedStrengthCommand& command, uint32_t now);
  void rollbackPrepared(const PreparedStrengthCommand& command);
  void onStrengthResponse(uint8_t sequence, uint8_t currentA, uint8_t currentB,
                          uint32_t now);
  void tick(uint32_t now);
  void resetConnection();
  uint8_t strengthA() const { return strengthA_; }
  uint8_t strengthB() const { return strengthB_; }
  bool waitingForResponse() const { return waiting_; }

 private:
  struct Intent {
    bool pending;
    StrengthOperation operation;
    int value;
  };
  Intent pendingA_;
  Intent pendingB_;
  uint8_t strengthA_;
  uint8_t strengthB_;
  uint8_t sequence_;
  uint8_t inFlightSequence_;
  bool waiting_;
  uint32_t sentAt_;
  Intent preparedA_;
  Intent preparedB_;
  bool hasPrepared_;

  static int clamp(int value);
  static bool elapsed(uint32_t now, uint32_t then, uint32_t interval);
  Intent& intent(Channel channel);
  const Intent& intent(Channel channel) const;
  void merge(Intent& target, StrengthOperation operation, int value);
  bool consume(Intent& target, int current, int& next, StrengthOperation& op,
               int& amount);
};

struct DeviceIdentity {
  uint8_t address[6];
  uint8_t addressType;
};

bool sameIdentity(const DeviceIdentity& lhs, const DeviceIdentity& rhs);

class ResumePolicy {
 public:
  ResumePolicy() : desiredSending_(false), hasIdentity_(false) {}
  void setDesiredSending(bool value) { desiredSending_ = value; }
  bool desiredSending() const { return desiredSending_; }
  void remember(const DeviceIdentity& identity) {
    identity_ = identity;
    hasIdentity_ = true;
  }
  void clearIdentity() { hasIdentity_ = false; }
  bool shouldRestore(const DeviceIdentity& identity) const {
    return desiredSending_ && hasIdentity_ && sameIdentity(identity_, identity);
  }

 private:
  bool desiredSending_;
  bool hasIdentity_;
  DeviceIdentity identity_;
};

}  // namespace dglab
