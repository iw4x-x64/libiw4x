#include <libiw4x/dvar/declaration.hxx>

#include <cassert>
#include <cinttypes>
#include <cstring>
#include <type_traits>

#include <tracy/Tracy.hpp>

#include <libiw4x/import.hxx>
#include <libiw4x/logger.hxx>

#include <libiw4x/dvar/abi.hxx>
#include <libiw4x/dvar/core.hxx>
#include <libiw4x/dvar/utility.hxx>
#include <libiw4x/dvar/types.hxx>

using namespace std;

namespace iw4x::dvar
{
  // Declaration tables.
  //
  // Note that all three tables are constexpr arrays. We compute their sizes via
  // extent_v so we do not have to maintain a manual element count
  // constant, which would inevitably fall out of sync over time.
  //
  // IW4x-owned dvars.
  //
  // Here we define the dvars that are owned by our modification. To introduce a
  // new dvar, we simply add a row to this table, declare a corresponding static
  // handle below, and wire up the assignment in register_owned().
  //
  constexpr declaration declarations[0]
  {
    // Start with an empty placeholder. Once you need to add rows, you should
    // place them right here.
    //
    // For example, a boolean dvar definition might look like this:
    //
    // {
    //   "iw4x_cl_motd_enabled",
    //   DVAR_TYPE_BOOL,
    //   { .enabled = true },
    //   {},
    //   DVAR_ARCHIVE,
    //   "Show the IW4x message of the day on connect."
    // }
    //
  };

  // Parallel static handles.
  //
  // We populate these handles during the registration phase in
  // register_owned(). Note that they are populated in exact table order. For
  // instance, if you added the MOTD dvar above, you would declare its handle
  // here.
  //

  // Engine dvar references.
  //
  // We use this table to resolve and cache handles to dvars that are owned by
  // the base engine. Note that we keep this empty for now. Add rows as they
  // become necessary.
  //
  // For instance, an entry might look like this:
  //
  // {
  //   "sv_cheats",
  //   DVAR_TYPE_BOOL,
  //   false, // This boolean marks the reference as required.
  //   &sv_cheats_h,
  //   nullptr // Leave as nullptr if you do not wish to patch the description.
  // }
  //
  constexpr engine_ref engine_refs[0]
  {
  };

  // Description patches.
  //
  // We use this table to overwrite or provide missing descriptions for existing
  // engine dvars. We start with an empty placeholder.
  //
  // For example:
  //
  // {
  //   "sv_maxclients", "Maximum number of connected clients."
  // }
  //
  constexpr description_patch desc_patches[0]
  {
  };

  // Case-insensitive hash function object.
  //
  // We use this to hash dvar names for our validation map. Note that we
  // manually lowercase each character because the engine treats dvar names
  // case-insensitively, and we need our duplicate detection to reflect that
  // behavior.
  //
  struct icase_hash
  {
    size_t
    operator () (const char* s) const noexcept
    {
      size_t h (0);
      for (; *s != '\0'; ++s)
        h = h * 31 + tolower (static_cast<unsigned char> (*s));
      return h;
    }
  };

  // Case-insensitive equality function object.
  //
  // Similar to the hash function, we need to handle hash collisions while
  // adhering to the case-insensitive semantics of the engine.
  //
  struct icase_eq
  {
    bool
    operator () (const char* lhs, const char* rhs) const noexcept
    {
      return I_stricmp (lhs, rhs) == 0;
    }
  };

  // Subsystem lifecycle state guards.
  //
  static bool s_initialized (false);
  static bool s_registered (false);
  static bool s_resolved (false);

