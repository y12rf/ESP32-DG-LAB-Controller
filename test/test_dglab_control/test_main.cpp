#include <unity.h>
#include <DgLabControl.h>
#include "../../src/Waveforms.h"

using namespace dglab;

void test_builtin_v2_wave_tables_keep_sizes_and_rotation() {
  const waveforms::V2WaveBlock& first = waveforms::currentV2('a', 0);
  const waveforms::V2WaveBlock& wrapped = waveforms::currentV2('a', 12);
  const waveforms::V2WaveBlock& last = waveforms::currentV2('a', 11);

  TEST_ASSERT_EQUAL_UINT8(0x21, first.bytes[0]);
  TEST_ASSERT_EQUAL_UINT8(0x01, first.bytes[1]);
  TEST_ASSERT_EQUAL_UINT8(0x00, first.bytes[2]);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(first.bytes, wrapped.bytes, 3);
  TEST_ASSERT_EQUAL_UINT8(0x00, last.bytes[0]);
  TEST_ASSERT_EQUAL_UINT8(0x00, last.bytes[1]);
  TEST_ASSERT_EQUAL_UINT8(0x00, last.bytes[2]);
}

void test_builtin_v3_wave_tables_keep_bytes_and_invalid_wave_fallback() {
  const WaveBlock& first = waveforms::currentV3('a', 0);
  const WaveBlock& last = waveforms::currentV3('a', 11);
  const WaveBlock& wrapped = waveforms::currentV3('a', 12);
  const WaveBlock& invalid = waveforms::currentV3('x', 0);

  TEST_ASSERT_EQUAL_UINT8(0x0A, first.bytes[0]);
  TEST_ASSERT_EQUAL_UINT8(0x0A, first.bytes[3]);
  TEST_ASSERT_EQUAL_UINT8(0x00, first.bytes[4]);
  TEST_ASSERT_EQUAL_UINT8(0x00, first.bytes[7]);
  TEST_ASSERT_EQUAL_UINT8(0x00, last.bytes[4]);
  TEST_ASSERT_EQUAL_UINT8(0x00, last.bytes[7]);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(first.bytes, wrapped.bytes, 8);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kDisabledWave.bytes, invalid.bytes, 8);
}

static void synchronize_at_100(StrengthController& controller);

void test_b0_layout_is_exactly_twenty_bytes() {
  const WaveBlock wave = {{10, 11, 12, 13, 1, 2, 3, 4}};
  B0Frame frame = {};
  encodeB0(2, 0x06, 5, 8, wave, wave, frame);
  TEST_ASSERT_EQUAL_UINT32(20, sizeof(frame.bytes));
  TEST_ASSERT_EQUAL_HEX8(0xB0, frame.bytes[0]);
  TEST_ASSERT_EQUAL_HEX8(0x06, frame.bytes[1]);
  TEST_ASSERT_EQUAL_UINT8(5, frame.bytes[2]);
  TEST_ASSERT_EQUAL_UINT8(8, frame.bytes[3]);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(wave.bytes, frame.bytes + 4, 8);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(wave.bytes, frame.bytes + 12, 8);
}

void test_disabled_wave_is_safe_marker() {
  TEST_ASSERT_EQUAL_UINT8(10, kDisabledWave.bytes[0]);
  TEST_ASSERT_EQUAL_UINT8(101, kDisabledWave.bytes[7]);
}

void test_v2_strength_decode_uses_little_endian_11_bit_channels() {
  const uint8_t bytes[3] = {0x01, 0x02, 0x03};
  uint16_t strengthA = 0;
  uint16_t strengthB = 0;

  decodeV2Strength(bytes, strengthA, strengthB);

  TEST_ASSERT_EQUAL_UINT16(96, strengthA);
  TEST_ASSERT_EQUAL_UINT16(513, strengthB);
}

void test_wave_schedule_handles_rollover() {
  TEST_ASSERT_FALSE(isWaveSendDue(0xFFFFFFC0u, 0xFFFFFFC0u));
  TEST_ASSERT_TRUE(isWaveSendDue(50u, 0xFFFFFFC0u));
}

