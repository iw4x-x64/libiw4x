#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace iw4x::demonware
{
  // bdLobbyConnection compatible object layout.
  //
  // The engine performs virtual dispatch on the connection object during
  // destruction and possibly at other points. We provide a fake vtable
  // filled with safe stubs so that nothing crashes when the engine pokes
  // at it.
  //
  //  0x00: vtable pointer
  //  0x08: reference count (int32)
  //  0x0C: body (zero-initialized, opaque to us)
  //
  struct bd_lobby_connection
  {
    void*    vtable;
    int32_t  refcount;
    uint8_t  body[0xF4];
  };

  static_assert (sizeof (bd_lobby_connection) == 0x100);

  // bdLobbyServiceImpl compatible object layout.
  //
  // This is a 0x140-byte structure that mirrors the bdLobbyServiceImpl
  // singleton at 0x140714A50. The vtable at offset 0x00 has four entries:
  // destructor, onConnect, pump, and onDisconnect. The inline
  // bdRemoteTaskManager lives at 0x18. The sub-service pointers (matchmaking,
  // storage, performance) are scattered at 0x40, 0x60, and 0x80, while our
  // bdLobbyConnection pointer sits at 0x90.
  //
  struct bd_lobby_service_impl
  {
    void*                vtable;            // 0x00: 4-slot vtable.
    uint8_t              net[0x10];         // 0x08: network/socket data.
    uint8_t              task_mgr[0x20];    // 0x18: bdRemoteTaskManager.
    int32_t              flags;             // 0x38
    int32_t              pad0;              // 0x3C
    void*                matchmaking;       // 0x40: bdMatchMaking*.
    uint8_t              mm_reserved[0x18]; // 0x48
    void*                storage;           // 0x60: bdStorage*.
    uint8_t              st_reserved[0x18]; // 0x68
    void*                performance;       // 0x80: bdPerformance*.
    int32_t              flags2;            // 0x88
    int32_t              pad1;              // 0x8C
    bd_lobby_connection* connection;        // 0x90: ref-counted.
    uint8_t              tail[0xA4];        // 0x98
    int32_t              flags3;            // 0x13C
  };

  static_assert (sizeof (bd_lobby_service_impl) == 0x140);

  // bdStorage compatible object layout.
  //
  // A minimal stub that the engine checks for a status field and a
  // connection pointer. We don't need anything fancier since we handle
  // all the actual storage logic ourselves.
  //
  struct bd_storage_stub
  {
    int32_t status;
    int32_t pad;
    void*   connection;
  };

  static_assert (sizeof (bd_storage_stub) == 0x10);

  // bdBitBuffer compatible object layout.
  //
  // The native bdBitBuffer is a refcounted, tagged, bitwise serialization
  // buffer. We reconstruct it from scratch when building reply payloads for
  // the engine. The layout below was reverse-engineered from the constructor
  // at the vtable address 0x1403DA2D0.
  //
  //  0x00: vtable pointer
  //  0x08: reference count
  //  0x10: data pointer (heap-allocated byte array)
  //  0x18: capacity in bytes
  //  0x1C: element count (allocated size in bytes)
  //  0x20: write position (bits)
  //  0x24: max write position (high water mark, bits)
  //  0x28: read position (bits)
  //  0x2C: flags
  //  0x2D: type checking enabled
  //
  struct bd_bit_buffer
  {
    void*    vtable;
    int32_t  refcount;
    int32_t  pad0;
    uint8_t* data;
    int32_t  capacity;
    int32_t  element_count;
    int32_t  write_position;
    int32_t  max_write_position;
    int32_t  read_position;
    uint8_t  flags;
    uint8_t  type_checking;
    uint16_t pad1;
  };

  static_assert (sizeof (bd_bit_buffer) == 0x30);

  // bdRemoteTask compatible object layout.
  //
  // The engine chains these into a singly-linked list and polls them until
  // their status field transitions to 2 (bd_done). We always create them
  // in the completed state so the engine picks them up immediately.
  //
  struct bd_remote_task
  {
    bd_remote_task* next;
    float           timeout;
    int32_t         status;          // 2 = bd_done.
    bd_bit_buffer*  result_buffer;
    bd_bit_buffer*  request_buffer;
    uint64_t        transaction_id;
    uint64_t        reserved;
  };

  // Authentication ticket.
  //
  // Contains the identity information for the local player. The session key
  // and ticket data are filled with random bytes since the lobby service
  // (which we control) never actually validates them. What matters is the
  // user_id and username as those are what the engine uses for player
  // identification.
  //
  struct bd_auth_ticket
  {
    uint64_t title_id;
    uint64_t user_id;

    char username[16];

    std::array<uint8_t, 128> session_key;
    std::array<uint8_t, 256> ticket_data;
  };

  // bdSocketRouter compatible object layout.
  //
  // A minimal stub that the engine's pointer checks require to be non-null.
  // We fill it with a dummy vtable so virtual calls during teardown or query
  // don't crash.
  //
  struct bd_router
  {
    void*   vtable;
    uint8_t body[0x1F8];
  };

  static_assert (sizeof (bd_router) == 0x200);

  // Wire-format type tags for the bit-level serialization protocol.
  //
  // Each typed field is prefixed by a 5-bit tag that identifies the type of
  // the data that follows. These values are dictated by the Demonware wire
  // format and must not be changed.
  //
  enum class bit_type_tag : uint8_t
  {
    boolean  = 1,
    byte     = 3,
    int16    = 5,
    uint16   = 6,
    int32    = 7,
    uint32   = 8,
    int64    = 9,
    uint64   = 10,
    floating = 13,
    string   = 16,
    blob     = 19
  };

  // Wire-format type tags for the byte-level serialization protocol.
  //
  // Similar idea to the bit-level tags but here each tag is a full byte.
  // Used by the bdByteBuffer family which is a simpler, byte-aligned
  // serialization format.
  //
  enum class byte_type_tag : uint8_t
  {
    boolean       = 1,
    integer32     = 3,
    integer64     = 5,
    floating      = 6,
    string        = 7,
    blob          = 8,
    struct_header = 10,
    array_count   = 16
  };

  // bdStorage sub-function identifiers.
  //
  // These correspond to the RPC sub-function IDs that the engine sends when
  // it wants to interact with the storage service. We dispatch on these in
  // the storage request handler.
  //
  struct storage_func
  {
    static constexpr uint8_t set_file               = 1;
    static constexpr uint8_t set_user_file           = 2;
    static constexpr uint8_t get_file               = 5;
    static constexpr uint8_t get_user_file           = 7;
    static constexpr uint8_t get_publisher_file_info = 8;
  };

  // Service identifiers.
  //
  struct service_id
  {
    static constexpr uint8_t storage = 10;
  };

  // Synthetic file identifiers.
  //
  // Since we are not a real Demonware backend, we don't have proper database
  // file IDs. Instead we use these synthetic IDs to distinguish between
  // different types of storage files during download requests.
  //
  struct file_id
  {
    static constexpr uint64_t publisher = 1;
    static constexpr uint64_t user      = 2;
  };
}
