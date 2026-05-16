#include <libiw4x/dvar/core.hxx>

#include <cassert>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

#ifdef TRACY_ENABLE
#  include <tracy/Tracy.hpp>
#endif

#include <libiw4x/import.hxx>
#include <libiw4x/logger.hxx>

#include <libiw4x/dvar/abi.hxx>
#include <libiw4x/dvar/utility.hxx>

using namespace std;

namespace iw4x::dvar
{
  dvar_t*
  find_raw (const char* name) noexcept
  {
    ZoneScoped;

    if (name == nullptr || *name == '\0')
      return nullptr;

    const uint32_t bucket (hash_name (name) & (hash_buckets - 1));

    DVAR_PREFETCH (dvarHashTable[bucket]);

    read_lock lock;

    for (dvar_t* d (dvarHashTable[bucket]); d != nullptr; d = d->hashNext)
    {
      if (d->hashNext != nullptr)
        DVAR_PREFETCH (d->hashNext);

      if (I_stricmp (name, d->name) == 0)
        return d;
    }

    return nullptr;
  }

  void
  for_each (for_each_fn fn, void* user_data) noexcept
  {
    ZoneScoped;

    if (fn == nullptr)
      return;

    read_lock lock;

    if (!*areDvarsSorted)
      Dvar_Sort ();

    const int count (*dvarCount);
    TracyPlot("dvarCount", static_cast<int64_t>(count));

    for (int i (0); i < count; ++i)
      fn (sortedDvars[i], user_data);
  }

  dvar_t*
  register_variant (const char* name,
                    dvarType    type,
                    DvarFlags   flags,
                    DvarValue   value,
                    DvarLimits  domain,
                    const char* description) noexcept
  {
    ZoneScoped;
    ZoneText (name, name != nullptr ? strlen (name) : 0);

    write_lock lock;

    const uint32_t bucket (hash_name (name) & (hash_buckets - 1));

    // Paths 1 & 2: Check if the dvar already exists in this bucket.
    //
    for (dvar_t* existing (dvarHashTable[bucket]);
         existing != nullptr;
         existing = existing->hashNext)
    {
      if (I_stricmp (name, existing->name) != 0)
        continue;

      set_description (existing, description);

      // Simple re-registration.
      //
      // Nothing really changed except perhaps the description, so we just
      // update it and return the existing instance.
      //
      if (!flag_set (existing->flags, DVAR_EXTERNAL))
        return existing;

      // EXTERNAL promotion.
      //
      // The config parser created a placeholder string dvar before we had a
      // chance to declare it properly. We now convert it to the declared
      // native type, swapping out the allocations.
      //
      const char* saved_str (nullptr);

      if (existing->type == DVAR_TYPE_STRING &&
          existing->current.string != nullptr)
        saved_str = CopyStringInternal (existing->current.string);

      // Free the existing string slots.
      //
      // We have to be careful here to guard against aliased pointers, since
      // current, latched, and reset can easily point to the same allocation.
      //
      if (existing->type == DVAR_TYPE_STRING)
      {
        const char* c (existing->current.string);
        const char* l (existing->latched.string);
        const char* r (existing->reset.string);

        if (c != nullptr)                     FreeStringInternal (c);
        if (l != nullptr && l != c)           FreeStringInternal (l);
        if (r != nullptr && r != c && r != l) FreeStringInternal (r);
      }

      existing->type = type;
      existing->flags = static_cast<DvarFlags> (
        (static_cast<int> (existing->flags) &
         ~static_cast<int> (DVAR_EXTERNAL)) |
        static_cast<int> (flags));
      existing->domain   = domain;
      existing->modified = false;

      // Set up the reset slot using the declared default value.
      //
      if (type == DVAR_TYPE_STRING)
      {
        DvarValue rv {};
        rv.string = CopyStringInternal (
          value.string != nullptr ? value.string : "");
        existing->reset = rv;
      }
      else
      {
        copy_value (existing->reset, value, type);
      }

      // Parse the saved config string into our target native type.
      //
      // If the parsed result falls outside the declared domain bounds, we
      // just fall back to the declared default we just established.
      //
      DvarValue parsed {};

      if (saved_str != nullptr)
      {
        string_to_value (&parsed, type, &domain, saved_str);

        if (!value_in_domain (type, parsed, domain))
          copy_value (parsed, existing->reset, type);
      }
      else
      {
        copy_value (parsed, existing->reset, type);
      }

      if (type == DVAR_TYPE_STRING)
      {
        DvarValue sv {};
        sv.string = CopyStringInternal (
          parsed.string != nullptr ? parsed.string : "");
        existing->current = sv;
        existing->latched = sv;
      }
      else
      {
        copy_value (existing->current, parsed, type);
        copy_value (existing->latched, parsed, type);
      }

      if (saved_str != nullptr)
        FreeStringInternal (saved_str);

      log::debug ("dvar: promoted external '{}' type={}",
                  name, static_cast<int> (type));

      return existing;
    }

    // Path 3: Fresh allocation.
    //
    // We didn't find an existing dvar, so we need to allocate a new slot
    // from the fixed-size pool.
    //
    if (*dvarCount >= static_cast<int> (pool_capacity))
      Com_Error (ERR_FATAL,
                 "Can't create dvar '%s': pool full (%zu slots used)",
                 name != nullptr ? name : "<null>",
                 pool_capacity);

    const int index (*dvarCount);
    dvar_t*   d     (&dvarPool[index]);

    (*dvarCount)++;
    sortedDvars[index] = d;
    *areDvarsSorted    = false;

    // Allocate the name string.
    //
    // EXTERNAL dvars usually come from the config-parser heap and need
    // copying into the engine string pool. All other names are typically
    // string literals with static lifetime, so we can just use the pointer.
    //
    d->name = flag_set (flags, DVAR_EXTERNAL)
      ? CopyStringInternal (name != nullptr ? name : "")
      : name;

    d->flags      = flags;
    d->type       = type;
    d->modified   = false;
    d->domain     = domain;
    d->domainFunc = nullptr;

    if (type == DVAR_TYPE_STRING)
    {
      DvarValue sv {};
      sv.string = CopyStringInternal (
        value.string != nullptr ? value.string : "");
      d->current = sv;
      d->latched = sv;
      d->reset   = sv;
    }
    else
    {
      copy_value (d->current, value, type);
      copy_value (d->latched, value, type);
      copy_value (d->reset,   value, type);
    }

    // Link the new dvar into the hash table at the head of the bucket.
    //
    {
      const uint32_t b (hash_name (d->name) & (hash_buckets - 1));

      d->hashNext      = dvarHashTable[b];
      dvarHashTable[b] = d;
    }

    set_description (d, description);

    log::debug ("dvar: registered '{}' type={} flags={}",
                d->name,
                static_cast<int> (type),
                static_cast<int> (flags));

    return d;
  }

