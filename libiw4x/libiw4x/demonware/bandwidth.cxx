#include <libiw4x/demonware/bandwidth.hxx>

#include <cstdint>
#include <cstring>
#include <stdexcept>

#include <tracy/Tracy.hpp>

#include <libiw4x/detour.hxx>
#include <libiw4x/import.hxx>

#include <libiw4x/utility-win32.hxx>

#include <libiw4x/demonware/diagnostics.hxx>

using namespace std;

namespace iw4x::demonware
{
  namespace
  {
    // bdBandwidthTestClient object layout (reverse-engineered).
    //
    //  0x00: vtable*
    //  0x10: controller_index (dword)
    //  0x14: initialized      (dword, 0=no, 1=yes)
    //  0x18: state            (dword, 0=idle, 1=requesting, 7=done)
    //  0x1C: status           (dword, 0=OK, 5=failed, 0x715=default)
    //  0x20: task_handle*     (bdRemoteTask*)
    //  0x68: socket-related pointer
    //

    SOCKET local_socket (INVALID_SOCKET);

    // We need a real bound UDP socket because the engine may peek at the socket
    // state through internal pointers. We bind to localhost on an ephemeral
    // port and make it non-blocking so we never accidentally stall the main
    // thread.
    //
    void
    create_local_socket ()
    {
      local_socket = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);

      if (local_socket == INVALID_SOCKET)
        throw demonware_error ("demonware: failed to create UDP socket");

      sockaddr_in addr {};
      addr.sin_family      = AF_INET;
      addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
      addr.sin_port        = 0; // Let the OS pick.

      if (::bind (local_socket,
                  reinterpret_cast<sockaddr*> (&addr),
                  sizeof (addr)) == SOCKET_ERROR)
      {
        closesocket (local_socket);
        local_socket = INVALID_SOCKET;
        throw demonware_error ("demonware: failed to bind UDP socket");
      }

      // Non-blocking so we never stall.
      //
      u_long mode (1);
      ioctlsocket (local_socket, FIONBIO, &mode);
    }

    bool
    bandwidth_init (void* self)
    {
      auto o (static_cast<uint8_t*> (self));

      // Already initialized? Match the original engine behavior and bail.
      //
      if (*reinterpret_cast<int32_t*> (o + 0x14) != 0)
        return false;

      if (local_socket == INVALID_SOCKET)
        create_local_socket ();

      *reinterpret_cast<int32_t*> (o + 0x14) = 1; // initialized
      *reinterpret_cast<int32_t*> (o + 0x1C) = 0; // status ok

      return true;
    }

    void
    bandwidth_start (void* self, int controller_index)
    {
      auto o (static_cast<uint8_t*> (self));

      if (*reinterpret_cast<int32_t*> (o + 0x14) == 0)
        return; // not initialized

      // Mark the test as done (state 7). When the pump switch evaluates this,
      // it naturally falls through to the default case and returns. The
      // Demonware frame handler's state-1 check then sees status==0 and reads
      // the zeroed bandwidth values.
      //
      *reinterpret_cast<int32_t*> (o + 0x18) = 7; // bd_done.
      *reinterpret_cast<int32_t*> (o + 0x1C) = 0; // status ok.
      *reinterpret_cast<int32_t*> (o + 0x10) = controller_index;
    }
  }

  void bandwidth::
  init ()
  {
    ZoneScoped;

    detour (bdBandwidthTestClientInit,  &bandwidth_init);
    detour (bdBandwidthTestClientStart, &bandwidth_start);
  }
}
