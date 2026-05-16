#pragma once

namespace iw4x::dvar
{
  // Initialize the dvar subsystem internal state.
  //
  // This must be called before any other registration or resolution passes so
  // that our internal tracking flags and structures are primed.
  //
  void
  init_subsystem () noexcept;

  // Register dvars owned by our modification.
  //
  // We iterate through our static declaration tables and commit each variant to
  // the engine's internal dvar pool. Note that if the engine fails to allocate
  // or register the dvar, we will immediately bail out.
  //
  void
  register_owned () noexcept;

  // Resolve and cache external engine references.
  //
  // Look up the base engine dvars that we depend on and cache their handles
  // locally. If a required engine dvar is missing or has a type mismatch, we
  // will trigger a fatal error. We only do this after our own variables are
  // registered.
  //
  void
  resolve_engine_refs () noexcept;

  // Apply description patches to existing engine dvars.
  //
  // Finally, we apply any custom description overrides from our patch tables to
  // the resolved engine dvars. We do this last as we need base engine
  // references to be fully resolved and accessible first.
  //
  void
  apply_descriptions () noexcept;
}
