#pragma once

#include <stdint.h>

enum class CliCommandType : uint8_t {
  Help,
  Status,
  Watch,
  Scan,
  Devices,
  Connect,
  Disconnect,
  AutoConnect,
  Output,
  Wave,
  Strength,
  Logs
};

enum class CliParseError : uint8_t {
  None,
  UnknownCommand,
  InvalidArguments
};

enum class CliStrengthAction : uint8_t { Add, Subtract, Set };

struct CliCommand {
  CliCommandType type = CliCommandType::Help;
  uint32_t value = 0;
  char channel = '\0';
  char wave = '\0';
  CliStrengthAction strengthAction = CliStrengthAction::Add;
  bool enabled = false;
  bool start = false;
};

CliParseError parseCliCommand(char* line, CliCommand& command);