void test_b0_cycle_waits_until_100ms_without_consuming_pending_strength() {
  StrengthController controller;
  controller.requestStrength(Channel::A, StrengthOperation::Increase, 5, 0);
  const WaveBlock wave = {{1, 2, 3, 4, 5, 6, 7, 8}};
  PreparedStrengthCommand command = {};
  B0Frame frame = {};

  TEST_ASSERT_FALSE(
      prepareB0Cycle(99, 0, controller, wave, command, frame));
  TEST_ASSERT_TRUE(
      prepareB0Cycle(100, 0, controller, wave, command, frame));
  TEST_ASSERT_TRUE(command.valid);
  TEST_ASSERT_EQUAL_UINT8(5, frame.bytes[2]);
}

void test_b0_cycle_puts_pending_strength_and_both_waves_in_one_frame() {
  StrengthController controller;
  synchronize_at_100(controller);
  controller.requestStrength(Channel::A, StrengthOperation::Increase, 5, 2);
  const WaveBlock wave = {{11, 22, 33, 44, 55, 66, 77, 88}};
  PreparedStrengthCommand command = {};
  B0Frame frame = {};

  TEST_ASSERT_TRUE(
      prepareB0Cycle(100, 0, controller, wave, command, frame));
  TEST_ASSERT_TRUE(command.valid);
  TEST_ASSERT_EQUAL_HEX8(0x24, frame.bytes[1]);
  TEST_ASSERT_EQUAL_UINT8(5, frame.bytes[2]);
  TEST_ASSERT_EQUAL_UINT8(0, frame.bytes[3]);
  TEST_ASSERT_EQUAL_UINT8(105, command.targetStrengthA);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(wave.bytes, frame.bytes + 4, kWaveBlockSize);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(wave.bytes, frame.bytes + 12, kWaveBlockSize);
}

void test_b0_cycle_sends_wave_only_while_strength_command_waits_for_b1() {
  StrengthController controller;
  controller.requestStrength(Channel::A, StrengthOperation::Increase, 5, 0);
  const WaveBlock wave = {{9, 8, 7, 6, 5, 4, 3, 2}};
  PreparedStrengthCommand command = {};
  B0Frame frame = {};

  TEST_ASSERT_TRUE(
      prepareB0Cycle(100, 0, controller, wave, command, frame));
  controller.commitPrepared(command, 100);

  command = {};
  frame = {};
  TEST_ASSERT_TRUE(
      prepareB0Cycle(200, 100, controller, wave, command, frame));
  TEST_ASSERT_FALSE(command.valid);
  TEST_ASSERT_EQUAL_HEX8(0, frame.bytes[1]);
  TEST_ASSERT_EQUAL_UINT8(0, frame.bytes[2]);
  TEST_ASSERT_EQUAL_UINT8(0, frame.bytes[3]);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(wave.bytes, frame.bytes + 4, kWaveBlockSize);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(wave.bytes, frame.bytes + 12, kWaveBlockSize);
}

void test_b0_cycle_disables_both_wave_channels_with_disabled_wave() {
  StrengthController controller;
  controller.requestStrength(Channel::A, StrengthOperation::Increase, 5, 0);
  PreparedStrengthCommand command = {};
  B0Frame frame = {};

  TEST_ASSERT_TRUE(
      prepareB0Cycle(100, 0, controller, kDisabledWave, command, frame));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kDisabledWave.bytes, frame.bytes + 4,
                                kWaveBlockSize);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kDisabledWave.bytes, frame.bytes + 12,
                                kWaveBlockSize);
}

void test_relative_strength_keeps_raw_delta() {
  StrengthController controller;
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RequestDisposition::Prepared), static_cast<uint8_t>(controller.requestStrength(Channel::A, StrengthOperation::Increase, 1, 0)));
  PreparedStrengthCommand command = {};
  TEST_ASSERT_TRUE(controller.prepareCommand(0, command));
  TEST_ASSERT_EQUAL_HEX8(0x14, command.sequenceMethod);
  TEST_ASSERT_EQUAL_UINT8(1, command.strengthA);
}

