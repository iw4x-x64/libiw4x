#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <libiw4x/demonware/types.hxx>

namespace iw4x::demonware
{
  // Byte-level typed buffer writer.
  //
  // A simpler, byte-aligned alternative to the bit-level protocol. Each
  // field is prefixed with a one-byte type tag followed by the data in
  // native byte order. Used by some of the newer Demonware service
  // endpoints.
  //
  class byte_buffer_writer
  {
  public:
    byte_buffer_writer () = default;

    void
    write_bool (bool);

    void
    write_uint8 (uint8_t);

    void
    write_uint32 (uint32_t);

    void
    write_int32 (int32_t);

    void
    write_uint64 (uint64_t);

    void
    write_int64 (int64_t);

    void
    write_float (float);

    void
    write_string (const std::string&);

    void
    write_blob (const uint8_t*, std::size_t);

    void
    write_struct_header (uint32_t error_code);

    void
    write_array_count (uint32_t count);

    const uint8_t*
    data () const
    {
      return buffer_.data ();
    }

    std::size_t
    size () const
    {
      return buffer_.size ();
    }

    bool
    empty () const
    {
      return buffer_.empty ();
    }

    std::vector<uint8_t>
    release ()
    {
      return std::move (buffer_);
    }

  private:
    void
    append (const void*, std::size_t);

    void
    append_tag (byte_type_tag);

    std::vector<uint8_t> buffer_;
  };

  // Byte-level typed buffer reader.
  //
  // Deserializes typed fields from a contiguous byte buffer. The reader
  // does not own the data.
  //
  class byte_buffer_reader
  {
  public:
    byte_buffer_reader (const uint8_t* data, std::size_t size)
      : data_ (data), size_ (size), pos_ (0) {}

    bool
    read_bool (bool&);

    bool
    read_uint8 (uint8_t&);

    bool
    read_uint32 (uint32_t&);

    bool
    read_int32 (int32_t&);

    bool
    read_uint64 (uint64_t&);

    bool
    read_int64 (int64_t&);

    bool
    read_float (float&);

    bool
    read_string (std::string&);

    bool
    read_blob (std::vector<uint8_t>&);

    bool
    read_struct_header (uint32_t& error_code);

    std::size_t
    position () const
    {
      return pos_;
    }

    std::size_t
    remaining () const
    {
      return pos_ < size_ ? size_ - pos_ : 0;
    }

    bool
    empty () const
    {
      return pos_ >= size_;
    }

  private:
    bool
    consume (void* out, std::size_t size);

    bool
    verify_tag (byte_type_tag);

    const uint8_t* data_;
    std::size_t size_;
    std::size_t    pos_;
  };
}
