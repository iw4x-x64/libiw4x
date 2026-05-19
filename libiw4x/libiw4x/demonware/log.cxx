#include <libiw4x/demonware/log.hxx>

#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <string>

#include <libiw4x/demonware/diagnostics.hxx>

using namespace std;

namespace iw4x::demonware
{
  void
  bd_log_message (int         /* type */,
                  const char* base_channel,
                  const char* channel,
                  const char* /* file */,
                  const char* /* function */,
                  int         /* line */,
                  const char* fmt,
                  ...)
  {
    va_list ap;
    va_start (ap, fmt);

    // Try the stack buffer first. Most Demonware log messages are short
    // enough to fit into 512 bytes. If they don't, vsnprintf tells us
    // exactly how much space we need and we fall back to a heap
    // allocation.
    //
    char buf[512];

    va_list ac;
    va_copy (ac, ap);
    int const n (vsnprintf (buf, sizeof (buf), fmt, ac));
    va_end (ac);

    assert (n >= 0);

    string heap;
    string_view msg;

    if (static_cast<size_t> (n) < sizeof (buf))
    {
      msg = string_view (buf, static_cast<size_t> (n));
    }
    else
    {
      heap.resize (static_cast<size_t> (n));
      vsnprintf (heap.data (), static_cast<size_t> (n) + 1, fmt, ap);
      msg = heap;
    }

    va_end (ap);

    // Format the channel prefix. Either or both channel strings may be
    // null, so we guard against that.
    //
    auto safe ([] (const char* p) { return p != nullptr ? p : ""; });

    log::trace_l3 ("demonware: [bd {} {}] {}",
                   safe (base_channel),
                   safe (channel),
                   msg);
  }
}