static void synchronize_at_100(StrengthController& controller) {
  controller.requestStrength(Channel::A, StrengthOperation::Absolute, 100, 0);
  controller.requestStrength(Channel::B, StrengthOperation::Absolute, 100, 0);
  PreparedStrengthCommand command = {};
  TEST_ASSERT_TRUE(controller.prepareCommand(0, command));
  controller.commitPrepared(command, 0);
  TEST_ASSERT_TRUE(controller.onStrengthResponse(1, 100, 100, 1));
  TEST_ASSERT_TRUE(controller.feedbackSynchronized());
}

void test_relative_increase_encodes_magnitude_and_predicts_target_for_a() {
  StrengthController controller;
  synchronize_at_100(controller);
  controller.requestStrength(Channel::A, StrengthOperation::Increase, 5, 2);

  PreparedStrengthCommand command = {};
  TEST_ASSERT_TRUE(controller.prepareCommand(2, command));
  TEST_ASSERT_EQUAL_UINT8(5, command.strengthA);
  TEST_ASSERT_EQUAL_UINT8(0, command.strengthB);
  TEST_ASSERT_EQUAL_UINT8(105, command.targetStrengthA);
}

void test_relative_decrease_encodes_magnitude_and_predicts_target_for_a() {
  StrengthController controller;
  synchronize_at_100(controller);
  controller.requestStrength(Channel::A, StrengthOperation::Decrease, 5, 2);

  PreparedStrengthCommand command = {};
  TEST_ASSERT_TRUE(controller.prepareCommand(2, command));
  TEST_ASSERT_EQUAL_UINT8(5, command.strengthA);
  TEST_ASSERT_EQUAL_UINT8(0, command.strengthB);
  TEST_ASSERT_EQUAL_UINT8(95, command.targetStrengthA);
}

void test_relative_increase_and_decrease_encode_magnitude_and_target_for_b() {
  StrengthController controller;
  synchronize_at_100(controller);
  controller.requestStrength(Channel::B, StrengthOperation::Increase, 5, 2);

  PreparedStrengthCommand command = {};
  TEST_ASSERT_TRUE(controller.prepareCommand(2, command));
  TEST_ASSERT_EQUAL_UINT8(0, command.strengthA);
  TEST_ASSERT_EQUAL_UINT8(5, command.strengthB);
  TEST_ASSERT_EQUAL_UINT8(105, command.targetStrengthB);

  StrengthController decrease;
  synchronize_at_100(decrease);
  decrease.requestStrength(Channel::B, StrengthOperation::Decrease, 5, 2);
  TEST_ASSERT_TRUE(decrease.prepareCommand(2, command));
  TEST_ASSERT_EQUAL_UINT8(0, command.strengthA);
  TEST_ASSERT_EQUAL_UINT8(5, command.strengthB);
  TEST_ASSERT_EQUAL_UINT8(95, command.targetStrengthB);
}

void test_a_and_b_relative_intents_share_frame_with_independent_targets() {
  StrengthController controller;
  controller.requestStrength(Channel::A, StrengthOperation::Absolute, 100, 0);
  controller.requestStrength(Channel::B, StrengthOperation::Absolute, 100, 0);
  PreparedStrengthCommand command = {};
  TEST_ASSERT_TRUE(controller.prepareCommand(0, command));
  controller.commitPrepared(command, 0);
  TEST_ASSERT_TRUE(controller.onStrengthResponse(1, 100, 100, 1));

  controller.requestStrength(Channel::A, StrengthOperation::Increase, 5, 2);
  controller.requestStrength(Channel::B, StrengthOperation::Decrease, 20, 2);
  TEST_ASSERT_TRUE(controller.prepareCommand(2, command));
  TEST_ASSERT_EQUAL_HEX8(0x26, command.sequenceMethod);
  TEST_ASSERT_EQUAL_UINT8(5, command.strengthA);
  TEST_ASSERT_EQUAL_UINT8(20, command.strengthB);
  TEST_ASSERT_EQUAL_UINT8(105, command.targetStrengthA);
  TEST_ASSERT_EQUAL_UINT8(80, command.targetStrengthB);
}

