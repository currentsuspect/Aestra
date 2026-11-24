// Input diagnosis aid, off by default: AESTRA_INPUT_TRACE=1 makes the platform and UI layers log, one INFO line each,
// where presses, keys and X input focus go (plugin editors embedded as X child windows steal both). When the variable
// is unset every call site is a single cached bool test.
#pragma once

#include <string>

namespace Aestra::InputTrace {

/// True when AESTRA_INPUT_TRACE is set to anything but "" or "0". Read once.
bool enabled();

/// Logs "[InputTrace] <line>" at INFO.
void log(const std::string& line);

/// Rate limit: true for at most @p perSecond calls per second for @p key (a string literal). Bursts of key repeats and
/// mouse chatter stay readable; the first event after a quiet second always passes.
bool allow(const char* key, int perSecond);

} // namespace Aestra::InputTrace
