#include <libiw4x/demonware/lobby.hxx>

#include <cstring>

#include <tracy/Tracy.hpp>

#include <libiw4x/import.hxx>
#include <libiw4x/detour.hxx>

#include <libiw4x/demonware/connection.hxx>
#include <libiw4x/demonware/diagnostics.hxx>
#include <libiw4x/demonware/storage.hxx>

using namespace std;

namespace iw4x::demonware
{
  namespace
  {
    alignas (16) bd_lobby_service_impl lobby_service_impl_;

    // The vtable layout matches the original game at 0x1403DBAC0. We only
    // have four slots to worry about:
    //
    //   [0] destructor   (0x14031C4F0)
    //   [1] onConnect    (0x14031D090)
    //   [2] pump         (0x140326620)
    //   [3] onDisconnect (0x14031D320)
    //
    void lobby_destructor    (bd_lobby_service_impl*) {}
    void lobby_on_connect    (bd_lobby_service_impl*) {}
    void lobby_pump          (bd_lobby_service_impl*) {}
    void lobby_on_disconnect (bd_lobby_service_impl*) {}

    void* lobby_vtable[4]
    {
      reinterpret_cast<void*> (&lobby_destructor),
      reinterpret_cast<void*> (&lobby_on_connect),
      reinterpret_cast<void*> (&lobby_pump),
      reinterpret_cast<void*> (&lobby_on_disconnect)
    };

    // Non-virtual member function hooks.
    //
    // These are the functions that the engine calls through direct pointers
    // rather than through the vtable. We hook them with detours.
    //

    bool
    lobby_connect (void*, void*, void*, bool)
    {
      // Pretend the connection succeeds. We are always online from the engine's
      // perspective.
      //
      return true;
    }

    void
    lobby_disconnect (void*, int /* reason */)
    {
      // Nothing to tear down.
      //
    }

    int32_t
    lobby_get_status (void*)
    {
      // Always return BD_ONLINE (value 2).
      //
      return 2;
    }

    void*
    lobby_get_matchmaking (void*)
    {
      return lobby_service_impl_.matchmaking;
    }

    void*
    lobby_get_task_mgr (void*)
    {
      return lobby_service_impl_.task_mgr;
    }

    void*
    lobby_get_performance (void*)
    {
      return lobby_service_impl_.performance;
    }

    void*
    lobby_get_storage (void*)
    {
      return lobby_service_impl_.storage;
    }

    void*
    lobby_instance ()
    {
      return &lobby_service_impl_;
    }
  }

  void lobby::
  init ()
  {
    ZoneScoped;

    // Initialize the connection first since the lobby service points to it.
    //
    connection_init ();

    memset (&lobby_service_impl_, 0, sizeof (lobby_service_impl_));

    lobby_service_impl_.vtable = lobby_vtable;
    lobby_service_impl_.connection = &lobby_connection;

    // Point the storage sub-service at our storage stub. The actual storage
    // logic is handled by the storage module's registered handler and this
    // pointer just satisfies the engine's null checks.
    //
    lobby_service_impl_.storage = &storage::stub ();

    detour (bdLobbyService,                   &lobby_instance);
    detour (bdLobbyServiceImplGetMatchmaking, &lobby_get_matchmaking);
    detour (bdLobbyServiceImplGetPerformance, &lobby_get_performance);
    detour (bdLobbyServiceImplGetStatus,      &lobby_get_status);
    detour (bdLobbyServiceImplGetStorage,     &lobby_get_storage);
    detour (bdLobbyServiceImplGetTaskMgr,     &lobby_get_task_mgr);
    detour (bdLobbyServiceImplConnect,        &lobby_connect);
    detour (bdLobbyServiceImplDisconnect,     &lobby_disconnect);
  }

  bd_lobby_service_impl& lobby::
  impl ()
  {
    return lobby_service_impl_;
  }
}