void test_absolute_target_is_adjusted_by_relative_intents_and_clamped() {
  StrengthController increase;
  increase.requestStrength(Channel::A, StrengthOperation::Absolute, 100, 0);
  increase.requestStrength(Channel::A, StrengthOperation::Increase, 5, 0);
  PreparedStrengthCommand command = {};
  TEST_ASSERT_TRUE(increase.prepareCommand(0, command));
  TEST_ASSERT_EQUAL_HEX8(0x1C, command.sequenceMethod);
  TEST_ASSERT_EQUAL_UINT8(105, command.strengthA);
  TEST_ASSERT_EQUAL_UINT8(105, command.targetStrengthA);

  StrengthController decrease;
  decrease.requestStrength(Channel::A, StrengthOperation::Absolute, 100, 0);
  decrease.requestStrength(Channel::A, StrengthOperation::Decrease, 20, 0);
  TEST_ASSERT_TRUE(decrease.prepareCommand(0, command));
  TEST_ASSERT_EQUAL_HEX8(0x1C, command.sequenceMethod);
  TEST_ASSERT_EQUAL_UINT8(80, command.strengthA);
  TEST_ASSERT_EQUAL_UINT8(80, command.targetStrengthA);

  StrengthController upper;
  upper.requestStrength(Channel::A, StrengthOperation::Absolute, 195, 0);
  upper.requestStrength(Channel::A, StrengthOperation::Increase, 20, 0);
  TEST_ASSERT_TRUE(upper.prepareCommand(0, command));
  TEST_ASSERT_EQUAL_UINT8(200, command.strengthA);
  TEST_ASSERT_EQUAL_UINT8(200, command.targetStrengthA);

  StrengthController lower;
  lower.requestStrength(Channel::A, StrengthOperation::Absolute, 5, 0);
  lower.requestStrength(Channel::A, StrengthOperation::Decrease, 20, 0);
  TEST_ASSERT_TRUE(lower.prepareCommand(0, command));
  TEST_ASSERT_EQUAL_UINT8(0, command.strengthA);
  TEST_ASSERT_EQUAL_UINT8(0, command.targetStrengthA);
}

void test_cancelled_relative_intents_do_not_produce_a_command() {
  StrengthController controller;
  controller.requestStrength(Channel::A, StrengthOperation::Increase, 10, 0);
  controller.requestStrength(Channel::A, StrengthOperation::Decrease, 10, 0);
  PreparedStrengthCommand command = {};
  TEST_ASSERT_FALSE(controller.prepareCommand(0, command));
}

void test_request_after_prepare_is_sent_after_matching_feedback() {
  StrengthController controller;
  controller.requestStrength(Channel::A, StrengthOperation::Increase, 5, 0);
  PreparedStrengthCommand command = {};
  TEST_ASSERT_TRUE(controller.prepareCommand(0, command));
  controller.requestStrength(Channel::A, StrengthOperation::Increase, 3, 0);
  controller.commitPrepared(command, 0);
  TEST_ASSERT_TRUE(controller.onStrengthResponse(1, 5, 0, 1));

  TEST_ASSERT_TRUE(controller.prepareCommand(1, command));
  TEST_ASSERT_EQUAL_HEX8(0x24, command.sequenceMethod);
  TEST_ASSERT_EQUAL_UINT8(3, command.strengthA);
  TEST_ASSERT_EQUAL_UINT8(8, command.targetStrengthA);
}

void test_request_after_prepare_is_merged_once_on_rollback() {
  StrengthController controller;
  controller.requestStrength(Channel::A, StrengthOperation::Increase, 5, 0);
  PreparedStrengthCommand command = {};
  TEST_ASSERT_TRUE(controller.prepareCommand(0, command));
  controller.requestStrength(Channel::A, StrengthOperation::Increase, 3, 0);
  controller.rollbackPrepared(command);

  TEST_ASSERT_TRUE(controller.prepareCommand(0, command));
  TEST_ASSERT_EQUAL_HEX8(0x14, command.sequenceMethod);
  TEST_ASSERT_EQUAL_UINT8(8, command.strengthA);
  TEST_ASSERT_EQUAL_UINT8(8, command.targetStrengthA);
}

