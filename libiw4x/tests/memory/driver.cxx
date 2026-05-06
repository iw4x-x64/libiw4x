#include <cstring>
#include <cstdint>

#undef NDEBUG
#include <cassert>

#include <libiw4x/memory.hxx>

int
main ()
{
  using namespace std;
  using namespace iw4x;

  // First, let's verify the edge cases. An empty write shouldn't touch
  // the destination memory. If it does, we have a problem right out of
  // the gate.
  //
  {
    uint8_t b (0xAA);

    memory::write (&b, 0x90, 0);
    assert (b == 0xAA);

    memory::write (&b, &b, 0);
    assert (b == 0xAA);
  }

  // Standard single-byte blast. We ask for a non-NOP value, so this
  // should just fall through to the underlying memset routine.
  //
  {
    uint8_t b[5];
    memory::write (b, 0xCC, 5);

    for (size_t i (0); i != 5; ++i)
      assert (b[i] == 0xCC);
  }

  // Now for the meat: NOP expansion. We will start with a sequence
  // smaller than our 9-byte chunk limit to test the remainder lookup
  // directly without the chunking loop getting in the way.
  //
  {
    uint8_t b[4] {0};
    memory::write (b, 0x90, 4);

    // We expect the 4-byte architectural NOP: {0x0F, 0x1F, 0x40, 0x00}.
    //
    assert (b[0] == 0x0F && b[1] == 0x1F && b[2] == 0x40 && b[3] == 0x00);
  }

  // Next, test the multi-chunk logic. Asking for 20 bytes gives us two
  // full 9-byte chunks and a 2-byte tail sequence.
  //
  {
    uint8_t b[20] {0};
    memory::write (b, 0x90, 20);

    const uint8_t nop9[] {0x66, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00};
    const uint8_t nop2[] {0x66, 0x90};

    assert (memcmp (b, nop9, 9) == 0);
    assert (memcmp (b + 9, nop9, 9) == 0);
    assert (memcmp (b + 18, nop2, 2) == 0);
  }

  // Memory copy functionality. First, a plain copy without any NOPs in
  // the source to trigger the fast-path memchr skip.
  //
  {
    const uint8_t src[] {0xAA, 0xBB, 0xCC};
    uint8_t b[3] {0};

    memory::write (b, src, 3);

    assert (memcmp (b, src, 3) == 0);
  }

  // Interplay between plain copy and NOP expansion. This tests the
  // core premise of the proxy copy function by nesting NOPs inside
  // ordinary data bytes.
  //
  {
    // Data, then 3 NOPs, then Data.
    //
    const uint8_t src[] {0xAA, 0x90, 0x90, 0x90, 0xBB};
    uint8_t b[5] {0};

    memory::write (b, src, 5);

    assert (b[0] == 0xAA);

    // 3-byte NOP sequence expected: {0x0F, 0x1F, 0x00}.
    //
    assert (b[1] == 0x0F && b[2] == 0x1F && b[3] == 0x00);
    assert (b[4] == 0xBB);
  }

  // Finally, the convenience wrappers. These are syntactic sugar, but
  // the string literal handling has a subtle quirk we must verify: it
  // intentionally truncates the null terminator since we almost never
  // want to patch an executable with a trailing null.
  //
  {
    uint8_t b[5] {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    // The string literal is 5 bytes including the null. The wrapper
    // should only write 4, leaving the tail unmodified.
    //
    memory::write (reinterpret_cast<uintptr_t> (b), "test");

    assert (b[0] == 't' && b[1] == 'e' && b[2] == 's' && b[3] == 't');
    assert (b[4] == 0xFF);
  }

  // Contrast this with the generic array wrapper, which writes the
  // exact dimension provided without any truncation.
  //
  {
    const uint8_t src[2] {0x11, 0x22};
    uint8_t b[3] {0xFF, 0xFF, 0xFF};

    memory::write (reinterpret_cast<uintptr_t> (b), src);

    assert (b[0] == 0x11 && b[1] == 0x22);
    assert (b[2] == 0xFF);
  }
}
