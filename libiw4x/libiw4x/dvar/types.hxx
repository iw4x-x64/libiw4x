#pragma once

#include <cstddef>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <string>

#include <libiw4x/import.hxx>

namespace iw4x::dvar
{
  // Pool geometry.
  //
  // Note that these are the engine's fixed-size allocations. We export them so
  // that test harnesses and assertion helpers can validate pool membership
  // without coupling to internal engine symbols. This saves us from a lot of
  // linking headaches later on.
  //
  inline constexpr std::size_t pool_capacity (0x1000);
  inline constexpr std::size_t hash_buckets  (0x400);

  // Typed, validated reference to an engine dvar.
  //
  // The idea here is to bind a raw dvar_t pointer to the type we declared at
  // lookup or registration time. Note that every accessor checks the type tag
  // before touching the value union.
  //
  // Also note that an invalid handle (ptr == nullptr) is perfectly safe to pass
  // everywhere. Accessors will simply return the type's zero value and log a
  // warning. This lets us work with optional dvars without littering null
  // checks all over the call sites.
  //
  // Do not construct these by hand. Instead, obtain them from the lookup
  // functions or from the exported handle globals.
  //
  struct handle
  {
    // Nullptr means invalid or optional miss.
    //
    dvar_t* ptr;

    // Type asserted at lookup or registration.
    //
    dvarType expected_type;
  };

  // Controls mutation semantics.
  //
  // Here INTERNAL bypasses cheat, ROM, and INIT guards, so use it carefully.
  // EXTERNAL is subject to all guards. SCRIPT is essentially like EXTERNAL but
  // without the cheat bypass. Also, remember not to use DEVGUI.
  //
  using source = DvarSetSource;

  // Thrown when a value violates its dvar's constraints.
  //
  // We don't want clamping on the public API boundary. That is, the internal
  // engine paths still clamp, but our own typed API surfaces the problem.
  //
  struct domain_error: std::runtime_error
  {
    std::string dvar_name;
    std::string detail;

    explicit
    domain_error (const std::string& name, const std::string& what)
      : std::runtime_error (std::format ("dvar '{}': domain violation: {}",
                                         name, what)),
        dvar_name (name),
        detail (what) {}
  };

  // Thrown when a handle's expected type does not match the underlying dvar's
  // actual type.
  //
  struct type_mismatch: std::runtime_error
  {
    std::string dvar_name;
    int expected;
    int actual;

    explicit
    type_mismatch (const std::string& name, int exp, int act)
      : std::runtime_error (std::format ("dvar '{}': type mismatch: expected {} actual {}",
                                         name, exp, act)),
        dvar_name (name),
        expected (exp),
        actual (act) {}
  };

  // Thrown by require() when a dvar does not exist.
  //
  struct not_found: std::runtime_error
  {
    std::string dvar_name;

    explicit
    not_found (const std::string& name)
      : std::runtime_error (std::format ("dvar '{}': not found",
                                         name)),
        dvar_name (name) {}
  };

  // Compile-time description of a single IW4x-owned dvar.
  //
  // Note that all pointer-typed fields (like name, description, and string
  // enumerations) must have static storage duration. The engine holds the
  // pointer but it doesn't copy the data.
  //
  // Validation is performed at registration time by validate_declarations ().
  // The invariants are quite strict here: name is non-null and unique
  // (case-insensitive), description is non-null, flags must not include
  // DVAR_EXTERNAL, and the default value must satisfy value_in_domain ().
  //
  struct declaration
  {
    const char* name;
    dvarType    type;
    DvarValue   default_value;
    DvarLimits  domain;
    DvarFlags   flags;
    const char* description;
  };

  // Compile-time description of a required engine dvar that we want a handle
  // to.
  //
  // The out_handle is written by resolve (). Keep in mind that for required
  // refs, a missing or mistyped dvar is fatal. For optional refs, out_handle
  // is simply left invalid.
  //
  struct engine_ref
  {
    const char* name;
    dvarType    expected_type;
    bool        optional;
    handle*     out_handle;
    const char* description;
  };

  // Post-registration description override.
  //
  // This is applied after all registrations complete. It is non-fatal if the
  // target dvar does not exist (we just log a warning and skip it).
  //
  struct description_patch
  {
    const char* dvar_name;
    const char* description;
  };

  // For-each callback signature.
  //
  // The engine's own iteration uses the __fastcall convention, so we must
  // preserve that here to avoid corrupting the stack.
  //
  using for_each_fn = void (__fastcall*) (const dvar_t*, void*);
}