void test_relative_intent_over_200_is_chunked_and_resumes_after_feedback() {
  StrengthController controller;
  controller.requestStrength(Channel::A, StrengthOperation::Increase, 201, 0);
  PreparedStrengthCommand command = {};
  TEST_ASSERT_TRUE(controller.prepareCommand(0, command));
  TEST_ASSERT_EQUAL_UINT8(200, command.strengthA);
  TEST_ASSERT_EQUAL_UINT8(200, command.targetStrengthA);
  controller.commitPrepared(command, 0);
  TEST_ASSERT_TRUE(controller.onStrengthResponse(1, 200, 0, 1));

  TEST_ASSERT_TRUE(controller.prepareCommand(1, command));
  TEST_ASSERT_EQUAL_HEX8(0x24, command.sequenceMethod);
  TEST_ASSERT_EQUAL_UINT8(1, command.strengthA);
  TEST_ASSERT_EQUAL_UINT8(200, command.targetStrengthA);
}

void test_timed_out_relative_command_has_no_retry_or_pending_intent() {
  StrengthController controller;
  controller.requestStrength(Channel::A, StrengthOperation::Increase, 5, 0);
  PreparedStrengthCommand command = {};
  TEST_ASSERT_TRUE(controller.prepareCommand(0, command));
  controller.commitPrepared(command, 0);
  TEST_ASSERT_TRUE(controller.tick(500));
  TEST_ASSERT_FALSE(controller.prepareCommand(500, command));
}

void test_late_feedback_for_timed_out_command_does_not_release_new_command() {
  StrengthController controller;
  PreparedStrengthCommand command = {};
  controller.requestStrength(Channel::A, StrengthOperation::Increase, 5, 0);
  TEST_ASSERT_TRUE(controller.prepareCommand(0, command));
  controller.commitPrepared(command, 0);
  TEST_ASSERT_TRUE(controller.tick(500));

  controller.requestStrength(Channel::A, StrengthOperation::Increase, 3, 500);
  TEST_ASSERT_TRUE(controller.prepareCommand(500, command));
  TEST_ASSERT_EQUAL_HEX8(0x24, command.sequenceMethod);
  controller.commitPrepared(command, 500);

  TEST_ASSERT_FALSE(controller.onStrengthResponse(1, 77, 0, 501));
  TEST_ASSERT_TRUE(controller.waitingForResponse());
  TEST_ASSERT_FALSE(controller.feedbackSynchronized());
  TEST_ASSERT_EQUAL_UINT8(77, controller.strengthA());
  TEST_ASSERT_TRUE(controller.onStrengthResponse(2, 8, 0, 502));
  TEST_ASSERT_FALSE(controller.waitingForResponse());
  TEST_ASSERT_TRUE(controller.feedbackSynchronized());
}

void test_matching_strength_sequences_cycle_from_one_through_fifteen() {
  StrengthController controller;
  PreparedStrengthCommand command = {};
  for (uint8_t i = 0; i < 16; ++i) {
    controller.requestStrength(Channel::A, StrengthOperation::Increase, 1, i);
    TEST_ASSERT_TRUE(controller.prepareCommand(i, command));
    uint8_t expectedSequence = static_cast<uint8_t>((i % 15) + 1);
    TEST_ASSERT_EQUAL_UINT8(expectedSequence,
                            static_cast<uint8_t>(command.sequenceMethod >> 4));
    controller.commitPrepared(command, i);
    TEST_ASSERT_TRUE(controller.onStrengthResponse(
        expectedSequence, command.targetStrengthA, 0, i + 1));
  }
}

void test_reset_clears_prepared_state_and_strength_feedback() {
  StrengthController prepared;
  PreparedStrengthCommand command = {};
  prepared.requestStrength(Channel::A, StrengthOperation::Increase, 5, 0);
  TEST_ASSERT_TRUE(prepared.prepareCommand(0, command));
  prepared.resetConnection();
  TEST_ASSERT_FALSE(prepared.waitingForResponse());
  TEST_ASSERT_FALSE(prepared.feedbackSynchronized());
  TEST_ASSERT_EQUAL_UINT8(0, prepared.strengthA());
  TEST_ASSERT_EQUAL_UINT8(0, prepared.strengthB());
  TEST_ASSERT_FALSE(prepared.prepareCommand(0, command));

  StrengthController inFlight;
  inFlight.requestStrength(Channel::B, StrengthOperation::Absolute, 100, 0);
  TEST_ASSERT_TRUE(inFlight.prepareCommand(0, command));
  inFlight.commitPrepared(command, 0);
  inFlight.requestStrength(Channel::B, StrengthOperation::Increase, 5, 0);
  inFlight.resetConnection();
  TEST_ASSERT_FALSE(inFlight.waitingForResponse());
  TEST_ASSERT_FALSE(inFlight.feedbackSynchronized());
  TEST_ASSERT_EQUAL_UINT8(0, inFlight.strengthA());
  TEST_ASSERT_EQUAL_UINT8(0, inFlight.strengthB());
  TEST_ASSERT_FALSE(inFlight.prepareCommand(0, command));
}

