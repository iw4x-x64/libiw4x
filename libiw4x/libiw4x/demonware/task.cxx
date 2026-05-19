#include <libiw4x/demonware/task.hxx>

#include <map>
#include <mutex>
#include <cstring>

#include <tracy/Tracy.hpp>

#include <libiw4x/import.hxx>
#include <libiw4x/detour.hxx>

#include <libiw4x/demonware/connection.hxx>
#include <libiw4x/demonware/diagnostics.hxx>

using namespace std;

namespace iw4x::demonware
{
  // make_completed_task
  //

  bd_remote_task*
  make_completed_task (bd_bit_buffer* r, uint64_t id)
  {
    auto t (static_cast<bd_remote_task*> (bdAlloc (sizeof (bd_remote_task))));

    *t = bd_remote_task
    {
      .next           = nullptr,
      .timeout        = 0.0f,
      .status         = 2, // bd_done.
      .result_buffer  = r,
      .request_buffer = nullptr,
      .transaction_id = id,
      .reserved       = 0
    };

    // Bump the reference count on the result buffer. It is now conceptually
    // owned by both the task itself and the caller, so it has to survive
    // until both are finished with it.
    //
    if (r != nullptr)
      r->refcount = 2;

    return t;
  }

  // task_manager
  //

  namespace
  {
    // Service handler registry, protected by a mutex for thread safety
    // during registration and dispatch.
    //
    mutex handlers_mutex;
    map<uint8_t, service_handler_t> handlers;

    // Transaction ID counter.
    //
    // We start at 1 to avoid treating 0 as a valid transaction ID, which
    // could confuse some engine code paths.
    //
    uint64_t next_transaction_id (1);
  }

  void task_manager::
  register_handler (uint8_t service_id, service_handler_t handler)
  {
    lock_guard<mutex> lk (handlers_mutex);
    handlers[service_id] = move (handler);

    log::trace_l1 ("demonware: registered handler for service {}",
                   static_cast<int> (service_id));
  }

  bd_remote_task* task_manager::
  start_task (uint8_t service_id,
              uint8_t sub_function_id,
              void*   payload)
  {
    ZoneScoped;

    // We need to extract the underlying raw bytes from the game's
    // bdBitBuffer object. Since we receive this as an opaque payload and
    // don't want to rely on calling the game's native member functions, we
    // unpack the state directly using known memory offsets.
    //
    // The memory layout of the object:
    //
    //  0x00: vtable
    //  0x10: uint8_t* (pointer to the actual allocated buffer)
    //  0x20: int32_t  (current write position in bits)
    //  0x2D: uint8_t  (boolean flag indicating if type tags are used)
    //
    auto buf (static_cast<uint8_t*> (payload));

    auto data (buf != nullptr
               ? *reinterpret_cast<uint8_t**> (buf + 0x10)
               : nullptr);

    auto write_bits (buf != nullptr
                     ? *reinterpret_cast<int32_t*> (buf + 0x20)
                     : 0);

    auto data_size (static_cast<size_t> ((write_bits + 7) / 8));

    // If the payload is missing or empty, we point to a dummy byte so
    // the reader has something safe to look at.
    //
    static const uint8_t empty_byte (0);

    const uint8_t* req_data (data != nullptr && data_size > 0
                             ? data
                             : &empty_byte);

    size_t req_size (data != nullptr && data_size > 0
                     ? data_size
                     : 0);

    bool use_types (buf != nullptr && buf[0x2D] != 0);

    // Build a reader over the raw request data.
    //
    // The native bdBitBuffer constructor puts a 1-bit header at position 0
    // to indicate whether type checking is enabled. We skip past it so our
    // reads correctly align with the first real type tag.
    //
    bit_buffer_reader req (req_data, req_size);
    req.set_position (use_types ? 1 : 0);

    // Dispatch to whoever is registered to handle this service.
    //
    bit_buffer_writer rep;

    {
      lock_guard<mutex> lk (handlers_mutex);
      auto it (handlers.find (service_id));

      if (it != handlers.end ())
      {
        log::trace_l1 ("demonware: dispatching service={} sub={}",
                    static_cast<int> (service_id),
                    static_cast<int> (sub_function_id));

        it->second (service_id, sub_function_id, req, rep);
      }
      else
      {
        log::trace_l1 ("demonware: no handler for service={} sub={}",
                    static_cast<int> (service_id),
                    static_cast<int> (sub_function_id));
      }
    }

    // If the handler didn't write a reply (or we had no handler at all),
    // fabricate a generic success response. The engine is generally happy
    // with "no error, no results."
    //
    if (rep.size () == 0)
    {
      rep.write_uint32 (0); // BD_NO_ERROR.
      rep.write_uint8 (0);  // No results.
    }

    auto tid (next_transaction_id++);
    auto res (make_bit_buffer (rep));

    return make_completed_task (res, tid);
  }

  void task_manager::
  init ()
  {
    ZoneScoped;

    detour (bdLobbyConnectionStartTask, &connection_start_task);
  }
}
