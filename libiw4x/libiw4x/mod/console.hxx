#pragma once

#include <libiw4x/console/core.hxx>

namespace iw4x::mod
{
  // The console module: the live-engine integration boundary.
  //
  // Constructing it boots the console subsystem and wires it to the engine: it
  // installs the engine-backed dvar gateway, the engine command-buffer fallback,
  // and (in the UI phase) the input and render detours. It mirrors dvar_module:
  // the pure console/ library knows nothing of the engine, and this object is
  // where the two are joined. It is created once, from iw4x.cxx, on the main
  // thread after the engine is far enough along to accept registration.
  //
  class console_module
  {
  public:
    console_module ();
  };

  // The live console coordinator.
  //
  // Engine-facing code (input detours, command thunks) drives the console
  // through this single instance. Constructed on first use, wired by
  // console_module.
  //
  [[nodiscard]] iw4x::console::console&
  console_instance ();
}