void test_strength_frames_copy_enabled_and_disabled_wave_blocks() {
  const WaveBlock wave = {{1, 2, 3, 4, 5, 6, 7, 8}};
  StrengthController controller;
  controller.requestStrength(Channel::A, StrengthOperation::Increase, 5, 0);
  PreparedStrengthCommand command = {};
  TEST_ASSERT_TRUE(controller.prepareCommand(0, command));

  B0Frame enabled = {};
  encodeB0(2, command.sequenceMethod, command.strengthA, command.strengthB,
           wave, wave, enabled);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(wave.bytes, enabled.bytes + 4, kWaveBlockSize);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(wave.bytes, enabled.bytes + 12, kWaveBlockSize);

  B0Frame disabled = {};
  encodeB0(2, command.sequenceMethod, command.strengthA, command.strengthB,
           kDisabledWave, kDisabledWave, disabled);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kDisabledWave.bytes, disabled.bytes + 4,
                                kWaveBlockSize);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kDisabledWave.bytes, disabled.bytes + 12,
                                kWaveBlockSize);
}

void test_timeout_allows_new_command_without_retry() {
  StrengthController controller;
  controller.requestStrength(Channel::B, StrengthOperation::Absolute, 100, 0);
  PreparedStrengthCommand command = {};
  TEST_ASSERT_TRUE(controller.prepareCommand(0, command));
  controller.commitPrepared(command, 0);
  TEST_ASSERT_FALSE(controller.tick(499));
  TEST_ASSERT_TRUE(controller.waitingForResponse());
  TEST_ASSERT_TRUE(controller.tick(500));
  TEST_ASSERT_FALSE(controller.waitingForResponse());
  TEST_ASSERT_FALSE(controller.feedbackSynchronized());
  TEST_ASSERT_FALSE(controller.prepareCommand(500, command));
  controller.requestStrength(Channel::B, StrengthOperation::Increase, 5, 500);
  TEST_ASSERT_TRUE(controller.prepareCommand(500, command));
  TEST_ASSERT_EQUAL_UINT8(5, command.strengthB);
  TEST_ASSERT_EQUAL_UINT8(105, command.targetStrengthB);
}

void test_absolute_and_relative_requests_merge_per_channel() {
  StrengthController controller;
  controller.requestStrength(Channel::A, StrengthOperation::Absolute, 100, 0);
  controller.requestStrength(Channel::B, StrengthOperation::Decrease, 2, 0);
  PreparedStrengthCommand command = {};
  TEST_ASSERT_TRUE(controller.prepareCommand(0, command));
  TEST_ASSERT_EQUAL_HEX8(0x1E, command.sequenceMethod);
  TEST_ASSERT_EQUAL_UINT8(100, command.strengthA);
  TEST_ASSERT_EQUAL_UINT8(2, command.strengthB);
  TEST_ASSERT_EQUAL_UINT8(0, command.targetStrengthB);
}

void test_queued_requests_are_not_lost_on_write_rollback() {
  StrengthController controller;
  controller.requestStrength(Channel::A, StrengthOperation::Increase, 5, 0);
  PreparedStrengthCommand command = {};
  TEST_ASSERT_TRUE(controller.prepareCommand(0, command));
  controller.rollbackPrepared(command);
  TEST_ASSERT_TRUE(controller.prepareCommand(0, command));
  TEST_ASSERT_EQUAL_UINT8(5, command.strengthA);
}

