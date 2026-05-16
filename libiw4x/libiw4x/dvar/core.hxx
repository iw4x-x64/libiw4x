#pragma once

#include <cstdint>
#include <cstring>

#include <libiw4x/dvar/types.hxx>
#include <libiw4x/dvar/utility.hxx>
#include <libiw4x/import.hxx>

namespace iw4x::dvar
{
  // Hash a dvar name.
  //
  // We use the exact same polynomial algorithm as the base MW2 engine here.
  // We have verified this by cross-referencing hash values for known dvars
  // out of the client. The engine treats dvar names as case-insensitive, so
  // we just fold everything down using tolower () as we step through.
  //
  // Notice that this returns the raw hash value. It is up to the caller to
  // mask this against the current bucket count before using it to index into
  // the actual hash table.
  //
  DVAR_ALWAYS_INLINE std::uint32_t
  hash_name (const char* name) noexcept
  {
    std::uint32_t h (0);
    std::uint32_t mul ('w');

    for (const auto* p (reinterpret_cast<const unsigned char*> (name));
         *p != '\0';
         ++p)
    {
      h += static_cast<std::uint32_t> (tolower (*p)) * mul;
      ++mul;
    }

    return h;
  }

  // Look up a dvar by its name.
  //
  // This performs a raw lookup directly in the engine's internal hash table.
  // You need to make sure you are holding a shared read lock before calling
  // this, otherwise you risk a race condition. If the dvar isn't registered,
  // we simply return a null pointer.
  //
  // Keep in mind that this doesn't validate the dvar's type or flags at all.
  // You get exactly what we find.
  //
  dvar_t*
  find_raw (const char* name) noexcept;

  // Iterate over all registered dvars.
  //
  // This walks through the dvars in a sorted order. If the internal list
  // happens to be unsorted when this is called, we will sort it right here
  // first. Then, we invoke your callback for each dvar while holding the
  // read lock.
  //
  // Because we hold that lock during iteration, your callback absolutely
  // cannot try to register or unregister any dvars. If you do, we will
  // immediately deadlock the thread.
  //
  void
  for_each (for_each_fn fn, void* user_data) noexcept;

  // Register or update a dvar in the engine hash table.
  //
  // This is the primary entry point for creating new dvars. It handles a lot
  // of tricky internal engine mechanics for you, like re-registration, safely
  // promoting DVAR_EXTERNAL dvars to native ones, and grabbing fresh
  // allocations from the dvar pool.
  //
  // If the dvar pool is completely full, we will just throw a fatal error
  // since there is really no recovering from that state. Also, make sure you
  // call mark_registration_flags () before calling this so the internal engine
  // state is prepared.
  //
  dvar_t*
  register_variant (const char* name,
                    dvarType type,
                    DvarFlags flags,
                    DvarValue value,
                    DvarLimits domain,
                    const char* description) noexcept;

  // Register custom 64-bit integer dvars.
  //
  // These are IW4x-specific extensions. The base MW2 engine doesn't natively
  // support INT64 or UINT64, so we provide these dedicated registration
  // functions. Conveniently, both of these will call mark_registration_flags ()
  // internally for you, so you don't have to worry about managing that step.
  //
  dvar_t*
  register_int64 (const char* name,
                  std::int64_t value,
                  std::int64_t min,
                  std::int64_t max,
                  DvarFlags flags,
                  const char* description) noexcept;

  dvar_t*
  register_uint64 (const char* name,
                   std::uint64_t value,
                   std::uint64_t min,
                   std::uint64_t max,
                   DvarFlags flags,
                   const char* description) noexcept;

