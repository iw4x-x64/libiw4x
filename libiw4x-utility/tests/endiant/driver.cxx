#include <libiw4x/utility/endian.hxx>

#include <cassert>
#include <cstdint>
#include <cstring>

using namespace std;
using namespace iw4x::utility;

namespace
{
  template <size_t N>
  void
  check (const unsigned char (&actual)[N],
         const unsigned char (&expected)[N])
  {
    assert (memcmp (actual, expected, N) == 0);
  }

  void
  test_encode_le ()
  {
    {
      unsigned char b[2] {};
      const unsigned char e[2] {0x34, 0x12};

      encode_le (b, uint16_t (0x1234));

      check (b, e);
    }

    {
      unsigned char b[4] {};
      const unsigned char e[4] {0x78, 0x56, 0x34, 0x12};

      encode_le (b, uint32_t (0x12345678));

      check (b, e);
    }

    {
      unsigned char b[8] {};
      const unsigned char e[8] { 0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01};

      encode_le (b, uint64_t (0x0123456789abcdefULL));

      check (b, e);
    }
  }

  void
  test_encode_be ()
  {
    {
      unsigned char b[2] {};
      const unsigned char e[2] {0x12, 0x34};

      encode_be (b, uint16_t (0x1234));

      check (b, e);
    }

    {
      unsigned char b[4] {};
      const unsigned char e[4] {0x12, 0x34, 0x56, 0x78};

      encode_be (b, uint32_t (0x12345678));

      check (b, e);
    }

    {
      unsigned char b[8] {};
      const unsigned char e[8] {
        0x01, 0x23, 0x45, 0x67,
        0x89, 0xab, 0xcd, 0xef};

      encode_be (b, uint64_t (0x0123456789abcdefULL));

      check (b, e);
    }
  }

  void
  test_decode_le ()
  {
    {
      const unsigned char b[2] {0x34, 0x12};

      uint16_t v (0);
      decode_le (b, v);

      assert (v == uint16_t (0x1234));
    }

    {
      const unsigned char b[4] {0x78, 0x56, 0x34, 0x12};

      uint32_t v (0);
      decode_le (b, v);

      assert (v == uint32_t (0x12345678));
    }

    {
      const unsigned char b[8] {
        0xef, 0xcd, 0xab, 0x89,
        0x67, 0x45, 0x23, 0x01};

      uint64_t v (0);
      decode_le (b, v);

      assert (v == uint64_t (0x0123456789abcdefULL));
    }
  }

  void
  test_decode_be ()
  {
    {
      const unsigned char b[2] {0x12, 0x34};

      uint16_t v (0);
      decode_be (b, v);

      assert (v == uint16_t (0x1234));
    }

    {
      const unsigned char b[4] {0x12, 0x34, 0x56, 0x78};

      uint32_t v (0);
      decode_be (b, v);

      assert (v == uint32_t (0x12345678));
    }

    {
      const unsigned char b[8] {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};

      uint64_t v (0);
      decode_be (b, v);

      assert (v == uint64_t (0x0123456789abcdefULL));
    }
  }

  template <typename T>
  void
  test_roundtrip_le (T v)
  {
    unsigned char b[sizeof (T)] {};

    T r (0);

    encode_le (b, v);
    decode_le (b, r);

    assert (r == v);
  }

  template <typename T>
  void
  test_roundtrip_be (T v)
  {
    unsigned char b[sizeof (T)] {};

    T r (0);

    encode_be (b, v);
    decode_be (b, r);

    assert (r == v);
  }

  void
  test_roundtrip ()
  {
    test_roundtrip_le<uint16_t> (uint16_t (0));
    test_roundtrip_le<uint16_t> (uint16_t (1));
    test_roundtrip_le<uint16_t> (uint16_t (0x00ff));
    test_roundtrip_le<uint16_t> (uint16_t (0x8000));
    test_roundtrip_le<uint16_t> (uint16_t (0xffff));

    test_roundtrip_le<uint32_t> (uint32_t (0));
    test_roundtrip_le<uint32_t> (uint32_t (1));
    test_roundtrip_le<uint32_t> (uint32_t (0x000000ff));
    test_roundtrip_le<uint32_t> (uint32_t (0x80000000));
    test_roundtrip_le<uint32_t> (uint32_t (0xffffffff));

    test_roundtrip_le<uint64_t> (uint64_t (0));
    test_roundtrip_le<uint64_t> (uint64_t (1));
    test_roundtrip_le<uint64_t> (uint64_t (0x00000000000000ffULL));
    test_roundtrip_le<uint64_t> (uint64_t (0x8000000000000000ULL));
    test_roundtrip_le<uint64_t> (uint64_t (0xffffffffffffffffULL));

    test_roundtrip_be<uint16_t> (uint16_t (0));
    test_roundtrip_be<uint16_t> (uint16_t (1));
    test_roundtrip_be<uint16_t> (uint16_t (0x00ff));
    test_roundtrip_be<uint16_t> (uint16_t (0x8000));
    test_roundtrip_be<uint16_t> (uint16_t (0xffff));

    test_roundtrip_be<uint32_t> (uint32_t (0));
    test_roundtrip_be<uint32_t> (uint32_t (1));
    test_roundtrip_be<uint32_t> (uint32_t (0x000000ff));
    test_roundtrip_be<uint32_t> (uint32_t (0x80000000));
    test_roundtrip_be<uint32_t> (uint32_t (0xffffffff));

    test_roundtrip_be<uint64_t> (uint64_t (0));
    test_roundtrip_be<uint64_t> (uint64_t (1));
    test_roundtrip_be<uint64_t> (uint64_t (0x00000000000000ffULL));
    test_roundtrip_be<uint64_t> (uint64_t (0x8000000000000000ULL));
    test_roundtrip_be<uint64_t> (uint64_t (0xffffffffffffffffULL));
  }

  void
  test_overloads ()
  {
    unsigned char b[8] {};

    uint16_t u16 (0x1234);
    uint32_t u32 (0x12345678);
    uint64_t u64 (0x0123456789abcdefULL);

    encode_le (b, u16);
    decode_le (b, u16);
    assert (u16 == uint16_t (0x1234));

    encode_le (b, u32);
    decode_le (b, u32);
    assert (u32 == uint32_t (0x12345678));

    encode_le (b, u64);
    decode_le (b, u64);
    assert (u64 == uint64_t (0x0123456789abcdefULL));

    encode_be (b, u16);
    decode_be (b, u16);
    assert (u16 == uint16_t (0x1234));

    encode_be (b, u32);
    decode_be (b, u32);
    assert (u32 == uint32_t (0x12345678));

    encode_be (b, u64);
    decode_be (b, u64);
    assert (u64 == uint64_t (0x0123456789abcdefULL));
  }
}

int
main ()
{
  test_encode_le ();
  test_encode_be ();

  test_decode_le ();
  test_decode_be ();

  test_roundtrip ();
  test_overloads ();
}
