#include <libiw4x/demonware/connection.hxx>

#include <cstring>

#include <tracy/Tracy.hpp>

#include <libiw4x/demonware/task.hxx>
#include <libiw4x/demonware/diagnostics.hxx>

using namespace std;

namespace iw4x::demonware
{
  alignas (16) bd_lobby_connection lobby_connection;

  namespace
  {
    // IW4 performs virtual dispatch on the connection object when it is being
    // destroyed. We provide a dummy destructor to catch this, and we fill the
    // rest of the vtable with safe stubs so the engine doesn't crash if it
    // tries to call into unidentified slots.
    //
    static constexpr size_t vtable_slots (32);
    void* connection_vtable[vtable_slots];

    int64_t
    connection_stub (void*)
    {
      return 0;
    }

    int64_t
    connection_destructor (bd_lobby_connection*)
    {
      // Our singleton is statically allocated and lives for the duration of the
      // process, so there is nothing to actually free here.
      //
      return 0;
    }
  }

  int32_t
  connection_start_task (void*   /* connection */,
                         void**  task_out,
                         uint8_t service_id,
                         uint8_t sub_func_id,
                         void*   payload,
                         float   /* timeout */)
  {
    ZoneScoped;

    auto task (task_manager::start_task (service_id, sub_func_id, payload));

    // The caller may or may not want a handle to the task. If the pointer
    // is non-null, hand it back.
    //
    if (task_out != nullptr)
      *task_out = task;

    return 0; // BD_NO_ERROR
  }

  void
  connection_init ()
  {
    ZoneScoped;

    auto stub (reinterpret_cast<void*> (&connection_stub));

    for (auto& e : connection_vtable)
      e = stub;

    connection_vtable[0] =
      reinterpret_cast<void*> (&connection_destructor);

    lobby_connection.vtable   = connection_vtable;
    lobby_connection.refcount = 0x7FFFFFFF; // never reaches zero

    memset (lobby_connection.body, 0, sizeof (lobby_connection.body));
  }
}
