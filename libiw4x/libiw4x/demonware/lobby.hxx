#pragma once

#include <libiw4x/demonware/types.hxx>

namespace iw4x::demonware
{
  namespace lobby
  {
    void
    init ();

    // Return a reference to the lobby service implementation singleton.
    //
    bd_lobby_service_impl&
    impl ();
  }
}