  dvar_t*
  register_int64 (const char*  name,
                  int64_t      value,
                  int64_t      min,
                  int64_t      max,
                  DvarFlags    flags,
                  const char*  description) noexcept
  {
    mark_registration_flags (flags);

    DvarValue  v {};
    DvarLimits d {};
    v.integer64     = value;
    d.integer64.min = min;
    d.integer64.max = max;

    return register_variant (name, DVAR_TYPE_INT64, flags, v, d, description);
  }

  dvar_t*
  register_uint64 (const char*   name,
                   uint64_t value,
                   uint64_t min,
                   uint64_t max,
                   DvarFlags     flags,
                   const char*   description) noexcept
  {
    mark_registration_flags (flags);

    DvarValue  v {};
    DvarLimits d {};
    v.unsignedInt64     = value;
    d.unsignedInt64.min = min;
    d.unsignedInt64.max = max;

    return register_variant (name, DVAR_TYPE_UINT64, flags, v, d, description);
  }

  void
  set_latched (dvar_t* d, const DvarValue& value) noexcept
  {
    ZoneScoped;

    if (d == nullptr)
      return;

    if (d->type != DVAR_TYPE_STRING)
    {
      copy_value (d->latched, value, d->type);
      return;
    }

    const char* old (d->latched.string);

    if (old == value.string)
      return; // already canonical

    const bool free_old (old != nullptr &&
                         old != d->current.string &&
                         old != d->reset.string);

    d->latched = canonical_string_for_latched (d, value.string);

    if (free_old)
      FreeStringInternal (old);
  }

