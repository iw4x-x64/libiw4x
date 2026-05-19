#include <libiw4x/demonware/auth.hxx>

#include <functional>
#include <random>
#include <string>

#include <tracy/Tracy.hpp>

#include <libiw4x/utility-win32.hxx>

#include <libiw4x/demonware/diagnostics.hxx>

using namespace std;

namespace iw4x::demonware
{
  namespace
  {
    bd_auth_ticket auth_ticket_ {};
    bool initialized_ (false);
  }

  void auth::
  init ()
  {
    ZoneScoped;

    auth_ticket_.title_id = 0x7DA; // IW4 (2010).

    // Derive the user identity from the local machine's hostname. This is
    // a reasonable default for LAN play and development. For anything more
    // serious we would need a proper account system, but that is a problem
    // for another day.
    //
    // @@ TODO: Think about how we want to handle unique identity long-term.
    //          A hostname hash is fine for now but collisions are possible
    //          on a busy LAN.
    //
    char hn[256] {};
    DWORD sz (sizeof (hn));
    GetComputerNameA (hn, &sz);

    hash<string> hs;
    auth_ticket_.user_id = hs (string (hn, sz));

    // The session array name field is only 0x10 bytes, so we truncate the
    // hostname to 15 characters plus the null terminator to use it as the
    // player's display name.
    //
    // @@ TODO: Check if this 15-character limit is actually sufficient.
    //          Between ANSI color codes, longer usernames, and multi-byte
    //          UTF-8 sequences for internationalized names, we might find
    //          ourselves truncating prematurely. We should probably keep an
    //          eye on this and see if we need a more dynamic approach or at
    //          least a more generous buffer.
    //
    strncpy (auth_ticket_.username,
             hn,
             sizeof (auth_ticket_.username) - 1);

    auth_ticket_.username[sizeof (auth_ticket_.username) - 1] = '\0';

    // Fill the session key and ticket data with random bytes. Since these
    // values are opaque to the game and only inspected by the lobby service
    // (which we control), any random data will do.
    //
    random_device rd;
    seed_seq ss {rd (), rd (), rd (), rd ()};
    independent_bits_engine<mt19937_64, 8, uint32_t> rng (ss);

    ranges::generate (auth_ticket_.session_key, ref (rng));
    ranges::generate (auth_ticket_.ticket_data, ref (rng));

    initialized_ = true;

    log::info ("demonware: user_id={} username='{}'",
               auth_ticket_.user_id,
               auth_ticket_.username);
  }

  const bd_auth_ticket& auth::
  ticket ()
  {
    return auth_ticket_;
  }
}
