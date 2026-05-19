#pragma once

#include <cstdint>
#include <functional>

#include <libiw4x/demonware/types.hxx>
#include <libiw4x/demonware/bit-buffer.hxx>

namespace iw4x::demonware
{
  // Service handler callback signature.
  //
  // A handler receives the service ID, sub-function ID, a reader over the
  // request payload, and a writer for the reply payload. It returns true if it
  // formulated a reply (even if the reply is "not found"). Returning false
  // signals that the request could not be handled at all and a generic success
  // reply should be fabricated.
  //
  using service_handler_t =
    std::function<bool (uint8_t  service_id,
                        uint8_t  sub_function_id,
                        bit_buffer_reader& request,
                        bit_buffer_writer& reply)>;

  // Construct a completed remote task.
  //
  // Creates a bd_remote_task in the "done" state with the given result
  // buffer and transaction ID. The engine will pick this up immediately
  // on its next pump cycle.
  //
  bd_remote_task*
  make_completed_task (bd_bit_buffer* result, uint64_t transaction_id);

  namespace task_manager
  {
    // Register a handler for a given service ID.
    //
    // Thread-safe: protected by a mutex internally.
    //
    void
    register_handler (uint8_t service_id, service_handler_t handler);

    // Dispatch a request and return a completed task.
    //
    // Unpacks the engine's bdBitBuffer payload, finds the registered
    // handler, lets it build a reply, and wraps everything into a
    // bd_remote_task. If no handler is found or the handler returns false,
    // a generic success reply is fabricated.
    //
    bd_remote_task*
    start_task (uint8_t service_id,
                uint8_t sub_function_id,
                void*   payload);

    // Install the startTask detour on the lobby connection.
    //
    void
    init ();
  }
}
