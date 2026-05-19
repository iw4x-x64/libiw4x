#include <libiw4x/mod/demonware.hxx>

#include <cstdint>
#include <cstring>

#include <tracy/Tracy.hpp>

#include <libiw4x/detour.hxx>
#include <libiw4x/import.hxx>

#include <libiw4x/utility-win32.hxx>

#include <libiw4x/demonware/auth.hxx>
#include <libiw4x/demonware/bandwidth.hxx>
#include <libiw4x/demonware/diagnostics.hxx>
#include <libiw4x/demonware/lobby.hxx>
#include <libiw4x/demonware/log.hxx>
#include <libiw4x/demonware/storage.hxx>
#include <libiw4x/demonware/task.hxx>
#include <libiw4x/demonware/types.hxx>

using namespace std;

namespace iw4x::mod
{
  namespace
  {
    // The idea with the 'uk_' (unknown) prefix is to mark undocumented memory
    // addresses discovered via reverse engineering. Note that their names,
    // types, and perceived semantics (for example, "state") are essentially
    // just our best guesses based on observed behavior. In other words, there
    // is no guarantee they are actually correct.

    auto& uk_state                (*reinterpret_cast<int32_t*>  (0x141646308));
    auto& uk_disable              (*reinterpret_cast<bool*>     (0x1416462E9));
    auto& uk_session_signin       (*reinterpret_cast<uint8_t*>  (0x141A4DA7C));
    auto& uk_session_signin_state (*reinterpret_cast<int32_t*>  (0x141A4dA84));
    auto& uk_xprivileg_state      (*reinterpret_cast<uint8_t*>  (0x141A4A990 + 0xC0));
    auto& uk_xprivileg_cache      (*reinterpret_cast<uint8_t*>  (0x141A4A990 + 0xC0 + 2));
    auto& uk_retry_count          (*reinterpret_cast<int32_t*>  (0x1416462C4));
    auto& uk_timestamp            (*reinterpret_cast<int32_t*>  (0x1416462C8));

    auto& dw_lobby_service        (*reinterpret_cast<void**>    (0x1416462D0));
    auto& dw_socket_router        (*reinterpret_cast<void**>    (0x1416462D8));

    auto& g_haveValidPlaylists    (*reinterpret_cast<uint8_t*>  (0x1467E86B0));
//  auto& s_playlistOutOfDate     (*reinterpret_cast<uint8_t*>  (0x141C39F60));
    auto& uk_playlist_state       (*reinterpret_cast<int32_t*>  (0x140465EB8));

    // Session stub.
    //
    // The connectivity gate at 0x1401B5A50 insists that the session pointer is
    // non-null, and 0x1401B5BE3 dereferences [ptr + 0x28], so that slot must be
    // either null (causing a graceful failure) or point to a valid refcounted
    // object. We just give it a zeroed buffer.
    //
    alignas (16) uint8_t session_stub[0x30] {};

    // Socket router stub.
    //
    // We route all virtual calls to a single no-op so the engine can tear
    // down or query the router without crashing.
    //
    alignas (16) demonware::bd_router router {};

    static constexpr size_t router_vtable_slots (64);
    void* router_vtable[router_vtable_slots];

    int64_t
    router_stub (void*)
    {
      return 0;
    }

    void
    init_router ()
    {
      auto stub (reinterpret_cast<void*> (&router_stub));

      for (auto& e : router_vtable)
        e = stub;

      router.vtable = router_vtable;
    }

    // Socket receive filter.
    //
    // We peek at the first byte of every pending datagram before deciding
    // whether to consume it. STUN packets (top two bits of the first byte are
    // always 00) belong to bdNet and are passed through to the original
    // receiveFrom so NAT traversal continues to work. All other packets (OOB
    // 0xFF and Netchan game traffic) are left in the kernel socket buffer by
    // returning -2 (bd_wouldblock) without consuming them. NET_GetPacket then
    // reads them directly via recvfrom(), processes them in the same frame, and
    // feeds them to SV_PacketEvent / CL_PacketEvent with correct sequencing.
    //
    // XXX: Actually, this is a lot more brittle than one would expect. Ideally,
    // we should probably just completely ignore `dw`. The problem, however, is
    // that it is not entirely clear if it relies on some of this state here and
    // there to keep working. Needs to be investigated at some point.
    //
    using bdSocketReceiveFrom_t =
      int (__thiscall*) (void* self, void* out_addr,
                         void* out_buf, int buf_size);

    bdSocketReceiveFrom_t bdSocketReceiveFrom (
      reinterpret_cast<bdSocketReceiveFrom_t> (0x140371fc0));

