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

void test_timeout_allows_new_command_without_retry() {
  StrengthController controller;
  controller.requestStrength(Channel::B, StrengthOperation::Absolute, 100, 0);
  PreparedStrengthCommand command = {};
  TEST_ASSERT_TRUE(controller.prepareCommand(0, command));
  controller.commitPrepared(command, 0);
  controller.tick(500);
  TEST_ASSERT_FALSE(controller.waitingForResponse());
  controller.requestStrength(Channel::B, StrengthOperation::Increase, 5, 500);
  TEST_ASSERT_TRUE(controller.prepareCommand(500, command));
  TEST_ASSERT_EQUAL_UINT8(105, command.strengthB);
}

void test_absolute_and_relative_requests_merge_per_channel() {
  StrengthController controller;
  controller.requestStrength(Channel::A, StrengthOperation::Absolute, 100, 0);
  controller.requestStrength(Channel::B, StrengthOperation::Decrease, 2, 0);
  PreparedStrengthCommand command = {};
  TEST_ASSERT_TRUE(controller.prepareCommand(0, command));
  TEST_ASSERT_EQUAL_HEX8(0x1E, command.sequenceMethod);
  TEST_ASSERT_EQUAL_UINT8(100, command.strengthA);
  TEST_ASSERT_EQUAL_UINT8(0, command.strengthB);
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
  controller.onStrengthResponse(7, 99, 88, 1);
  TEST_ASSERT_TRUE(controller.waitingForResponse());
  controller.onStrengthResponse(1, 5, 0, 2);
  TEST_ASSERT_FALSE(controller.waitingForResponse());
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

void setup() {
  UNITY_BEGIN();
  RUN_TEST(test_b0_layout_is_exactly_twenty_bytes);
  RUN_TEST(test_disabled_wave_is_safe_marker);
  RUN_TEST(test_wave_schedule_handles_rollover);
  RUN_TEST(test_relative_strength_keeps_raw_delta);
  RUN_TEST(test_timeout_allows_new_command_without_retry);
  RUN_TEST(test_absolute_and_relative_requests_merge_per_channel);
  RUN_TEST(test_queued_requests_are_not_lost_on_write_rollback);
  RUN_TEST(test_mismatched_response_keeps_command_in_flight);
  RUN_TEST(test_resume_requires_exact_identity);
  UNITY_END();
}

void loop() {}
