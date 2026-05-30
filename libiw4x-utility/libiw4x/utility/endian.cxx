#include <libiw4x/utility/endian.hxx>

#include <cassert>
#include <climits>

using namespace std;

namespace iw4x
{
  namespace utility
  {
    namespace
    {
      template <typename T>
      void
      encode_le_impl (unsigned char* p, T v) noexcept
      {
        assert (p != nullptr);

        for (size_t i (0); i != sizeof (T); ++i)
          p[i] = static_cast<unsigned char> ((v >> (i * CHAR_BIT)) & 0xffu);
      }

      template <typename T>
      void
      encode_be_impl (unsigned char* p, T v) noexcept
      {
        assert (p != nullptr);

        for (size_t i (0), n (sizeof (T)); i != n; ++i)
          p[n - i - 1] =
            static_cast<unsigned char> ((v >> (i * CHAR_BIT)) & 0xffu);
      }

      template <typename T>
      T
      decode_le_impl (const unsigned char* p) noexcept
      {
        assert (p != nullptr);

        T r (0);

        for (size_t i (0); i != sizeof (T); ++i)
          r |= static_cast<T> (p[i]) << (i * CHAR_BIT);

        return r;
      }

      template <typename T>
      T
      decode_be_impl (const unsigned char* p) noexcept
      {
        assert (p != nullptr);

        T r (0);

        for (size_t i (0), n (sizeof (T)); i != n; ++i)
          r |= static_cast<T> (p[n - i - 1]) << (i * CHAR_BIT);

        return r;
      }
    }

    void
    encode_le (unsigned char* p, uint16_t v) noexcept
    {
      encode_le_impl (p, v);
    }

    void
    encode_le (unsigned char* p, uint32_t v) noexcept
    {
      encode_le_impl (p, v);
    }

    void
    encode_le (unsigned char* p, uint64_t v) noexcept
    {
      encode_le_impl (p, v);
    }

    void
    encode_be (unsigned char* p, uint16_t v) noexcept
    {
      encode_be_impl (p, v);
    }

    void
    encode_be (unsigned char* p, uint32_t v) noexcept
    {
      encode_be_impl (p, v);
    }

    void
    encode_be (unsigned char* p, uint64_t v) noexcept
    {
      encode_be_impl (p, v);
    }

    void
    decode_le (const unsigned char* p, uint16_t& v) noexcept
    {
      v = decode_le_impl<uint16_t> (p);
    }

    void
    decode_le (const unsigned char* p, uint32_t& v) noexcept
    {
      v = decode_le_impl<uint32_t> (p);
    }

    void
    decode_le (const unsigned char* p, uint64_t& v) noexcept
    {
      v = decode_le_impl<uint64_t> (p);
    }

    void
    decode_be (const unsigned char* p, uint16_t& v) noexcept
    {
      v = decode_be_impl<uint16_t> (p);
    }

    void
    decode_be (const unsigned char* p, uint32_t& v) noexcept
    {
      v = decode_be_impl<uint32_t> (p);
    }

    void
    decode_be (const unsigned char* p, uint64_t& v) noexcept
    {
      v = decode_be_impl<uint64_t> (p);
    }
  }
}
