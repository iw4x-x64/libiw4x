#pragma once

#include <libiw4x/demonware/types.hxx>

namespace iw4x::demonware
{
  // Authentication service.
  //
  // Note that for our purposes we just fabricate a ticket from the local
  // machine's hostname. The session key and ticket data are random garbage that
  // nobody ever validates because the only "server" that sees them is us.
  //
  namespace auth
  {
    // Initialize the authentication ticket.
    //
    // Derives the user_id from a hash of the local hostname, truncates the
    // hostname to fit the 16-byte session array name field, and fills the
    // session key and ticket data with random bytes.
    //
    void
    init ();

    // Return a reference to the authentication ticket.
    //
    // The ticket is valid for the lifetime of the process.
    //
    const bd_auth_ticket&
    ticket ();
  }
}
