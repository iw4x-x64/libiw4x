#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <libiw4x/demonware/types.hxx>

namespace iw4x::demonware
{
  // Raw bit-level writer.
  //
  // Writes individual bits or groups of bits into a growable byte buffer. Bits
  // are packed LSB-first within each byte, which matches the Demonware wire
  // format. The buffer grows in 64-byte increments to avoid excessive
  // reallocations for typical payloads.
  //
  class bit_writer
  {
  public:
    bit_writer ();

    void
    write_bit (bool);

    void
    write_bits (uint64_t value, unsigned int n);

    void
    write_bytes (const uint8_t*, std::size_t);

    const uint8_t*
    data () const
    {
      return buffer_.data ();
    }

    std::size_t
    size () const;

    std::size_t
    bit_size () const
    {
      return bit_pos_;
    }

  private:
    void
    ensure_capacity (std::size_t additional_bits);

    std::vector<uint8_t> buffer_;
    std::size_t bit_pos_;
  };

  // Raw bit-level reader.
  //
  // Reads individual bits or groups of bits from a byte buffer. The reader
  // does not own the data -- it simply walks a pointer that someone else is
  // responsible for keeping alive.
  //
  class bit_reader
  {
  public:
    bit_reader (const uint8_t* data, std::size_t size_bytes)
      : data_ (data), size_ (size_bytes), bit_pos_ (0) {}

    bool
    read_bit (bool&);

    bool
    read_bits (uint64_t& value, unsigned int n);

    bool
    read_bytes (uint8_t*, std::size_t);

    std::size_t
    position () const
    {
      return bit_pos_;
    }

    void
    set_position (std::size_t bit)
    {
      bit_pos_ = bit;
    }

  private:
    const uint8_t* data_;
    std::size_t    size_;
    std::size_t    bit_pos_;
  };

  // Typed bit-buffer writer.
  //
  // Serializes typed fields using the 5-bit type tag protocol that the
  // Demonware wire format mandates. Each field is prefixed with a tag
  // written via the underlying bit_writer, followed by the value bits.
  //
  class bit_buffer_writer
  {
  public:
    bit_buffer_writer () = default;

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
    write_float (float);

    void
    write_string (const char*);

    void
    write_blob (const uint8_t*, std::size_t);

    void
    write_bytes (const uint8_t* data, std::size_t size)
    {
      writer_.write_bytes (data, size);
    }

    // Forwarded accessors.
    //
    const uint8_t*
    data () const
    {
      return writer_.data ();
    }

    std::size_t
    size () const
    {
      return writer_.size ();
    }

    std::size_t
    bit_size () const
    {
      return writer_.bit_size ();
    }

    bit_writer&
    writer ()
    {
      return writer_;
    }

    const bit_writer&
    writer () const
    {
      return writer_;
    }

  private:
    void write_tag (bit_type_tag);

    bit_writer writer_;
  };

  // Typed bit-buffer reader.
  //
  // Deserializes typed fields from a bit buffer. Returns false from each
  // read method if the data is truncated or the type tag does not match
  // the expected value.
  //
  class bit_buffer_reader
  {
  public:
    bit_buffer_reader (const uint8_t* data, std::size_t size_bytes)
      : reader_ (data, size_bytes)
    {
    }

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
    read_string (std::string&, std::size_t max_len);

    bool
    read_blob (std::vector<uint8_t>&);

    std::size_t
    position () const
    {
      return reader_.position ();
    }

    void
    set_position (std::size_t bit)
    {
      reader_.set_position (bit);
    }

    bit_reader&
    reader ()
    {
      return reader_;
    }

    const bit_reader&
    reader () const
    {
      return reader_;
    }

  private:
    bool verify_tag (bit_type_tag);

    bit_reader reader_;
  };

  // Construct a native bdBitBuffer from our writer.
  //
  // We allocates a engine-compatible structure using bdAlloc so that the engine
  // can consume it as if it were a real bdBitBuffer. Note that the caller is
  // responsible for the lifetime, typically the task takes ownership.
  //
  bd_bit_buffer*
  make_bit_buffer (const bit_buffer_writer&);
}