  namespace
  {
    DVAR_NOINLINE void
    assign_current (dvar_t* d, const DvarValue& value) noexcept
    {
      switch (d->type)
      {
        case DVAR_TYPE_FLOAT_2:
        case DVAR_TYPE_FLOAT_3:
        case DVAR_TYPE_FLOAT_3_COLOR:
        case DVAR_TYPE_FLOAT_4:
          copy_value (d->current, value, d->type);
          copy_value (d->latched, value, d->type);
          break;

        case DVAR_TYPE_STRING:
        {
          const char* old_cur (d->current.string);
          const bool free_cur (old_cur != nullptr &&
                               old_cur != d->latched.string &&
                               old_cur != d->reset.string);

          DvarValue new_cur (canonical_string_for_current (d, value.string));

          const char* old_lat (d->latched.string);
          d->current = new_cur;

          if (old_lat != nullptr &&
              old_lat != d->current.string &&
              old_lat != d->reset.string)
            FreeStringInternal (old_lat);

          d->latched = new_cur;

          if (free_cur)
            FreeStringInternal (old_cur);

          break;
        }

        default:
          copy_value (d->current, value, d->type);
          copy_value (d->latched, value, d->type);
          break;
      }

      d->modified = true;
    }
  }

  void
  set_variant (dvar_t* d, const DvarValue& value, DvarSetSource source) noexcept
  {
    ZoneScoped;

    if (d == nullptr)
      return;

    // The engine enforces certain invariants around mutation at this address.
    //
    // Its exact purpose is a bit unclear from static analysis alone, but we
    // know we must always call it first before touching the values.
    //
    reinterpret_cast<void (*) ()> (k_pre_set_variant) ();

    DvarValue v (value);

    // Domain check.
    //
    // Out-of-domain values are silently rejected for most types. ENUMs are
    // special, however; they fall back to the reset value instead.
    //
    if (!value_in_domain (d->type, v, d->domain))
    {
      if (d->type != DVAR_TYPE_ENUM)
        return;

      v = d->reset;
    }

    // Domain function callback.
    //
    // If one is installed, it has the right to veto or transform the value
    // before we commit it. We check if the page is executable to avoid
    // crashing on some older dangling pointers.
    //
    if (d->domainFunc != nullptr)
    {
      DvarValue cb_val (v);

      if (!d->domainFunc (d, &cb_val))
        return;

      v = cb_val;
    }

    // Source-based security guards.
    //
    // These checks only apply when the mutation comes from EXTERNAL or SCRIPT
    // sources (e.g. the console). We protect ROM, INIT, and cheat variables
    // here.
    //
    if (source == DVAR_SOURCE_EXTERNAL || source == DVAR_SOURCE_SCRIPT)
    {
      const int f (static_cast<int> (d->flags));

      if ((f & (static_cast<int> (DVAR_INIT) |
                static_cast<int> (DVAR_ROM))) != 0)
        return;

      if (source == DVAR_SOURCE_EXTERNAL &&
          (f & static_cast<int> (DVAR_CHEAT)) != 0 &&
          *reinterpret_cast<const unsigned char*> (k_cheats_flag) == 0)
        return;

      if ((f & static_cast<int> (DVAR_LATCH)) != 0)
      {
        set_latched (d, v);
        return;
      }
    }

    mark_modified_flags (d);

    // No-change optimisation.
    //
    // If the new value is identical to the current one, we just sync the
    // latched state and avoid a full assignment to save time.
    //
    if (values_equal (d->type, d->current, v))
    {
      set_latched (d, d->current);
      return;
    }

    assign_current (d, v);
  }

  void
  set_bool (dvar_t* d, bool v, DvarSetSource s) noexcept
  {
    if (d == nullptr) return;
    DvarValue dv {};
    dv.enabled = v;
    set_variant (d, dv, s);
  }

  void
  set_int (dvar_t* d, int v, DvarSetSource s) noexcept
  {
    if (d == nullptr) return;
    DvarValue dv {};

    switch (d->type)
    {
      case DVAR_TYPE_INT:
      case DVAR_TYPE_ENUM:
        dv.integer = v;
        break;
      case DVAR_TYPE_INT64:
        dv.integer64 = v;
        break;
      case DVAR_TYPE_UINT64:
        dv.unsignedInt64 = (v < 0) ? 0 : static_cast<uint64_t> (v);
        break;
      default:
      {
        char buf[32];
        snprintf (buf, sizeof (buf), "%i", v);
        dv.string = buf;
        break;
      }
    }

    set_variant (d, dv, s);
  }

  void
  set_float (dvar_t* d, float v, DvarSetSource s) noexcept
  {
    if (d == nullptr) return;
    DvarValue dv {};

    if (d->type == DVAR_TYPE_FLOAT)
      dv.value = v;
    else
    {
      char buf[64];
      snprintf (buf, sizeof (buf), "%g", static_cast<double> (v));
      dv.string = buf;
    }

    set_variant (d, dv, s);
  }