  // Check if a value falls within the valid domain for its type.
  //
  // We always inline this because it sits directly on the critical path of
  // set_variant (), meaning it gets called on literally every single dvar
  // mutation in the game.
  //
  DVAR_ALWAYS_INLINE bool
  value_in_domain (dvarType type,
                   const DvarValue& value,
                   const DvarLimits& domain) noexcept
  {
    switch (type)
    {
      case DVAR_TYPE_BOOL:
      case DVAR_TYPE_STRING:
      case DVAR_TYPE_COLOR:
        return true;

      case DVAR_TYPE_FLOAT:
        return value.value >= domain.value.min &&
               value.value <= domain.value.max;

      case DVAR_TYPE_FLOAT_2:
        return value.vector.value[0] >= domain.vector.min &&
               value.vector.value[0] <= domain.vector.max &&
               value.vector.value[1] >= domain.vector.min &&
               value.vector.value[1] <= domain.vector.max;

      case DVAR_TYPE_FLOAT_3:
        return value.vector.value[0] >= domain.vector.min &&
               value.vector.value[0] <= domain.vector.max &&
               value.vector.value[1] >= domain.vector.min &&
               value.vector.value[1] <= domain.vector.max &&
               value.vector.value[2] >= domain.vector.min &&
               value.vector.value[2] <= domain.vector.max;

      case DVAR_TYPE_FLOAT_4:
        return value.vector.value[0] >= domain.vector.min &&
               value.vector.value[0] <= domain.vector.max &&
               value.vector.value[1] >= domain.vector.min &&
               value.vector.value[1] <= domain.vector.max &&
               value.vector.value[2] >= domain.vector.min &&
               value.vector.value[2] <= domain.vector.max &&
               value.vector.value[3] >= domain.vector.min &&
               value.vector.value[3] <= domain.vector.max;

      case DVAR_TYPE_INT:
        return value.integer >= domain.integer.min &&
               value.integer <= domain.integer.max;

      case DVAR_TYPE_INT64:
        return value.integer64 >= domain.integer64.min &&
               value.integer64 <= domain.integer64.max;

      case DVAR_TYPE_UINT64:
        return value.unsignedInt64 >= domain.unsignedInt64.min &&
               value.unsignedInt64 <= domain.unsignedInt64.max;

      case DVAR_TYPE_ENUM:
        // Index 0 is always considered valid as a safe fallback.
        //
        return value.integer == 0 ||
               (value.integer > 0 &&
                value.integer < domain.enumeration.stringCount);

      case DVAR_TYPE_FLOAT_3_COLOR:
        return value.vector.value[0] >= 0.0f &&
               value.vector.value[0] <= domain.vector.max &&
               value.vector.value[1] >= 0.0f &&
               value.vector.value[1] <= domain.vector.max &&
               value.vector.value[2] >= 0.0f &&
               value.vector.value[2] <= domain.vector.max;

      default:
        return false;
    }
  }

  // Perform a deep equality check between two DvarValues.
  //
  // We assume both values share the same type. For strings, we actually have
  // to compare the full string contents rather than just the pointers. The
  // engine sometimes holds multiple separate allocations of the identical
  // string across different slots, so a simple pointer equality check will
  // yield false negatives.
  //
  DVAR_ALWAYS_INLINE bool
  values_equal (dvarType type, const DvarValue& a, const DvarValue& b) noexcept
  {
    switch (type)
    {
      case DVAR_TYPE_BOOL:
        return a.enabled == b.enabled;

      case DVAR_TYPE_FLOAT:
        return a.value == b.value;

      case DVAR_TYPE_FLOAT_2:
        return a.vector.x == b.vector.x &&
               a.vector.y == b.vector.y;

      case DVAR_TYPE_FLOAT_3:
      case DVAR_TYPE_FLOAT_3_COLOR:
        return a.vector.x == b.vector.x &&
               a.vector.y == b.vector.y &&
               a.vector.z == b.vector.z;

      case DVAR_TYPE_FLOAT_4:
        return a.vector.x == b.vector.x &&
               a.vector.y == b.vector.y &&
               a.vector.z == b.vector.z &&
               a.vector.w == b.vector.w;

      case DVAR_TYPE_INT:
      case DVAR_TYPE_ENUM:
        return a.integer == b.integer;

      case DVAR_TYPE_INT64:
        return a.integer64 == b.integer64;

      case DVAR_TYPE_UINT64:
        return a.unsignedInt64 == b.unsignedInt64;

      case DVAR_TYPE_STRING:
        if (a.string == b.string) return true;
        if (a.string == nullptr || b.string == nullptr) return false;
        return std::strcmp (a.string, b.string) == 0;

      case DVAR_TYPE_COLOR:
        return __builtin_memcmp (a.color, b.color, 4) == 0;

      default:
        return false;
    }
  }

