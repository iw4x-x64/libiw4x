#pragma once

#include <cstddef>

// Map inlining control to standard compiler attributes where available.
//
#if defined(__GNUC__)
#  define CONSOLE_FORCEINLINE __attribute__ ((always_inline)) inline
#elif defined(_MSC_VER)
#  define CONSOLE_FORCEINLINE __forceinline
#endif

// Mark a control-flow path that cannot be taken.
//
// Reaching it is undefined behavior, so only use it to guard genuinely
// impossible cases. For example, the default arm of a switch that
// already handles every enum value.
//
#if defined(__GNUC__)
#  define CONSOLE_UNREACHABLE() __builtin_unreachable ()
#elif defined(_MSC_VER)
#  define CONSOLE_UNREACHABLE() __assume (0)
#endif

// Note that we don't want locale dependency here.
//
namespace iw4x::console
{
  CONSOLE_FORCEINLINE constexpr char
  ascii_to_lower (char c) noexcept
  {
    return (c >= 'A' && c <= 'Z') ? static_cast<char> (c - 'A' + 'a') : c;
  }

  CONSOLE_FORCEINLINE constexpr bool
  ascii_is_space (char c) noexcept
  {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
  }

  CONSOLE_FORCEINLINE constexpr bool
  ascii_is_digit (char c) noexcept
  {
    return c >= '0' && c <= '9';
  }

  CONSOLE_FORCEINLINE constexpr bool
  ascii_is_alpha (char c) noexcept
  {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
  }

  CONSOLE_FORCEINLINE constexpr bool
  ascii_is_identifier_start (char c) noexcept
  {
    return ascii_is_alpha (c) || c == '_';
  }

  CONSOLE_FORCEINLINE constexpr bool
  ascii_is_identifier_continue (char c) noexcept
  {
    return ascii_is_alpha (c) || ascii_is_digit (c) || c == '_' || c == '.' || c == '-' || c == '+';
  }
}
