#include <libiw4x/dvar/command.hxx>
#include <libiw4x/dvar/core.hxx>
#include <libiw4x/dvar/utility.hxx>

#include <tracy/Tracy.hpp>

#include <cstring>

#include <libiw4x/import.hxx>

namespace iw4x::dvar
{
  void
  cmd_toggle () noexcept
  {
    ZoneScoped;

    if (cmd_argc () < 2)
      return;

    const char* name (cmd_argv (1));
    dvar_t* d (find_raw (name));

    if (d == nullptr)
      return;

    // Handle the multi-value cycle.
    //
    if (cmd_argc () > 2)
    {
      const char* current_str (value_to_string (d, d->current));
      const char* next (cmd_argv (2));

      for (int i (2); i < cmd_argc (); ++i)
      {
        const char* candidate (cmd_argv (i));

        // If we are dealing with an enumeration, normalise the candidate to
        // its canonical string. This way, "toggle weapon_class 0 1 2" works
        // exactly the same as passing the actual string names.
        //
        if (d->type == DVAR_TYPE_ENUM)
        {
          const int v (string_to_enum (d->domain, candidate));

          if (v >= 0 && v < d->domain.enumeration.stringCount
                     && d->domain.enumeration.strings[v] != nullptr)
          {
            candidate = d->domain.enumeration.strings[v];
          }
        }

        // If we found our current value, pick the next one in the list.
        // Wrap around to the beginning if we are at the last argument.
        //
        if (I_stricmp (current_str, candidate) == 0)
        {
          next = (i + 1 < cmd_argc ()) ? cmd_argv (i + 1) : cmd_argv (2);
          break;
        }
      }

      set_command (name, next);
      return;
    }

    // Handle the binary or natural cycle toggle.
    //
    DvarValue v {};

    switch (d->type)
    {
      case DVAR_TYPE_BOOL:
        v.enabled = !d->current.enabled;
        break;

      case DVAR_TYPE_ENUM:
        if (d->domain.enumeration.stringCount == 0)
          return;

        v.integer = (d->current.integer + 1) %
                     d->domain.enumeration.stringCount;
        break;

      case DVAR_TYPE_INT:
        // Prefer toggling between 0 and 1 if the domain allows it, as this
        // is the most common use case for integer flags. Otherwise, bounce
        // between the extremes.
        //
        if (d->domain.integer.min <= 0 && d->domain.integer.max >= 1)
          v.integer = (d->current.integer == 0) ? 1 : 0;
        else
          v.integer = (d->current.integer == d->domain.integer.min)
            ? d->domain.integer.max
            : d->domain.integer.min;
        break;

      case DVAR_TYPE_INT64:
        v.integer64 = (d->current.integer64 == d->domain.integer64.min)
          ? d->domain.integer64.max
          : d->domain.integer64.min;
        break;

      case DVAR_TYPE_UINT64:
        v.unsignedInt64 = (d->current.unsignedInt64 == d->domain.unsignedInt64.min)
          ? d->domain.unsignedInt64.max
          : d->domain.unsignedInt64.min;
        break;

      case DVAR_TYPE_FLOAT:
        // Similar to integers, prefer the 0.0 to 1.0 toggle if possible.
        //
        if (d->domain.value.min <= 0.0f && d->domain.value.max >= 1.0f)
          v.value = (d->current.value == 0.0f) ? 1.0f : 0.0f;
        else
          v.value = (d->current.value == d->domain.value.min)
            ? d->domain.value.max
            : d->domain.value.min;
        break;

      default:
        // Strings, colors, and vectors do not have a natural toggle state.
        //
        return;
    }

    set_variant (d, v, DVAR_SOURCE_EXTERNAL);
  }

  void
  cmd_set () noexcept
  {
    ZoneScoped;

    if (cmd_argc () < 3)
      return;

    const char* name (cmd_argv (1));

    if (!Dvar_IsValidName (name))
      return;

    // Combine the remaining arguments into a single string representation.
    //
    char combined[0x1000] {};
    get_combined_string (combined, 2);

    set_command (name, combined);
  }

  void
  cmd_seta () noexcept
  {
    ZoneScoped;

    if (cmd_argc () < 3)
      return;

    const char* name (cmd_argv (1));

    if (!Dvar_IsValidName (name))
      return;

    // Combine the remaining arguments.
    //
    char combined[0x1000] {};
    get_combined_string (combined, 2);

    set_command (name, combined);

    // Try to find the dvar we just updated and mark it for archiving.
    //
    dvar_t* d (find_raw (name));

    if (d != nullptr)
      add_flags (d, DVAR_ARCHIVE);
  }

  void
  cmd_reset () noexcept
  {
    ZoneScoped;

    if (cmd_argc () != 2)
      return;

    const char* name (cmd_argv (1));
    dvar_t* d (find_raw (name));

    if (d != nullptr)
      reset (d, DVAR_SOURCE_EXTERNAL);
  }

  void
  cmd_setfromdvar () noexcept
  {
    ZoneScoped;

    if (cmd_argc () != 3)
      return;

    const char* src_name (cmd_argv (2));
    dvar_t* src (find_raw (src_name));

    if (src == nullptr)
      return;

    const char* dst_name (cmd_argv (1));
    const char* val (value_to_string (src, src->current));

    set_command (dst_name, val);
  }

  int
  cmd_dispatch () noexcept
  {
    ZoneScoped;

    const char* name (cmd_argv (0));
    dvar_t* d (find_raw (name));

    if (d == nullptr)
      return 0;

    // If there are arguments following the dvar name, combine them and
    // update the dvar. Otherwise, we just acknowledge the token.
    //
    if (cmd_argc () > 1)
    {
      char combined[0x1000] {};

      get_combined_string (combined, 1);
      set_command (name, combined);
    }

    return 1;
  }
}
