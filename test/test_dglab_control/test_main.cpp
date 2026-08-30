#include <unity.h>
#include <DgLabControl.h>

using namespace dglab;

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

void test_wave_schedule_handles_rollover() {
  TEST_ASSERT_FALSE(isWaveSendDue(0xFFFFFFC0u, 0xFFFFFFC0u));
  TEST_ASSERT_TRUE(isWaveSendDue(50u, 0xFFFFFFC0u));
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
  TEST_ASSERT_EQUAL_HEX8(0x24, command.sequenceMethod);
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
  RUN_TEST(test_b0_layout_is_exactly_twenty_bytes);
  RUN_TEST(test_disabled_wave_is_safe_marker);
  RUN_TEST(test_wave_schedule_handles_rollover);
  RUN_TEST(test_relative_strength_keeps_raw_delta);
  RUN_TEST(test_relative_increase_encodes_magnitude_and_predicts_target_for_a);
  RUN_TEST(test_relative_decrease_encodes_magnitude_and_predicts_target_for_a);
  RUN_TEST(test_relative_increase_and_decrease_encode_magnitude_and_target_for_b);
  RUN_TEST(test_a_and_b_relative_intents_share_frame_with_independent_targets);
  RUN_TEST(test_absolute_target_is_adjusted_by_relative_intents_and_clamped);
  RUN_TEST(test_cancelled_relative_intents_do_not_produce_a_command);
  RUN_TEST(test_timeout_allows_new_command_without_retry);
  RUN_TEST(test_absolute_and_relative_requests_merge_per_channel);
  RUN_TEST(test_queued_requests_are_not_lost_on_write_rollback);
  RUN_TEST(test_mismatched_response_keeps_command_in_flight);
  RUN_TEST(test_resume_requires_exact_identity);
  return UNITY_END();
}