  // Write directly to a dvar's latched slot.
  //
  // For primitive, non-string types, this is just a straightforward memory
  // copy. For strings, however, it takes care of resolving canonical pointer
  // reuse, which prevents redundant string allocations from flooding the pool.
  //
  void
  set_latched (dvar_t* d, const DvarValue& value) noexcept;

  // Set a dvar's variant value based on the given source context.
  //
  // This is our central mutation primitive. The source context dictates the
  // strictness of the rules for how the value gets applied:
  //
  //   INTERNAL bypasses everything (cheat flags, ROM status, INIT checks).
  //   EXTERNAL is the strictest and respects all guards. If the dvar is
  //            latched, this redirects over to set_latched ().
  //   SCRIPT   behaves just like EXTERNAL, but it explicitly will not bypass
  //            cheat protections.
  //   DEVGUI   is reserved for UI usage but currently handled as EXTERNAL.
  //
  void
  set_variant (dvar_t* d,
               const DvarValue& value,
               DvarSetSource source) noexcept;

  // Raw typed setters.
  //
  // These are primarily meant to be used by engine hooks and other internal
  // sub-systems that need to forcefully update a dvar without running it
  // through the standard string-based parsing pipeline.
  //
  void
  set_bool (dvar_t* d, bool v, DvarSetSource s) noexcept;

  void
  set_int (dvar_t* d, int v, DvarSetSource s) noexcept;

  void
  set_float (dvar_t* d, float v, DvarSetSource s) noexcept;

  void
  set_string (dvar_t* d, const char* v, DvarSetSource s) noexcept;

  void
  set_int64 (dvar_t* d,
             std::int64_t v,
             DvarSetSource s = DVAR_SOURCE_INTERNAL) noexcept;

  void
  set_uint64 (dvar_t* d,
              std::uint64_t v,
              DvarSetSource s = DVAR_SOURCE_INTERNAL) noexcept;

  // Overwrite the reset slot for a specific dvar.
  //
  // You will typically see this being called by set_command () during the
  // autoexec phase when the engine loads up the default configuration files.
  //
  void
  update_reset_value (dvar_t* d, const DvarValue& value) noexcept;

  // Revert a dvar to its reset value.
  //
  // This simply grabs whatever is currently residing in the reset slot and
  // applies it back using set_variant ().
  //
  void
  reset (dvar_t* d, DvarSetSource source) noexcept;

  // Strip the DVAR_SCRIPTINFO flag from all dvars.
  //
  // We iterate through every single registered dvar and clear this flag out.
  //
  void
  reset_script_info () noexcept;

  // Safely append new flags to an existing dvar.
  //
  // This is null-safe. You can pass a null dvar pointer here and it will
  // just silently early-out instead of crashing.
  //
  void
  add_flags (dvar_t* d, DvarFlags flags) noexcept;

  // Retrieve custom 64-bit integer values.
  //
  // Because the engine obviously doesn't have native getters for our extended
  // types, we provide these utility functions so you can safely extract them.
  //
  std::int64_t
  get_int64 (const dvar_t* d) noexcept;

  std::uint64_t
  get_uint64 (const dvar_t* d) noexcept;

  // Convert a DvarValue back into a printable C-string.
  //
  // This returns a pointer into the engine's thread-local rotating string
  // buffer. Because it's a rotating buffer, the returned pointer is highly
  // ephemeral. If you need the string to survive across multiple function
  // calls or frames, you absolutely must copy it out yourself.
  //
  const char*
  value_to_string (const dvar_t* d, const DvarValue& value) noexcept;

  // Apply a raw string value to a dvar by its name.
  //
  // If the dvar name isn't actually registered yet, we assume it's an
  // uninitialized configuration variable and will automatically create it
  // as a DVAR_EXTERNAL string dvar.
  //
  void
  set_command (const char* name, const char* value) noexcept;

  // Set a dvar from a string, purely by its name.
  //
  // We do a lookup by name here. If we find it, we go ahead and set it
  // using INTERNAL source privileges. If it doesn't exist, we fallback to
  // creating it as a new EXTERNAL dvar.
  //
  dvar_t*
  set_from_string_by_name (const char* name, const char* value) noexcept;
}
