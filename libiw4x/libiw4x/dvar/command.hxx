#pragma once

namespace iw4x::dvar
{
  // Handle the toggle command. We operate in two distinct modes depending on
  // the number of arguments provided.
  //
  // If we have a multi-value toggle (argc > 2), we cycle through the listed
  // arguments. We compare the current string representation against each
  // candidate and advance to the next. For enumerations, we normalise the
  // candidate to its canonical string before comparison. This ensures that
  // "set g_gametype 2" and "set g_gametype dom" both match correctly.
  //
  // If we have a binary toggle (argc == 2), we simply toggle the dvar over
  // its natural domain. Booleans are flipped. Integers and floats toggle
  // between 0 and 1 if their domain contains both, otherwise we bounce
  // between their minimum and maximum values. Enumerations cycle to the
  // next element. Note that strings, colors, and vectors are not togglable.
  //
  void
  cmd_toggle () noexcept;

  // Assign a new value to a dvar. The value might span multiple arguments
  // (for example, vectors), so we combine everything after the dvar name.
  //
  void
  cmd_set () noexcept;

  // Identical to cmd_set, except we also set the DVAR_ARCHIVE flag. This
  // ensures the new value persists across engine sessions.
  //
  void
  cmd_seta () noexcept;

  // Revert a dvar back to its default value.
  //
  void
  cmd_reset () noexcept;

  // Copy the current value from one dvar to another.
  //
  void
  cmd_setfromdvar () noexcept;

  // Intercept unrecognised tokens from the engine's command processor. If
  // the token happens to match an existing dvar name, we treat the sequence
  // as an implicit "set" command.
  //
  int
  cmd_dispatch () noexcept;
}
