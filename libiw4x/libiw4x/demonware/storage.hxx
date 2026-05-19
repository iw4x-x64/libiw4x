#pragma once

#include <libiw4x/demonware/types.hxx>

namespace iw4x::demonware
{
  namespace storage
  {
    void
    init ();

    // Return a reference to the storage stub.
    //
    // The lobby service needs a pointer to this for its sub-service slot.
    //
    bd_storage_stub&
    stub ();
  }
}