  void
  set_string (dvar_t* d, const char* v, DvarSetSource s) noexcept
  {
    if (d == nullptr) return;

    DvarValue dv {};

    if (d->type == DVAR_TYPE_STRING)
    {
      dv.string = (v != nullptr) ? v : "";
    }
    else if (d->type == DVAR_TYPE_ENUM)
    {
      dv.integer = string_to_enum (d->domain, v);
    }
    else
    {
      string_to_value (&dv, d->type, &d->domain, v);
    }

    set_variant (d, dv, s);
  }

  void
  set_int64 (dvar_t* d, int64_t v, DvarSetSource s) noexcept
  {
    if (d == nullptr) return;
    DvarValue dv {};
    dv.integer64 = v;
    set_variant (d, dv, s);
  }

  void
  set_uint64 (dvar_t* d, uint64_t v, DvarSetSource s) noexcept
  {
    if (d == nullptr) return;
    DvarValue dv {};
    dv.unsignedInt64 = v;
    set_variant (d, dv, s);
  }

  void
  update_reset_value (dvar_t* d, const DvarValue& value) noexcept
  {
    if (d == nullptr)
      return;

    if (d->type != DVAR_TYPE_STRING)
    {
      copy_value (d->reset, value, d->type);
      return;
    }

    const char* old (d->reset.string);
    d->reset = canonical_string_for_reset (d, value.string);

    if (old != nullptr &&
        old != d->current.string &&
        old != d->latched.string &&
        old != d->reset.string)
      FreeStringInternal (old);
  }

  void
  reset (dvar_t* d, DvarSetSource source) noexcept
  {
    if (d != nullptr)
      set_variant (d, d->reset, source);
  }

  void
  reset_script_info () noexcept
  {
    write_lock lock;

    for (int i (0); i < *dvarCount; ++i)
    {
      dvarPool[i].flags =
        static_cast<DvarFlags> (static_cast<int> (dvarPool[i].flags) &
                                ~static_cast<int> (DVAR_SCRIPTINFO));
    }
  }

  void
  add_flags (dvar_t* d, DvarFlags flags) noexcept
  {
    if (d == nullptr)
      return;

    mark_registration_flags (flags);

    d->flags = static_cast<DvarFlags> (
      static_cast<int> (d->flags) | static_cast<int> (flags));
  }

  int64_t
  get_int64 (const dvar_t* d) noexcept
  {
    if (d == nullptr)
      return 0;

    switch (d->type)
    {
      case DVAR_TYPE_BOOL:
        return d->current.enabled ? 1 : 0;

      case DVAR_TYPE_FLOAT:
        return static_cast<int64_t> (d->current.value);

      case DVAR_TYPE_INT:
      case DVAR_TYPE_ENUM:
        return d->current.integer;

      case DVAR_TYPE_INT64:
        return d->current.integer64;

      case DVAR_TYPE_UINT64:
      {
        const auto u (d->current.unsignedInt64);

        return (u > static_cast<uint64_t> (
                      numeric_limits<int64_t>::max ()))
          ? numeric_limits<int64_t>::max ()
          : static_cast<int64_t> (u);
      }

      case DVAR_TYPE_STRING:
      {
        return strtoll (d->current.string != nullptr ? d->current.string
                                                          : "",
                             nullptr,
                             10);
      }

      default:
        return 0;
    }
  }

  uint64_t
  get_uint64 (const dvar_t* d) noexcept
  {
    if (d == nullptr)
      return 0;

    switch (d->type)
    {
      case DVAR_TYPE_BOOL:
        return d->current.enabled ? 1ULL : 0ULL;

      case DVAR_TYPE_FLOAT:
        return (d->current.value < 0.0f)
          ? 0ULL
          : static_cast<uint64_t> (d->current.value);

      case DVAR_TYPE_INT:
      case DVAR_TYPE_ENUM:
        return (d->current.integer < 0)
          ? 0ULL
          : static_cast<uint64_t> (d->current.integer);

      case DVAR_TYPE_INT64:
        return (d->current.integer64 < 0)
          ? 0ULL
          : static_cast<uint64_t> (d->current.integer64);

      case DVAR_TYPE_UINT64:
        return d->current.unsignedInt64;

      case DVAR_TYPE_STRING:
        return strtoull (
          d->current.string != nullptr ? d->current.string : "",
          nullptr, 10);

      default:
        return 0;
    }
  }

