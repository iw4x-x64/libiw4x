#pragma once

#include <cstdint>

#include <libiw4x/utility/export.hxx>

namespace iw4x
{
  namespace utility
  {
    LIBIW4X_UTILITY_SYMEXPORT void
    encode_le (unsigned char*, std::uint16_t) noexcept;

    LIBIW4X_UTILITY_SYMEXPORT void
    encode_le (unsigned char*, std::uint32_t) noexcept;

    LIBIW4X_UTILITY_SYMEXPORT void
    encode_le (unsigned char*, std::uint64_t) noexcept;

    LIBIW4X_UTILITY_SYMEXPORT void
    encode_be (unsigned char*, std::uint16_t) noexcept;

    LIBIW4X_UTILITY_SYMEXPORT void
    encode_be (unsigned char*, std::uint32_t) noexcept;

    LIBIW4X_UTILITY_SYMEXPORT void
    encode_be (unsigned char*, std::uint64_t) noexcept;

    LIBIW4X_UTILITY_SYMEXPORT void
    decode_le (const unsigned char*, std::uint16_t&) noexcept;

    LIBIW4X_UTILITY_SYMEXPORT void
    decode_le (const unsigned char*, std::uint32_t&) noexcept;

    LIBIW4X_UTILITY_SYMEXPORT void
    decode_le (const unsigned char*, std::uint64_t&) noexcept;

    LIBIW4X_UTILITY_SYMEXPORT void
    decode_be (const unsigned char*, std::uint16_t&) noexcept;

    LIBIW4X_UTILITY_SYMEXPORT void
    decode_be (const unsigned char*, std::uint32_t&) noexcept;

    LIBIW4X_UTILITY_SYMEXPORT void
    decode_be (const unsigned char*, std::uint64_t&) noexcept;
  }
}
