#pragma once

#include <climits>
#include <cstddef>
#include <cstdint>

#include <libiw4x/dvar/types.hxx>
#include <libiw4x/import.hxx>

// Compiler hints.
//
#if defined(__GNUC__)
#  define DVAR_ALWAYS_INLINE __attribute__ ((always_inline)) inline
#  define DVAR_COLD __attribute__ ((cold, noinline))
#  define DVAR_NOINLINE __attribute__ ((noinline))
#elif defined(_MSC_VER)
#  define DVAR_ALWAYS_INLINE __forceinline
#  define DVAR_COLD __declspec (noinline)
#  define DVAR_NOINLINE __declspec (noinline)
#endif

// Prefetch hint.
//
// We use this in find_raw() to overlap hash-chain traversal with memory
// latency. Note that the locality hint 3 (T0) means all cache levels.
//
#if defined(_MSC_VER)
#  include <intrin.h>
#  define DVAR_PREFETCH(p)                                                     \
    _mm_prefetch (reinterpret_cast<const char*> (p), _MM_HINT_T0)
#elif defined(__GNUC__)
#  define DVAR_PREFETCH(p) __builtin_prefetch ((p), 0, 3)
#endif

namespace iw4x::dvar
{
  inline constexpr std::size_t max_string_probe (8192);

  // Pool membership.
  //
  // The engine allocates dvars from a flat array (dvarPool). We map
  // a dvar_t* back to its index so we can index the parallel description
  // array.
  //
  // Note that if the pointer does not land inside the pool at the correct
  // alignment, we reject it. It is either a stale pointer or fabricated.
  //
  DVAR_ALWAYS_INLINE bool
  pool_index (const dvar_t* d, std::size_t& index) noexcept
  {
    if (d == nullptr)
      return false;

    const auto base (reinterpret_cast<uintptr_t> (dvarPool));
    const auto address (reinterpret_cast<uintptr_t> (d));
    const auto span (sizeof (dvar_t) * pool_capacity);

    if (address < base || address >= base + span)
      return false;

    const auto offset (address - base);

    if (offset % sizeof (dvar_t) != 0)
      return false;

    index = (offset / sizeof (dvar_t));
    return true;
  }

  // Description storage
  //
  // The engine's own description field belongs to SL, which is the string
  // library subsystem. Note that this is not available during early Dvar_Init.
  // Because of this, we keep a parallel heap-allocated array indexed by the
  // pool position.
  //
  // While we could intentionally leak old descriptions on replacement rather
  // than tracking them (since descriptions change at most once per dvar per
  // run and the total memory is negligible), the original code freed them so we
  // preserve that behavior to be safe.

  extern const char* descriptions[pool_capacity];

  void
  set_description (dvar_t* d, const char* text) noexcept;

  const char*
  get_description (const dvar_t* d) noexcept;

  // Type-aware value copy.
  //
  // Copy only the live bytes of src into dst. Vector types occupy
  // only their leading components, so we zero the tail to prevent garbage
  // in unused union members, which would otherwise break values_equal ().
  //
  // Note that this is always-inlined because it sits on the critical
  // path of register, set, and set_latched operations.
  //
  DVAR_ALWAYS_INLINE void
  copy_value (DvarValue& dst, const DvarValue& src, dvarType type) noexcept
  {
    switch (type)
    {
      case DVAR_TYPE_FLOAT_2:
      {
        __builtin_memcpy (&dst, &src, sizeof (float) * 2);
        __builtin_memset (reinterpret_cast<char*> (&dst) + sizeof (float) * 2,
                          0,
                          sizeof (DvarValue) - sizeof (float) * 2);
        break;
      }
      case DVAR_TYPE_FLOAT_3:
      case DVAR_TYPE_FLOAT_3_COLOR:
      {
        __builtin_memcpy (&dst, &src, sizeof (float) * 3);
        __builtin_memset (reinterpret_cast<char*> (&dst) + sizeof (float) * 3,
                          0,
                          sizeof (DvarValue) - sizeof (float) * 3);
        break;
      }
      case DVAR_TYPE_FLOAT_4:
      {
        __builtin_memcpy (&dst, &src, sizeof (float) * 4);
        break;
      }
      default:
      {
        // Scalar and union types (bool, int, int64, uint64, float, color,
        // string). A full-union copy produces a single MOVQ or MOVDQA on
        // x86-64.
        //
        __builtin_memcpy (&dst, &src, sizeof (DvarValue));
        break;
      }
    }
  }