    int __thiscall
    socket_receive_from (void* self, void* out_addr,
                         void* out_buf, int buf_size)
    {
      // Socket fd lives at bdSocket + 0x08.
      //
      auto fd (*reinterpret_cast<int32_t*> (
        static_cast<char*> (self) + 0x08));

      if (fd == -1)
        return -2;

      // Peek at the first byte without consuming the datagram.
      //
      unsigned char first_byte (0);
      int peeked (recv (static_cast<SOCKET> (static_cast<uint32_t> (fd)),
                        reinterpret_cast<char*> (&first_byte),
                        1,
                        MSG_PEEK));

      if (peeked <= 0)
      {
        // WSAEMSGSIZE just means the peek saw a datagram larger than our 1-byte
        // buffer, which is expected. Anything else means no data.
        //
        if (peeked == -1 && WSAGetLastError () == WSAEMSGSIZE)
          ; // fall through to the STUN check
        else
          return -2;
      }

      // STUN packets have their top two bits == 00 (RFC 5389). Pass them
      // through to bdNet for NAT traversal.
      //
      if ((first_byte & 0xC0) == 0x00)
        return bdSocketReceiveFrom (self, out_addr, out_buf, buf_size);

      // Everything else can be treated as netchan, leave it in the socket
      // buffer for NET_GetPacket.
      //
      return -2;
    }

    // After a friend connects, DW_SendPush (called every frame) ends up
    // invoking bdSocketRouter::connect on the real bdSocketRouter. That
    // triggers NAT traversal with an invalid socket and bogus addresses
    // (2.0.0.0:0), which loops "Failed to send. Invalid socket?". We just
    // stub it out.
    //
    using bdSocketRouterConnect_t =
      bool (__thiscall*) (void* self, void* addr_handle_ref);

    bdSocketRouterConnect_t bdSocketRouterConnect (
      reinterpret_cast<bdSocketRouterConnect_t> (0x140335340));

    bool __thiscall
    socket_router_connect (void*, void*)
    {
      return false;
    }

    // Engine callback accessors.
    //
    // We call these purely for their side effects: the engine requires the
    // internal state modifications that happen inside these getters before
    // we actually trigger certain operations. Yes, it is as bizarre as it
    // sounds.
    //
    using is_session_clear_t      = bool (*) (int);
    using get_state_t             = int (*) ();
    using has_active_slot_t       = bool (*) (void*, int, int);
    using readiness_check_t       = bool (*) (int);
    using get_status_accessor_t   = bool (*) (int);
    using any_session_signed_in_t = bool (*) ();

    is_session_clear_t    is_session_clear (
      reinterpret_cast<is_session_clear_t> (0x1401B5AC0));

    get_state_t           uk_get_state (
      reinterpret_cast<get_state_t> (0x14012F720));

    has_active_slot_t     has_active_slot (
      reinterpret_cast<has_active_slot_t> (0x1402005C0));

    readiness_check_t     readiness_check (
      reinterpret_cast<readiness_check_t> (0x1401FE180));

    get_status_accessor_t get_status_accessor (
      reinterpret_cast<get_status_accessor_t> (0x1401358E0));

    any_session_signed_in_t any_session_signed_in (
      reinterpret_cast<any_session_signed_in_t> (0x1401b5a70));

    // Playlist and stats state tracking.
    //
    int32_t last_playlist_state   (-1);
    int32_t last_stats_game_state (-1);
    uint8_t last_stats_playlists  (0xFF);