  // Validate static declarations.
  //
  // We thoroughly examine every entry in the declarations array prior to
  // initiating any registration calls.
  //
  DVAR_COLD static void
  validate_declarations ()
  {
    ZoneScoped;

    constexpr auto n (extent_v<decltype (declarations)>);

    unordered_map<
      const char*, size_t, icase_hash, icase_eq
    > seen_names;

    seen_names.reserve (n);

    for (size_t i (0); i < n; ++i)
    {
      const auto& d (declarations[i]);

      if (d.name == nullptr || d.name[0] == '\0')
        Com_Error (ERR_FATAL,
                   "dvar: declaration[%zu] has null or empty name", i);

      // Note that passing an empty string is perfectly valid. However, a null
      // pointer will eventually cause access violations down the line when the
      // engine attempts to read the description.
      //
      if (d.description == nullptr)
        Com_Error (ERR_FATAL,
                   "dvar: declaration '%s' has null description", d.name);

      // Note that the DVAR_EXTERNAL flag is reserved for the config parser. If
      // we see it declared in our static table, it indicates that the developer
      // confused the internal and external initialization paths.
      //
      if (flag_set (d.flags, DVAR_EXTERNAL))
        Com_Error (ERR_FATAL,
                   "dvar: declaration '%s' sets DVAR_EXTERNAL", d.name);

      // Check for name uniqueness across the entire table.
      //
      if (d.name != nullptr)
      {
        auto [it, inserted] (seen_names.emplace (d.name, i));

        if (!inserted)
          Com_Error (ERR_FATAL,
                     "dvar: duplicate declaration name '%s' at %zu and %zu",
                     d.name, it->second, i);
      }

      // Validate type-specific domain constraints.
      //
      // Default values must not violate their own bounds, as the engine
      // implicitly trusts our initialization parameters.
      //
      switch (d.type)
      {
        case DVAR_TYPE_ENUM:
        {
          // Specifically for enumerations, we require a valid list of strings
          // and a strict bounds check on the default index to ensure it falls
          // within the valid range.
          //
          if (d.domain.enumeration.stringCount <= 0)
            Com_Error (ERR_FATAL,
                       "dvar: '%s' (ENUM) stringCount <= 0", d.name);

          if (d.domain.enumeration.strings == nullptr)
            Com_Error (ERR_FATAL,
                       "dvar: '%s' (ENUM) strings is null", d.name);

          if (d.default_value.integer < 0 ||
              d.default_value.integer >= d.domain.enumeration.stringCount)
            Com_Error (ERR_FATAL,
                       "dvar: '%s' (ENUM) default index %d outside [0, %d)",
                       d.name, d.default_value.integer,
                       d.domain.enumeration.stringCount);
          break;
        }

        case DVAR_TYPE_STRING:
        {
          // For string dvars, we verify the default pointer refers to valid
          // memory.
          //
          if (d.default_value.string == nullptr)
            Com_Error (ERR_FATAL,
                       "dvar: '%s' (STRING) default is null", d.name);
          break;
        }

        case DVAR_TYPE_INT:
        {
          // For integers, we first verify that the domain itself is logically
          // sound before confirming the default value resides within it.
          //
          if (d.domain.integer.min > d.domain.integer.max)
            Com_Error (ERR_FATAL,
                       "dvar: '%s' (INT) domain min %d > max %d",
                       d.name, d.domain.integer.min, d.domain.integer.max);

          if (!value_in_domain (d.type, d.default_value, d.domain))
            Com_Error (ERR_FATAL,
                       "dvar: '%s' (INT) default %d outside [%d, %d]",
                       d.name, d.default_value.integer,
                       d.domain.integer.min, d.domain.integer.max);
          break;
        }

        case DVAR_TYPE_FLOAT:
        {
          // We perform the exact same logical checks for floating-point values
          // as we do for integers.
          //
          if (d.domain.value.min > d.domain.value.max)
            Com_Error (ERR_FATAL,
                       "dvar: '%s' (FLOAT) domain min %g > max %g",
                       d.name,
                       static_cast<double> (d.domain.value.min),
                       static_cast<double> (d.domain.value.max));

          if (!value_in_domain (d.type, d.default_value, d.domain))
            Com_Error (ERR_FATAL,
                       "dvar: '%s' (FLOAT) default %g outside [%g, %g]",
                       d.name,
                       static_cast<double> (d.default_value.value),
                       static_cast<double> (d.domain.value.min),
                       static_cast<double> (d.domain.value.max));
          break;
        }

        case DVAR_TYPE_INT64:
        {
          if (!value_in_domain (d.type, d.default_value, d.domain))
            Com_Error (ERR_FATAL,
                       "dvar: '%s' (INT64) default %" PRId64
                       " outside [%" PRId64 ", %" PRId64 "]",
                       d.name, d.default_value.integer64,
                       d.domain.integer64.min, d.domain.integer64.max);
          break;
        }

        case DVAR_TYPE_UINT64:
        {
          if (!value_in_domain (d.type, d.default_value, d.domain))
            Com_Error (ERR_FATAL,
                       "dvar: '%s' (UINT64) default %" PRIu64
                       " outside [%" PRIu64 ", %" PRIu64 "]",
                       d.name, d.default_value.unsignedInt64,
                       d.domain.unsignedInt64.min,
                       d.domain.unsignedInt64.max);
          break;
        }

        default:
        {
          // For any other underlying type, we fall back to our generic domain
          // check utility function.
          //
          if (!value_in_domain (d.type, d.default_value, d.domain))
            Com_Error (ERR_FATAL,
                       "dvar: '%s' (type %d) default fails domain check",
                       d.name, static_cast<int> (d.type));
          break;
        }
      }
    }

    log::debug ("dvar: validated {} declaration(s)", n);
  }

  void
  init_subsystem () noexcept
  {
    ZoneScoped;

    // The subsystem must not be initialized more than once.
    //
    // If it is, we log a warning and return early to prevent internal state
    // corruption.
    //
    if (s_initialized)
    {
      log::warning ("dvar: init_subsystem() called more than once");
      return;
    }

    s_initialized = true;
    log::info ("dvar: subsystem ready");
  }

