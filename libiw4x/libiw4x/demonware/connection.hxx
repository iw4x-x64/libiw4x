#pragma once

#include <cstdint>

#include <libiw4x/demonware/types.hxx>

namespace iw4x::demonware
{
  // The global lobby connection singleton.
  //
  // IW4 expects exactly one bdLobbyConnection object to exist for the lifetime
  // of the process. We create it statically and pin its reference count to
  // INT32_MAX so it never gets destroyed.
  //
  extern bd_lobby_connection lobby_connection;

  // Initialize the lobby connection singleton.
  //
  // Fills the vtable with safe stubs, zeros the body, and pins the
  // reference count. Must be called before anything touches the
  // connection object.
  //
  void
  connection_init ();

  // The startTask hook.
  //
  // This is what the engine calls when it wants to send a request through
  // the lobby connection. We intercept it and route it through our task
  // manager instead of actually going over the network.
  //
  int32_t
  connection_start_task (void*    connection,
                         void**   task_out,
                         uint8_t  service_id,
                         uint8_t  sub_func_id,
                         void*    payload,
                         float    timeout);
}
