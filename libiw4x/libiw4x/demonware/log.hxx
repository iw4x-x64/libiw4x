#pragma once

namespace iw4x::demonware
{
  // Demonware log message bridge.
  //
  // The engine's native code calls bdLogMessage() to log diagnostic
  // messages from the Demonware SDK.
  //
  void
  bd_log_message (int         type,
                  const char* base_channel,
                  const char* channel,
                  const char* file,
                  const char* function,
                  int         line,
                  const char* fmt,
                  ...);
}
