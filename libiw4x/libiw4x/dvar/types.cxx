#include <libiw4x/dvar/types.hxx>

#include <cinttypes>
#include <cstdio>
#include <cstring>

using namespace std;

namespace iw4x::dvar
{
  // Thread-local rotating string buffer.
  //
  // The idea here is to keep four 256-byte slots, returning one per call in a
  // round-robin order. This lets us have up to four concurrent
  // value_to_string () results live in a single expression. A typical use
  // case for this would be a printf with multiple dvar arguments in the
  // engine's console formatting code.
  //
  // Notice that the returned buffer has its first byte zeroed. Callers
  // naturally must copy the result if it needs to survive a subsequent call
  // in the same thread.
  //
  char*
  next_string_buffer () noexcept
  {
    thread_local char bufs [4][256] {};
    thread_local int slot (0);

    // Advance the slot using a branchless mod-4.
    //
    slot = (slot + 1) & 3;
    bufs[slot][0] = '\0';

    return bufs[slot];
  }

  // Color byte conversion.
  //
  // We clamp a [0, 1] float to a [0, 255] unsigned byte. Notice that we use
  // the manual min/max form rather than clamp (). We do this because
  // this function is called from always-inline paths, and we want the
  // compiler to emit branchless conditional moves without pulling in the
  // <algorithm> header.
  //
  unsigned char
  color_byte (float v) noexcept
  {
    v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    return static_cast<unsigned char> (v * 255.0f + 0.5f);
  }
}