    // The main frame hook.
    //
    // This is where we spoof all the engine state that the connectivity
    // gates expect. Every single one of these assignments was reverse-
    // engineered by tracing through the gate checks and figuring out what
    // values they want to see. It is not pretty, but it works.
    //
    void
    iwnet_frame (int controller)
    {
      ZoneScoped;

      // Force the state variable to 0x08. We are not entirely sure what
      // this state represents in the grand scheme of things, but this is
      // the exact value the game expects. It might simply be another GDK
      // peculiarity we have to accommodate.
      //
      uk_state = 8;

      // Clear the disable flag. The game tends to set this to true
      // immediately before calling into the Demonware service, which
      // completely blocks progress. Overriding it here solves the lockup.
      //
      uk_disable = false;

      // Force a session sign-in byte on every tick. Testing shows that
      // Live_Frame silently skips important logic if this isn't set.
      //
      uk_session_signin = 1;

      // The connectivity gate at 0x1402A6F40 (check 2) calls into
      // 0x1401B5AA0, which stubbornly expects this state to be 3 (signed
      // in). Under normal conditions the GDK sign-in flow at 0x1401b62E0
      // handles this. But we have stripped the GDK initialization out
      // entirely, so we spoof it manually.
      //
      uk_session_signin_state = 3;

      // Multiplayer privilege cache entry for controller 0.
      //
      // The third check in the connectivity gate at 0x1401b5E90 verifies
      // multiplayer privileges by indexing into the session array at
      // 0x141A4A990. The index logic is roughly (privilege_id + 3) * 12,
      // so for privilege 13 (XPRIVILEGE_MULTIPLAYER_SESSIONS) we land at
      // offset 0xC0. Byte +2 acts as a "checked" flag.
      //
      uk_xprivileg_state = 1;
      uk_xprivileg_cache = 1;

      // One-time initialization on the first frame after we gain control.
      //
      if (static bool connected (false); !connected)
      {
        connected = true;

        // Disable the automated background retry mechanism. We are driving
        // the Demonware pump manually within our own frame loop, so the
        // original background retry system is completely redundant and
        // often conflicts with our state.
        //
        uk_retry_count = 0;
        uk_timestamp   = 0;

        Uk_OnConnected (controller);

        // Populate the session name. This must happen after the connected
        // callback, otherwise the game immediately zeroes out the session
        // array and wipes our string.
        //
        auto sn (reinterpret_cast<char*> (0x141A4A998 + 0x30D4));
        strncpy (sn, demonware::auth::ticket ().username, 16 - 1);
        sn[16 - 1] = '\0';

        uk_session_signin = 1;

        log::info ("demonware: first frame: connection established for '{}'",
                   demonware::auth::ticket ().username);
      }

      // Pump the task completion array.
      //
      DW_SendPush ();
    }

    // Playlist fetch wrapper.
    //
    // We evaluate the session and state getters purely for their side
    // effects before triggering the actual fetch. The engine requires
    // these internal state modifications to happen first.
    //
    void
    live_storage_fetch_playlists (int controller)
    {
      if (uk_playlist_state != last_playlist_state)
      {
        last_playlist_state = uk_playlist_state;

        (void) is_session_clear (controller);
        (void) uk_get_state ();
      }

      LiveStorage_FetchPlaylists (controller);
    }

    // Stats download wrapper.
    //
    // Similar to the playlist fetch: we tick various internal engine
    // states related to profile slot activity and general readiness
    // before letting the actual download proceed.
    //
    void
    live_storage_download_stats (int controller)
    {
      if (uk_playlist_state != last_stats_game_state ||
          g_haveValidPlaylists != last_stats_playlists)
      {
        last_stats_game_state = uk_playlist_state;
        last_stats_playlists = g_haveValidPlaylists;

        has_active_slot (reinterpret_cast<void*> (0x141C39FB0), 1, controller);
        readiness_check (controller);
        is_session_clear (controller);
        get_status_accessor (controller);
      }

      LiveStorage_DownloadStatsFromDir (controller);
    }

    // Client connect wrapper.
    //
    void*
    client_connect (int controller, uint16_t mode)
    {
      return ClientConnect (controller, mode);
    }

    // Live frame wrapper.
    //
    // Poke the session gate to keep the internal sign-in watchdog happy,
    // then let the original Live_Frame proceed.
    //
    void
    live_frame (int controller)
    {
      (void) any_session_signed_in ();
      Live_Frame (controller);
    }
  }

  demonware_module::
  demonware_module ()
  {
    ZoneScoped;

    // Core services.
    //
    demonware::auth::init ();
    demonware::lobby::init ();
    demonware::task_manager::init ();
    demonware::storage::init ();
    demonware::bandwidth::init ();

    // Socket router.
    //
    init_router ();
    dw_socket_router = &router;
    dw_lobby_service = &demonware::lobby::impl ();

    uk_state = 8;
    uk_disable = false;

    // Session stub.
    //
    auto& session_ptr (*reinterpret_cast<void**> (0x141a4a998));
    auto& session_context (*reinterpret_cast<uint64_t*> (0x141a4a9a0));

    session_ptr = session_stub;
    session_context = demonware::auth::ticket ().user_id;

    detour (bdLogMessage,                     &demonware::bd_log_message);
    detour (IWNet_Frame,                      &iwnet_frame);
    detour (LiveStorage_FetchPlaylists,       &live_storage_fetch_playlists);
    detour (LiveStorage_DownloadStatsFromDir, &live_storage_download_stats);
    detour (ClientConnect,                    &client_connect);
    detour (Live_Frame,                       live_frame);
    detour (bdSocketRouterConnect,            socket_router_connect);
    detour (bdSocketReceiveFrom,              socket_receive_from);
  }
}
