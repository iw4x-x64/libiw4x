#include <libiw4x/dvar/utility.hxx>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <libiw4x/logger.hxx>

using namespace std;

namespace iw4x::dvar
{
  const char* descriptions[pool_capacity] {};

  void
  set_description (dvar_t* d, const char* text) noexcept
  {
    size_t idx (0);

    // If we cannot resolve the pool index, then we obviously cannot store the
    // description.
    //
    if (!pool_index (d, idx))
      return;

    const char* str (text != nullptr ? text : "");

    // We skip the allocation round-trip if the stored description already
    // matches the one we are trying to set. Notice that this is fairly common
    // during re-registration.
    //
    const char* old (descriptions[idx]);

    if (old != nullptr && strcmp (old, str) == 0)
      return;

    // Allocate a new copy and drop it into the slot.
    //
    const auto len (strlen (str));
    char* copy (new char[len + 1]);
    memcpy (copy, str, len + 1);

    descriptions[idx] = copy;

    if (old != nullptr)
      delete[] old;
  }

  const char*
  get_description (const dvar_t* d) noexcept
  {
    size_t idx (0);

    if (!pool_index (d, idx))
      return "";

    return descriptions[idx] != nullptr ? descriptions[idx] : "";
  }

  // Canonical string selection.
  //
  // The idea here is simple. If the new value is pointer-equal to one of the
  // existing slots (such as current, latched, or reset), we can just hand back
  // that pointer without calling CopyStringInternal (). Note that each variant
  // excludes the slot it is about to overwrite from the reuse candidates.

  namespace
  {
    bool
    try_reuse (const char* value,
               const char* candidate,
               DvarValue& out) noexcept
    {
      // If the candidate pointer happens to be exactly the string we are
      // looking for, then we can just point to it directly.
      //
      if (value != nullptr && value == candidate && candidate != nullptr)
      {
        out.string = candidate;
        return true;
      }
      return false;
    }
  }

  DvarValue
  canonical_string_for_latched (const dvar_t* d, const char* value) noexcept
  {
    DvarValue r {};

    if (try_reuse (value, d->current.string, r))
      return r;
    if (try_reuse (value, d->reset.string, r))
      return r;

    r.string = CopyStringInternal (value != nullptr ? value : "");
    return r;
  }

  DvarValue
  canonical_string_for_current (const dvar_t* d, const char* value) noexcept
  {
    DvarValue r {};

    if (try_reuse (value, d->latched.string, r))
      return r;
    if (try_reuse (value, d->reset.string, r))
      return r;

    r.string = CopyStringInternal (value != nullptr ? value : "");
    return r;
  }

  DvarValue
  canonical_string_for_reset (const dvar_t* d, const char* value) noexcept
  {
    DvarValue r {};

    if (try_reuse (value, d->current.string, r))
      return r;
    if (try_reuse (value, d->latched.string, r))
      return r;

    r.string = CopyStringInternal (value != nullptr ? value : "");
    return r;
  }

  int
  string_to_enum (const DvarLimits& domain, const char* str) noexcept
  {
    if (str == nullptr)
      str = "";

    // Fist, perform an exact case-insensitive match. This is the common case
    // when the user types an enum name directly into the console.
    //
    for (int i (0); i < domain.enumeration.stringCount; ++i)
    {
      if (domain.enumeration.strings[i] != nullptr &&
          I_stricmp (str, domain.enumeration.strings[i]) == 0)
        return i;
    }

    // Then check for a pure decimal index string. Interestingly, the engine
    // allows something like "set foo 3" to mean "set foo to enum index 3". So
    // we have to support that.
    //
    {
      const auto* p (reinterpret_cast<const unsigned char*> (str));
      bool all_digits (true);
      int value (0);

      for (; *p != '\0'; ++p)
      {
        if (*p < '0' || *p > '9')
        {
          all_digits = false;
          break;
        }
        value = value * 10 + (*p - '0');
      }

      if (all_digits && value >= 0 && value < domain.enumeration.stringCount)
        return value;
    }

    // And finally, perform a case-insensitive prefix match. This allows for
    // abbreviations. For example, "set g_gametype dom" matching "domination".
    //
    {
      const int len (static_cast<int> (strlen (str)));

      for (int i (0); i < domain.enumeration.stringCount; ++i)
      {
        if (domain.enumeration.strings[i] != nullptr &&
            I_strnicmp (str, domain.enumeration.strings[i], len) == 0)
          return i;
      }
    }

    return -1337;
  }

