#include <unity.h>

#include <CliParser.h>
#include <string.h>

namespace {
CliParseError parse(const char* text, CliCommand& command) {
  char line[128];
  strncpy(line, text, sizeof(line) - 1);
  line[sizeof(line) - 1] = '\0';
  return parseCliCommand(line, command);
}
}

void setUp() {}
void tearDown() {}

void test_simple_commands() {
  const char* inputs[] = {
      "help", "status", "watch", "scan", "devices", "disconnect", "logs"};
  const CliCommandType types[] = {
      CliCommandType::Help, CliCommandType::Status, CliCommandType::Watch,
      CliCommandType::Scan, CliCommandType::Devices,
      CliCommandType::Disconnect, CliCommandType::Logs};
  for (size_t i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i) {
    CliCommand command{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(CliParseError::None),
        static_cast<int>(parse(inputs[i], command)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(types[i]),
                          static_cast<int>(command.type));
  }
}

void test_connect_and_switch_commands() {
  CliCommand command{};
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CliParseError::None),
                        static_cast<int>(parse("connect 12", command)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CliCommandType::Connect),
                        static_cast<int>(command.type));
  TEST_ASSERT_EQUAL_UINT32(12, command.value);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CliParseError::None),
                        static_cast<int>(parse("connect 0", command)));
  TEST_ASSERT_EQUAL_UINT32(0, command.value);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CliParseError::None),
                        static_cast<int>(parse("autoconnect on", command)));
  TEST_ASSERT_TRUE(command.enabled);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CliParseError::None),
                        static_cast<int>(parse("autoconnect off", command)));
  TEST_ASSERT_FALSE(command.enabled);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CliParseError::None),
                        static_cast<int>(parse("output start", command)));
  TEST_ASSERT_TRUE(command.start);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CliParseError::None),
                        static_cast<int>(parse("output stop", command)));
  TEST_ASSERT_FALSE(command.start);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CliParseError::None),
                        static_cast<int>(parse("wave c", command)));
  TEST_ASSERT_EQUAL_CHAR('c', command.wave);
}

void test_strength_commands() {
  const char* inputs[] = {
      "strength a add 5", "strength b sub 10", "strength a set 200"};
  const char channels[] = {'a', 'b', 'a'};
  const CliStrengthAction actions[] = {
      CliStrengthAction::Add, CliStrengthAction::Subtract,
      CliStrengthAction::Set};
  const uint32_t values[] = {5, 10, 200};
  for (size_t i = 0; i < 3; ++i) {
    CliCommand command{};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CliParseError::None),
                          static_cast<int>(parse(inputs[i], command)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CliCommandType::Strength),
                          static_cast<int>(command.type));
    TEST_ASSERT_EQUAL_CHAR(channels[i], command.channel);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(actions[i]),
                          static_cast<int>(command.strengthAction));
    TEST_ASSERT_EQUAL_UINT32(values[i], command.value);
  }
  CliCommand boundary{};
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CliParseError::None),
                        static_cast<int>(parse("strength b set 0", boundary)));
  TEST_ASSERT_EQUAL_UINT32(0, boundary.value);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CliParseError::None),
                        static_cast<int>(parse("strength b set 4294967295", boundary)));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, boundary.value);
}

void test_whitespace_is_accepted_but_uppercase_is_not() {
  CliCommand command{};
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CliParseError::None),
                        static_cast<int>(parse("  strength   a   add   5  ", command)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CliParseError::UnknownCommand),
                        static_cast<int>(parse("STATUS", command)));
}

void test_bad_arguments_are_rejected_for_known_commands() {
  const char* inputs[] = {
      "status now",       "connect",          "connect -1",
      "connect abc",     "connect 4294967296", "autoconnect yes",
      "output go",       "wave d",           "strength c add 1",
      "strength a grow 1", "strength a add -1", "strength a add 1 extra"};
  for (const char* input : inputs) {
    CliCommand command{};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CliParseError::InvalidArguments),
                          static_cast<int>(parse(input, command)));
  }

  CliCommand command{};
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CliParseError::InvalidArguments),
                        static_cast<int>(parse("status now", command)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CliCommandType::Status),
                        static_cast<int>(command.type));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CliParseError::InvalidArguments),
                        static_cast<int>(parse("connect", command)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CliCommandType::Connect),
                        static_cast<int>(command.type));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CliParseError::InvalidArguments),
                        static_cast<int>(parse("strength c add 1", command)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CliCommandType::Strength),
                        static_cast<int>(command.type));
}

void test_unknown_and_empty_commands() {
  CliCommand command{};
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CliParseError::UnknownCommand),
                        static_cast<int>(parse("nope", command)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CliParseError::UnknownCommand),
                        static_cast<int>(parse("   ", command)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_simple_commands);
  RUN_TEST(test_connect_and_switch_commands);
  RUN_TEST(test_strength_commands);
  RUN_TEST(test_whitespace_is_accepted_but_uppercase_is_not);
  RUN_TEST(test_bad_arguments_are_rejected_for_known_commands);
  RUN_TEST(test_unknown_and_empty_commands);
  return UNITY_END();
}
