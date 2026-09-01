#include "CliParser.h"

#include <ctype.h>
#include <limits.h>
#include <string.h>

namespace {
char* nextToken(char*& cursor) {
  while (*cursor && isspace(static_cast<unsigned char>(*cursor))) ++cursor;
  if (!*cursor) return nullptr;
  char* token = cursor;
  while (*cursor && !isspace(static_cast<unsigned char>(*cursor))) ++cursor;
  if (*cursor) *cursor++ = '\0';
  return token;
}

bool noMoreTokens(char*& cursor) { return nextToken(cursor) == nullptr; }

bool parseUnsigned(const char* token, uint32_t& value) {
  if (!token || !*token) return false;
  uint32_t parsed = 0;
  for (const char* p = token; *p; ++p) {
    if (*p < '0' || *p > '9') return false;
    const uint32_t digit = static_cast<uint32_t>(*p - '0');
    if (parsed > (UINT32_MAX - digit) / 10U) return false;
    parsed = parsed * 10U + digit;
  }
  value = parsed;
  return true;
}

CliParseError parseNoArgs(char*& cursor) {
  return noMoreTokens(cursor) ? CliParseError::None
                              : CliParseError::InvalidArguments;
}
}

CliParseError parseCliCommand(char* line, CliCommand& command) {
  command = CliCommand{};
  char* cursor = line;
  char* name = nextToken(cursor);
  if (!name) return CliParseError::UnknownCommand;

  struct SimpleCommand {
    const char* name;
    CliCommandType type;
  };
  static const SimpleCommand simple[] = {
      {"help", CliCommandType::Help}, {"status", CliCommandType::Status},
      {"watch", CliCommandType::Watch}, {"scan", CliCommandType::Scan},
      {"devices", CliCommandType::Devices},
      {"disconnect", CliCommandType::Disconnect}, {"logs", CliCommandType::Logs},
  };
  for (const auto& item : simple) {
    if (strcmp(name, item.name) == 0) {
      command.type = item.type;
      return parseNoArgs(cursor);
    }
  }

  if (strcmp(name, "connect") == 0) {
    command.type = CliCommandType::Connect;
    char* index = nextToken(cursor);
    return parseUnsigned(index, command.value) && noMoreTokens(cursor)
               ? CliParseError::None
               : CliParseError::InvalidArguments;
  }

  if (strcmp(name, "autoconnect") == 0) {
    command.type = CliCommandType::AutoConnect;
    char* value = nextToken(cursor);
    if (!value || !noMoreTokens(cursor)) return CliParseError::InvalidArguments;
    if (strcmp(value, "on") == 0) command.enabled = true;
    else if (strcmp(value, "off") == 0) command.enabled = false;
    else return CliParseError::InvalidArguments;
    return CliParseError::None;
  }

  if (strcmp(name, "output") == 0) {
    command.type = CliCommandType::Output;
    char* value = nextToken(cursor);
    if (!value || !noMoreTokens(cursor)) return CliParseError::InvalidArguments;
    if (strcmp(value, "start") == 0) command.start = true;
    else if (strcmp(value, "stop") == 0) command.start = false;
    else return CliParseError::InvalidArguments;
    return CliParseError::None;
  }

  if (strcmp(name, "wave") == 0) {
    command.type = CliCommandType::Wave;
    char* value = nextToken(cursor);
    if (!value || value[1] || !noMoreTokens(cursor) ||
        (value[0] != 'a' && value[0] != 'b' && value[0] != 'c')) {
      return CliParseError::InvalidArguments;
    }
    command.wave = value[0];
    return CliParseError::None;
  }

  if (strcmp(name, "strength") == 0) {
    command.type = CliCommandType::Strength;
    char* channel = nextToken(cursor);
    char* action = nextToken(cursor);
    char* value = nextToken(cursor);
    if (!channel || channel[1] || (channel[0] != 'a' && channel[0] != 'b') ||
        !action || !parseUnsigned(value, command.value) || !noMoreTokens(cursor)) {
      return CliParseError::InvalidArguments;
    }
    command.channel = channel[0];
    if (strcmp(action, "add") == 0) command.strengthAction = CliStrengthAction::Add;
    else if (strcmp(action, "sub") == 0) command.strengthAction = CliStrengthAction::Subtract;
    else if (strcmp(action, "set") == 0) command.strengthAction = CliStrengthAction::Set;
    else return CliParseError::InvalidArguments;
    return CliParseError::None;
  }

  return CliParseError::UnknownCommand;
}