  // Canonical string selection.
  //
  // When writing a string dvar, try to reuse an existing pointer from one
  // of the three live slots (current, latched, reset) if the new value is
  // pointer-equal to it. This avoids a CopyStringInternal () call for the
  // common case of setting a dvar to its own current value.
  //
  // Note that each variant excludes its target slot from reuse consideration
  // to avoid aliasing what we are about to overwrite.
  //

  DvarValue
  canonical_string_for_latched (const dvar_t* d, const char* value) noexcept;

  DvarValue
  canonical_string_for_current (const dvar_t* d, const char* value) noexcept;

  DvarValue
  canonical_string_for_reset (const dvar_t* d, const char* value) noexcept;

  // String-to-native conversion.
  //
  // Parse console/config string input into the native type. Note that
  // these do not validate against the domain. Callers must call
  // value_in_domain () themselves after conversion.
  //

  // Case-insensitive enum match.
  //
  // Return the index on success. Note that it returns -1337 as a sentinel
  // on no match.
  //
  int
  string_to_enum (const DvarLimits& domain, const char* str) noexcept;

  // Parse "r g b a" into DvarValue.color[4].
  //
  void
  string_to_color (const char* str, DvarValue& out) noexcept;

  // General-purpose string-to-value.
  //
  // Write the parsed result into *out and return out.
  //
  DvarValue*
  string_to_value (DvarValue* out,
                   dvarType type,
                   const DvarLimits* domain,
                   const char* str) noexcept;

  // Thread-local rotating buffer.
  //
  // Provide 4 slots of 256 bytes for temporary string operations.
  //
  char*
  next_string_buffer () noexcept;

  // Convert float color component to byte.
  //
  unsigned char
  color_byte (float v) noexcept;

  // Command argument wrappers.
  //
  // The engine's cmd_args structure uses a nesting index. We clamp the
  // nesting to [0, 7] and guard against null argv entries so callers
  // don't have to worry about strict bounds checking.
  //

  DVAR_ALWAYS_INLINE int
  cmd_argc () noexcept
  {
    int n (cmd_args->nesting);

    if (n < 0)
      n = (0);

    if (n > 7)
      n = (7);

    return (cmd_args->argc[n]);
  }

  DVAR_ALWAYS_INLINE const char*
  cmd_argv (int index) noexcept
  {
    int n (cmd_args->nesting);

    if (n < 0)
      n = (0);

    if (n > 7)
      n = (7);

    if (index < 0 || index >= cmd_args->argc[n])
      return "";

    const char** const argv (cmd_args->argv[n]);

    return (argv != nullptr && argv[index] != nullptr) ? argv[index] : "";
  }

  // Concatenate argv[start_arg..argc-1] with single spaces into out.
  //
  // Note that the buffer must be at least 0x1000 bytes. We stop at the
  // engine's 0xFFE-byte command-line budget.
  //
  void
  get_combined_string (char* out, int start_arg) noexcept;

  // Check if specific flags are set in the mask.
  //
  DVAR_ALWAYS_INLINE bool
  flag_set (DvarFlags flags, DvarFlags mask) noexcept
  {
    return ((static_cast<int> (flags) & static_cast<int> (mask)) != 0);
  }

  // Saturate a 64-bit signed integer to 32-bit signed limits.
  //
  DVAR_ALWAYS_INLINE int
  saturate_to_int (std::int64_t v) noexcept
  {
    if (v > INT_MAX)
      return (INT_MAX);

    if (v < INT_MIN)
      return (INT_MIN);

    return (static_cast<int> (v));
  }

  // Saturate a 64-bit unsigned integer to the 32-bit signed maximum.
  //
  DVAR_ALWAYS_INLINE int
  saturate_to_int (std::uint64_t v) noexcept
  {
    return (v > static_cast<std::uint64_t> (INT_MAX) ? INT_MAX
                                                     : static_cast<int> (v));
  }
}