  // XXX Should we be returning std::string here instead of const char*?
  //
  // Currently, returning a raw pointer keeps this fast and noexcept since
  // we are assuming the string payload is already materialized and owned by
  // either `d` or `value`. However, if we ever need to synthesize a string
  // representation on the fly (for instance, if DvarValue holds an integer
  // or a boolean that we need to format), we won't have a stable memory
  // location to return a pointer to.
  //
  // Sticking with const char* for now avoids the allocation overhead for the
  // common case, but if the underlying types we need to stringify expand,
  // this signature will box us into a corner and will need to change.
  //
  const char*
  value_to_string (const dvar_t*    d,
                   const DvarValue& value) noexcept
  {
    if (d == nullptr)
      return "";

    char* const buf (next_string_buffer ());

    switch (d->type)
    {
      case DVAR_TYPE_BOOL:
        return value.enabled ? "1" : "0";

      case DVAR_TYPE_FLOAT:
        snprintf (buf, 256, "%g", static_cast<double> (value.value));
        return buf;

      case DVAR_TYPE_FLOAT_2:
        snprintf (buf, 256, "%g %g",
                       static_cast<double> (value.vector.x),
                       static_cast<double> (value.vector.y));
        return buf;

      case DVAR_TYPE_FLOAT_3:
      case DVAR_TYPE_FLOAT_3_COLOR:
        snprintf (buf, 256, "%g %g %g",
                       static_cast<double> (value.vector.x),
                       static_cast<double> (value.vector.y),
                       static_cast<double> (value.vector.z));
        return buf;

      case DVAR_TYPE_FLOAT_4:
        snprintf (buf, 256, "%g %g %g %g",
                       static_cast<double> (value.vector.x),
                       static_cast<double> (value.vector.y),
                       static_cast<double> (value.vector.z),
                       static_cast<double> (value.vector.w));
        return buf;

      case DVAR_TYPE_INT:
        snprintf (buf, 256, "%i", value.integer);
        return buf;

      case DVAR_TYPE_INT64:
        snprintf (buf, 256, "%" PRId64, value.integer64);
        return buf;

      case DVAR_TYPE_UINT64:
        snprintf (buf, 256, "%" PRIu64, value.unsignedInt64);
        return buf;

      case DVAR_TYPE_ENUM:
        if (value.integer >= 0 &&
            value.integer < d->domain.enumeration.stringCount &&
            d->domain.enumeration.strings[value.integer] != nullptr)
          return d->domain.enumeration.strings[value.integer];
        return "";

      case DVAR_TYPE_STRING:
        return (value.string != nullptr) ? value.string : "";

      case DVAR_TYPE_COLOR:
        snprintf (
          buf, 256, "%g %g %g %g",
          static_cast<double> (
            static_cast<unsigned char> (value.color[0]) * (1.0f / 255.0f)),
          static_cast<double> (
            static_cast<unsigned char> (value.color[1]) * (1.0f / 255.0f)),
          static_cast<double> (
            static_cast<unsigned char> (value.color[2]) * (1.0f / 255.0f)),
          static_cast<double> (
            static_cast<unsigned char> (value.color[3]) * (1.0f / 255.0f)));
        return buf;

      default:
        return "";
    }
  }

  void
  set_command (const char* name, const char* value) noexcept
  {
    dvar_t* d (find_raw (name));

    if (d == nullptr)
      d = Dvar_RegisterString (name,
                               (value != nullptr) ? value : "",
                               DVAR_EXTERNAL,
                               "External Dvar");
    else
      set_string (d, value, DVAR_SOURCE_EXTERNAL);

    if (d == nullptr)
      return;

    if (*reinterpret_cast<const unsigned char*> (k_autoexec_flag) != 0)
    {
      add_flags (d, DVAR_AUTOEXEC);
      update_reset_value (d, d->current);
    }
  }

  dvar_t*
  set_from_string_by_name (const char* name, const char* value) noexcept
  {
    dvar_t* d (find_raw (name));

    if (d == nullptr)
      return Dvar_RegisterString (name,
                                  (value != nullptr) ? value : "",
                                  DVAR_EXTERNAL,
                                  "External Dvar");

    set_string (d, value, DVAR_SOURCE_INTERNAL);
    return d;
  }
}