  void
  string_to_color (const char* str, DvarValue& out) noexcept
  {
    float c[4] {};

    // Parse the float components from the string. Notice how we leave the array
    // zeroed out if the scan fails.
    //
    if (str != nullptr)
      sscanf (str, "%g %g %g %g", &c[0], &c[1], &c[2], &c[3]);

    // Pack them back into the color byte array.
    //
    out.color[0] = static_cast<char> (color_byte (c[0]));
    out.color[1] = static_cast<char> (color_byte (c[1]));
    out.color[2] = static_cast<char> (color_byte (c[2]));
    out.color[3] = static_cast<char> (color_byte (c[3]));
  }

  DvarValue*
  string_to_value (DvarValue* out,
                   dvarType type,
                   const DvarLimits* domain,
                   const char* str) noexcept
  {
    // Zero out the output value first to avoid leaving any padding or unused
    // fields uninitialized. This is especially important here since DvarValue
    // is a union structure.
    //
    memset (out, 0, sizeof (*out));

    if (str == nullptr)
      str = "";

    switch (type)
    {
      case DVAR_TYPE_BOOL:
        out->enabled = (atoi (str) != 0);
        break;

      case DVAR_TYPE_FLOAT:
        out->value = static_cast<float> (atof (str));
        break;

      case DVAR_TYPE_FLOAT_2:
        sscanf (str, "%g %g", &out->vector.x, &out->vector.y);
        break;

      case DVAR_TYPE_FLOAT_3:
      case DVAR_TYPE_FLOAT_3_COLOR:
        // The engine actually accepts both "x y z" and "( x, y, z )" forms.
        // Note that the latter typically comes from the developer GUI.
        //
        if (*str == '(')
          sscanf (str,
                  "( %g, %g, %g )",
                  &out->vector.x,
                  &out->vector.y,
                  &out->vector.z);
        else
          sscanf (str,
                  "%g %g %g",
                  &out->vector.x,
                  &out->vector.y,
                  &out->vector.z);
        break;

      case DVAR_TYPE_FLOAT_4:
        sscanf (str,
                "%g %g %g %g",
                &out->vector.x,
                &out->vector.y,
                &out->vector.z,
                &out->vector.w);
        break;

      case DVAR_TYPE_INT:
        out->integer = atoi (str);
        break;

      case DVAR_TYPE_INT64:
        out->integer64 = strtoll (str, nullptr, 10);
        break;

      case DVAR_TYPE_UINT64:
        out->unsignedInt64 = strtoull (str, nullptr, 10);
        break;

      case DVAR_TYPE_ENUM:
        out->integer =
          (domain != nullptr) ? string_to_enum (*domain, str) : -1337;
        break;

      case DVAR_TYPE_STRING:
        // This is just a transient result that is used immediately and never
        // stored as-is. So the caller is responsible for the string lifetime.
        //
        out->string = str;
        break;

      case DVAR_TYPE_COLOR:
        string_to_color (str, *out);
        break;

      default:
        break;
    }

    return out;
  }

  void
  get_combined_string (char* out, int start_arg) noexcept
  {
    if (out == nullptr)
      return;

    out[0] = '\0';

    const int argc (cmd_argc ());
    int total (0);

    // Iterate over the arguments starting from the requested index and
    // concatenate them. We must be extremely careful not to exceed the engine's
    // internal buffer limits here.
    //
    for (int i (start_arg); i < argc; ++i)
    {
      const char* arg (cmd_argv (i));
      const auto length (strlen (arg));

      total += static_cast<int> (length) + 1;

      // Stop if we hit the engine command-line budget to avoid overwriting
      // adjacent memory blocks.
      //
      if (total >= 0xFFE)
        break;

      const auto used (strlen (out));
      const auto remaining (static_cast<size_t> (0x1000) - used - 1);

      if (remaining == 0)
        break;

      const auto copy_len (min (length, remaining));
      memcpy (out + used, arg, copy_len);

      if (i != argc - 1 && used + copy_len + 1 < 0x1000)
      {
        out[used + copy_len] = ' ';
        out[used + copy_len + 1] = '\0';
      }
      else
      {
        out[used + copy_len] = '\0';
      }
    }
  }
}