  void
  register_owned () noexcept
  {
    ZoneScoped;

    // Registration requires the subsystem to be initialized beforehand.
    //
    // We terminate immediately if it is not.
    //
    if (!s_initialized)
      Com_Error (ERR_FATAL,
                 "dvar: register_owned() called before init_subsystem()");

    // We only run the registration pass once to avoid duplicate memory
    // allocations and state corruption within the engine's internal dvar pool.
    //
    if (s_registered)
      Com_Error (ERR_FATAL,
                 "dvar: register_owned() called more than once");

    constexpr auto n (extent_v<decltype (declarations)>);
    log::debug ("dvar: registering {} IW4x dvar(s)", n);

    // Run our validation pass right before we begin committing dvars to the
    // engine.
    //
    validate_declarations ();

    for (size_t i (0); i < n; ++i)
    {
      const auto& d (declarations[i]);

      mark_registration_flags (d.flags);

      dvar_t* ptr (register_variant (d.name, d.type, d.flags,
                                     d.default_value, d.domain,
                                     d.description));

      // If the engine fails to allocate or register the dvar, it indicates a
      // serious internal consistency issue, such as the dvar pool being
      // exhausted. We have to bail out.
      //
      if (ptr == nullptr)
        Com_Error (ERR_FATAL,
                   "dvar: register_variant returned null for '%s'", d.name);

      log::debug ("dvar: registered '{}'", d.name);

      // Assign the parallel handle for row i.
      //
      // Once you start adding handles above, insert the corresponding
      // assignments right here in table order.
      //
      (void) ptr;
    }

    s_registered = true;
  }

  void
  resolve_engine_refs () noexcept
  {
    ZoneScoped;

    // We only resolve external engine references after we have registered our
    // owned dvars.
    //
    if (!s_registered)
      Com_Error (ERR_FATAL,
                 "dvar: resolve_engine_refs() called before register_owned()");

    // Like the other phases, resolution is a one-time operation.
    //
    if (s_resolved)
    {
      log::warning ("dvar: resolve_engine_refs() called more than once");
      return;
    }

    constexpr auto n (extent_v<decltype (engine_refs)>);
    log::info ("dvar: resolving {} engine dvar reference(s)", n);

    for (size_t i (0); i < n; ++i)
    {
      const auto& ref (engine_refs[i]);

      assert (ref.name       != nullptr);
      assert (ref.out_handle != nullptr);

      dvar_t* ptr (find_raw (ref.name));

      // If we fail to find a dvar that is marked as required, it means the
      // engine is missing functionality that we depend upon.
      //
      if (ptr == nullptr && !ref.optional)
        Com_Error (ERR_FATAL,
                   "dvar: required engine dvar '%s' (expected type %d) "
                   "not found",
                   ref.name, static_cast<int> (ref.expected_type));

      // Runtime type must match what we actually expect it to be.
      //
      if (ptr != nullptr &&
          ref.expected_type != DVAR_TYPE_COUNT &&
          ptr->type != ref.expected_type)
      {
        if (!ref.optional)
          Com_Error (ERR_FATAL,
                     "dvar: engine dvar '%s' has type %d but %d required",
                     ref.name,
                     static_cast<int> (ptr->type),
                     static_cast<int> (ref.expected_type));

        // For optional references, a type mismatch means we cannot use the
        // dvar.
        //
        log::warning ("dvar: optional engine dvar '{}' type mismatch "
                      "(got {}, expected {}) - handle left invalid",
                      ref.name,
                      static_cast<int> (ptr->type),
                      static_cast<int> (ref.expected_type));
        ptr = nullptr;
      }

      // Populate the local target handle.
      //
      *ref.out_handle = { ptr, ref.expected_type };

      log::info ("dvar: resolved '{}' -> {}",
                 ref.name,
                 (ptr != nullptr ? "ok" : "optional-miss"));
    }

    s_resolved = true;
    log::info ("dvar: resolution complete");
  }

  void
  apply_descriptions () noexcept
  {
    ZoneScoped;

    // First, we apply any description patches from our patch table.
    //
    {
      constexpr auto n (extent_v<decltype (desc_patches)>);
      log::info ("dvar: applying {} description patch(es)", n);

      for (size_t i (0); i < n; ++i)
      {
        const auto& p (desc_patches[i]);

        assert (p.dvar_name   != nullptr);
        assert (p.description != nullptr);

        dvar_t* d (find_raw (p.dvar_name));

        // If the target dvar is missing, there is obviously nothing for us to
        // patch.
        //
        if (d == nullptr)
        {
          log::warning ("dvar: description patch target '{}' not found",
                        p.dvar_name);
          continue;
        }

        set_description (d, p.description);
        log::debug ("dvar: patched description for '{}'", p.dvar_name);
      }
    }

    // Finally, we apply any overriding descriptions that were specified
    // directly on our resolved engine references.
    //
    {
      constexpr auto n (extent_v<decltype (engine_refs)>);

      for (size_t i (0); i < n; ++i)
      {
        const auto& ref (engine_refs[i]);

        // Skip processing if there is no overriding description provided in
        // the table row.
        //
        if (ref.description == nullptr)
          continue;

        // Skip processing if we failed to resolve this specific engine handle.
        //
        if (ref.out_handle == nullptr || ref.out_handle->ptr == nullptr)
          continue;

        set_description (ref.out_handle->ptr, ref.description);
        log::debug ("dvar: applied ref description for '{}'", ref.name);
      }
    }
  }
}