void test_mismatched_response_keeps_command_in_flight() {
  StrengthController controller;
  controller.requestStrength(Channel::A, StrengthOperation::Increase, 5, 0);
  PreparedStrengthCommand command = {};
  controller.prepareCommand(0, command);
  controller.commitPrepared(command, 0);
  TEST_ASSERT_FALSE(controller.feedbackSynchronized());
  TEST_ASSERT_FALSE(controller.onStrengthResponse(0, 99, 88, 1));
  TEST_ASSERT_TRUE(controller.waitingForResponse());
  TEST_ASSERT_FALSE(controller.feedbackSynchronized());
  TEST_ASSERT_FALSE(controller.onStrengthResponse(7, 98, 87, 1));
  TEST_ASSERT_TRUE(controller.waitingForResponse());
  TEST_ASSERT_EQUAL_UINT8(98, controller.strengthA());
  TEST_ASSERT_EQUAL_UINT8(87, controller.strengthB());
  TEST_ASSERT_TRUE(controller.onStrengthResponse(1, 5, 0, 2));
  TEST_ASSERT_FALSE(controller.waitingForResponse());
  TEST_ASSERT_TRUE(controller.feedbackSynchronized());
}

void test_resume_requires_exact_identity() {
  const DeviceIdentity a = {{1, 2, 3, 4, 5, 6}, 1};
  const DeviceIdentity b = {{1, 2, 3, 4, 5, 7}, 1};
  ResumePolicy policy;
  policy.setDesiredSending(true);
  policy.remember(a);
  TEST_ASSERT_TRUE(policy.shouldRestore(a));
  TEST_ASSERT_FALSE(policy.shouldRestore(b));
  DeviceIdentity differentType = a;
  differentType.addressType = 0;
  TEST_ASSERT_FALSE(policy.shouldRestore(differentType));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_builtin_v2_wave_tables_keep_sizes_and_rotation);
  RUN_TEST(test_builtin_v3_wave_tables_keep_bytes_and_invalid_wave_fallback);
  RUN_TEST(test_b0_layout_is_exactly_twenty_bytes);
  RUN_TEST(test_disabled_wave_is_safe_marker);
  RUN_TEST(test_v2_strength_decode_uses_little_endian_11_bit_channels);
  RUN_TEST(test_wave_schedule_handles_rollover);
  RUN_TEST(test_b0_cycle_waits_until_100ms_without_consuming_pending_strength);
  RUN_TEST(test_b0_cycle_puts_pending_strength_and_both_waves_in_one_frame);
  RUN_TEST(test_b0_cycle_sends_wave_only_while_strength_command_waits_for_b1);
  RUN_TEST(test_b0_cycle_disables_both_wave_channels_with_disabled_wave);
  RUN_TEST(test_relative_strength_keeps_raw_delta);
  RUN_TEST(test_relative_increase_encodes_magnitude_and_predicts_target_for_a);
  RUN_TEST(test_relative_decrease_encodes_magnitude_and_predicts_target_for_a);
  RUN_TEST(test_relative_increase_and_decrease_encode_magnitude_and_target_for_b);
  RUN_TEST(test_a_and_b_relative_intents_share_frame_with_independent_targets);
  RUN_TEST(test_absolute_target_is_adjusted_by_relative_intents_and_clamped);
  RUN_TEST(test_cancelled_relative_intents_do_not_produce_a_command);
  RUN_TEST(test_request_after_prepare_is_sent_after_matching_feedback);
  RUN_TEST(test_request_after_prepare_is_merged_once_on_rollback);
  RUN_TEST(test_relative_intent_over_200_is_chunked_and_resumes_after_feedback);
  RUN_TEST(test_timed_out_relative_command_has_no_retry_or_pending_intent);
  RUN_TEST(test_late_feedback_for_timed_out_command_does_not_release_new_command);
  RUN_TEST(test_matching_strength_sequences_cycle_from_one_through_fifteen);
  RUN_TEST(test_reset_clears_prepared_state_and_strength_feedback);
  RUN_TEST(test_strength_frames_copy_enabled_and_disabled_wave_blocks);
  RUN_TEST(test_timeout_allows_new_command_without_retry);
  RUN_TEST(test_absolute_and_relative_requests_merge_per_channel);
  RUN_TEST(test_queued_requests_are_not_lost_on_write_rollback);
  RUN_TEST(test_mismatched_response_keeps_command_in_flight);
  RUN_TEST(test_resume_requires_exact_identity);
  return UNITY_END();
}
