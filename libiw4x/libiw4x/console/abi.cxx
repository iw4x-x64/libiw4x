#include <libiw4x/console/abi.hxx>

#include <array>
#include <cstring>
#include <inplace_vector>
#include <ranges>

#include <libiw4x/import.hxx>

#include <libiw4x/dvar/abi.hxx>
#include <libiw4x/dvar/core.hxx>
#include <libiw4x/dvar/utility.hxx>

using namespace std;

namespace iw4x::console
{
  void
  execute (string_view c) noexcept
  {
    constexpr size_t m (1024);

    inplace_vector<char, m> cmd;
    for (auto v (c | ranges::views::take (m - 2)); const char x : v)
      cmd.unchecked_push_back (x);

    // Make sure the command is newline and null terminated, as required
    // by the engine buffer.
    //
    cmd.unchecked_push_back ('\n');
    cmd.unchecked_push_back ('\0');

    Cbuf_AddText (0, cmd.data ());
  }

  namespace
  {
    // The engine requires a persistent cmd_function_s struct for every
    // registered command. It links these into an internal list, meaning the
    // memory must remain valid for the lifetime of the process.
    //
    // XXX: Bump this if we need more slots.
    //
    constexpr size_t k_command_slots (64);

    array<cmd_function_s, k_command_slots>&
    command_pool () noexcept
    {
      static array<cmd_function_s, k_command_slots> pool;
      return pool;
    }

    size_t&
    command_pool_used () noexcept
    {
      static size_t used (0);
      return used;
    }
  }

  bool
  register_engine_command (const char* name, engine_command_fn fn) noexcept
  {
    size_t& used (command_pool_used ());

    if (used >= k_command_slots)
      return false;

    // Grab the next available slot and make sure we zero it out. The engine
    // relies on certain fields (like the internal 'next' pointers) being
    // null when it inserts this struct into its command linked list.
    //
    cmd_function_s& slot (command_pool () [used]);
    memset (&slot, 0, sizeof (slot));

    Cmd_AddCommandInternal (name, reinterpret_cast<void (*) ()> (fn), &slot);

    ++used;
    return true;
  }

  bool engine_dvar_gateway::
  exists (const dvar_name& name) const
  {
    dvar::read_lock lock;

    return dvar::find_raw (name.str ().c_str ()) != nullptr;
  }

  optional<string> engine_dvar_gateway::
  get (const dvar_name& name) const
  {
    dvar::read_lock lock;

    dvar_t* d (dvar::find_raw (name.str ().c_str ()));

    // It is entirely possible the dvar was removed or never existed in the
    // first place, so we handle the null case gracefully.
    //
    if (d == nullptr)
      return nullopt;

    // The value_to_string function returns borrowed storage. In the
    // formatted-value cases this storage may be backed by a rotating
    // thread-local buffer, so do not let the raw pointer escape this scope.
    // Copy the bytes into the returned string while the buffer contents are
    // still the ones we asked for.
    //
    const char* s (dvar::value_to_string (d, d->current));
    return string (s != nullptr ? s : "");
  }

  dvar_set_outcome engine_dvar_gateway::
  set (const dvar_name& name, string_view value)
  {
    dvar_t* d (nullptr);

    {
      // We only hold the read lock while resolving the dvar pointer. We must
      // release the lock before calling set_command below, because the engine
      // handles its own locking and state modification during the set operation.
      // Keeping our lock would likely result in a deadlock.
      //
      dvar::read_lock lock;
      d = dvar::find_raw (name.str ().c_str ());
    }

    if (d == nullptr)
      return dvar_set_outcome (dvar_set_status::not_found, nullopt);

    // Read-only (ROM) dvars cannot be altered from the console.
    //
    if (dvar::flag_set (d->flags, DVAR_ROM))
      return dvar_set_outcome (dvar_set_status::read_only, nullopt);

    // We forward to the dvar layer, which parses the string against the
    // dvar's type and clamps it to its domain. We mirror the engine's
    // accepting semantics here; finer type/domain rejection is the dvar
    // layer's own concern.
    //
    const string text (value);
    dvar::set_command (name.str ().c_str (), text.c_str ());

    return dvar_set_outcome (dvar_set_status::ok, nullopt);
  }

  void engine_dvar_gateway::
  enumerate (const function<void (const dvar_descriptor&)>& fn) const
  {
    // The for_each function holds the read lock and walks the engine's dvar
    // table in sorted order, invoking our trampoline once per dvar. Note that
    // the callback must not register or remove dvars, as it would deadlock.

    // The C-style callback API expects a raw function pointer and a user
    // context. Since we cannot pass a stateful lambda directly as a function
    // pointer, we pack our callback into this context struct and pass it via
    // the user data pointer. The stateless thunk then unwraps it.
    //
    struct context
    {
      const function<void (const dvar_descriptor&)>* fn;
    };

    context ctx (&fn);

    auto thunk ([] (const dvar_t* d, void* user)
    {
      auto* c (static_cast<context*> (user));

      // Sanity check. The engine should not really give us null pointers here
      // during a table walk, but it is better to be safe than to segfault
      // if the engine's internal table state gets corrupted.
      //
      if (d == nullptr || d->name == nullptr)
        return;

      dvar_descriptor desc;
      desc.name = dvar_name (d->name);

      const char* value (dvar::value_to_string (d, d->current));
      desc.current_value = (value != nullptr ? value : "");

      const char* descr (dvar::get_description (d));
      desc.description = (descr != nullptr ? descr : "");

      (*c->fn) (desc);
    });

    dvar::for_each (reinterpret_cast<dvar::for_each_fn> (
      static_cast<void (*) (const dvar_t*, void*)> (thunk)), &ctx);
  }
}
