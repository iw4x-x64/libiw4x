#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include <libiw4x/console/core.hxx>
#include <libiw4x/console/types.hxx>

namespace iw4x::console
{
  // Submit a command line to the engine's command buffer.
  //
  // We forward the text verbatim to Cbuf_AddText for the local client.
  // This is how our console executes engine-native commands that it doesn't
  // own. Note that the engine copies the text, so our borrowed view doesn't
  // need to outlive this call.
  //
  void
  execute (std::string_view command_line) noexcept;

  // Console-owned engine command entry point.
  //
  // The engine invokes registered commands through a parameterless function.
  // The thunk will read its arguments from the engine's current command context
  // (which is the same cmd_args that the dvar layer uses) and forward them
  // directly into the console.
  //
  using engine_command_fn = void (*) ();

  // Register a command with the engine using the official machinery.
  //
  // We use Cmd_AddCommandInternal with storage that this layer owns. Keep
  // in mind that the engine keeps a pointer to the cmd_function_s we supply,
  // so it must have a static lifetime. We return false if the console's
  // command-slot pool is exhausted, which the caller should treat as a
  // registration_error.
  //
  // We deliberately don't bypass this machinery since the engine remains
  // the system of record for command existence and lookup.
  //
  bool
  register_engine_command (const char* name, engine_command_fn fn) noexcept;

  // Engine-backed dvar gateway.
  //
  // We forward every operation to the iw4x::dvar layer under the appropriate
  // locks, making sure never to duplicate the dvar state. Lookups and
  // iteration will acquire the dvar read lock. This is the concrete
  // dvar_gateway that the console core uses in the live game, as opposed
  // to the test fake.
  //
  class engine_dvar_gateway final : public dvar_gateway
  {
  public:
    // Check if a dvar is registered in the engine.
    //
    bool
    exists (const dvar_name& name) const override;

    // Retrieve the string representation of a dvar's value.
    //
    // We return a nullopt if the dvar doesn't exist.
    //
    std::optional<std::string>
    get (const dvar_name& name) const override;

    // Set the dvar to a new value.
    //
    // Returns the outcome of the operation, which indicates either success
    // or the specific engine-level reason for failure.
    //
    dvar_set_outcome
    set (const dvar_name& name, std::string_view value) override;

    // Enumerate all available dvars.
    //
    // We call the provided callback for each dvar descriptor we find during
    // the traversal.
    //
    void
    enumerate (const std::function<void (const dvar_descriptor&)>& fn) const override;
  };
}
