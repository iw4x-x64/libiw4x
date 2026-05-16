#include <libiw4x/mod/dvar.hxx>

#include <cstdlib>
#include <cstring>

#include <tracy/Tracy.hpp>

#include <libiw4x/dvar/abi.hxx>
#include <libiw4x/dvar/command.hxx>
#include <libiw4x/dvar/core.hxx>
#include <libiw4x/dvar/declaration.hxx>
#include <libiw4x/dvar/utility.hxx>

#include <libiw4x/detour.hxx>
#include <libiw4x/import.hxx>
#include <libiw4x/logger.hxx>

namespace iw4x::mod
{
  dvar_t*
  find_var (const char* name)
  {
    return iw4x::dvar::find_raw (name);
  }

  void
  for_each (dvar::for_each_fn fn, void* user_data)
  {
    iw4x::dvar::for_each (fn, user_data);
  }

  dvar_t*
  register_variant (const char* name,
                    dvarType type,
                    DvarFlags flags,
                    const DvarValue* value,
                    const DvarLimits* domain,
                    const char* description)
  {
    ZoneScoped;

    DvarValue v {};
    DvarLimits d {};

    // The engine might pass null pointers for value or domain if it relies on
    // zero-initialization. We catch those here and pass default-initialized
    // structs down to our internal registration.
    //
    if (value  != nullptr) v = *value;
    if (domain != nullptr) d = *domain;

    // Notice that we also need to explicitly mark the registration
    // flags here before proceeding.
    //
    dvar::mark_registration_flags (flags);

    return iw4x::dvar::register_variant (name, type, flags, v, d, description);
  }

  dvar_t*
  register_int (const char* name,
                int value,
                int min,
                int max,
                DvarFlags flags,
                const char* description)
  {
    ZoneScoped;

    dvar::mark_registration_flags (flags);

    DvarValue v {};
    v.integer = value;

    DvarLimits d {};
    d.integer.min = min;
    d.integer.max = max;

    // Unlike most other types, we completely bypass the engine's implementation
    // for integers as we need to handle our custom 64-bit types without getting
    // tangled in the engine's limited 32-bit logic.
    //
    return iw4x::dvar::register_variant (name,
                                         DVAR_TYPE_INT,
                                         flags,
                                         v,
                                         d,
                                         description);
  }

  dvar_t*
  register_bool (const char* name,
                 bool value,
                 DvarFlags flags,
                 const char* description)
  {
    ZoneScoped;

    dvar_t* d (Dvar_RegisterBool (name, value, flags, description));
    dvar::set_description (d, description);
    return d;
  }

  dvar_t*
  register_float (const char* name,
                  float value,
                  float min,
                  float max,
                  DvarFlags flags,
                  const char* description)
  {
    ZoneScoped;

    dvar_t* d (Dvar_RegisterFloat (name, value, min, max, flags, description));
    dvar::set_description (d, description);
    return d;
  }

  dvar_t*
  register_string (const char* name,
                   const char* value,
                   DvarFlags flags,
                   const char* description)
  {
    ZoneScoped;

    dvar_t* d (Dvar_RegisterString (name, value, flags, description));
    dvar::set_description (d, description);
    return d;
  }

  dvar_t*
  register_enum (const char* name,
                 const char** value_list,
                 int default_index,
                 DvarFlags flags,
                 const char* description)
  {
    ZoneScoped;

    dvar_t* d (Dvar_RegisterEnum (name, value_list, default_index, flags, description));
    dvar::set_description (d, description);
    return d;
  }

  dvar_t*
  register_color (const char* name,
                  float r,
                  float g,
                  float b,
                  float a,
                  DvarFlags flags,
                  const char* description)
  {
    ZoneScoped;

    dvar_t* d (Dvar_RegisterColor (name, r, g, b, a, flags, description));
    dvar::set_description (d, description);
    return d;
  }

  dvar_t*
  register_vec2 (const char* name,
                 float x,
                 float y,
                 float min,
                 float max,
                 DvarFlags flags,
                 const char* description)
  {
    ZoneScoped;

    dvar_t* d (Dvar_RegisterVec2 (name, x, y, min, max, flags, description));
    dvar::set_description (d, description);
    return d;
  }

  dvar_t*
  register_vec3 (const char* name,
                 float x,
                 float y,
                 float z,
                 float min,
                 float max,
                 DvarFlags flags,
                 const char* description)
  {
    ZoneScoped;

    dvar_t* d (Dvar_RegisterVec3 (name, x, y, z, min, max, flags, description));
    dvar::set_description (d, description);
    return d;
  }

  dvar_t*
  register_vec3_color (const char* name,
                       float r,
                       float g,
                       float b,
                       DvarFlags flags,
                       const char* description)
  {
    ZoneScoped;

    dvar_t* d (Dvar_RegisterVec3Color (name, r, g, b, flags, description));
    dvar::set_description (d, description);
    return d;
  }

  dvar_t*
  register_vec4 (const char* name,
                 float x,
                 float y,
                 float z,
                 float w,
                 float min,
                 float max,
                 DvarFlags flags,
                 const char* description)
  {
    ZoneScoped;

    dvar_t* d (Dvar_RegisterVec4 (name, x, y, z, w, min, max, flags, description));
    dvar::set_description (d, description);
    return d;
  }

  bool
  value_in_domain (dvarType type, DvarValue value, DvarLimits domain)
  {
    return iw4x::dvar::value_in_domain (type, value, domain);
  }

  bool
  values_equal (dvarType type, DvarValue lhs, DvarValue rhs)
  {
    return iw4x::dvar::values_equal (type, lhs, rhs);
  }

  void
  set_latched_value (dvar_t* d, DvarValue* value)
  {
    if (value != nullptr)
      iw4x::dvar::set_latched (d, *value);
  }

  void
  reset_dvar (dvar_t* d, DvarSetSource source)
  {
    iw4x::dvar::reset (d, source);
  }

  void
  reset_script_info ()
  {
    iw4x::dvar::reset_script_info ();
  }

  void
  update_reset_value (dvar_t* d, DvarValue value)
  {
    iw4x::dvar::update_reset_value (d, value);
  }

  void
  add_flags (dvar_t* d, DvarFlags flags)
  {
    iw4x::dvar::add_flags (d, flags);
  }

  int
  string_to_enum (DvarLimits domain, const char* string)
  {
    return iw4x::dvar::string_to_enum (domain, string);
  }

  DvarValue*
  string_to_value (DvarValue* out,
                   dvarType type,
                   DvarLimits* domain,
                   const char* string)
  {
    return iw4x::dvar::string_to_value (out, type, domain, string);
  }

  void
  string_to_color (const char* string, DvarValue* value)
  {
    if (value != nullptr)
      iw4x::dvar::string_to_color (string, *value);
  }

  void
  get_combined_string (char* out, int start_arg)
  {
    iw4x::dvar::get_combined_string (out, start_arg);
  }

  const char*
  value_to_string (dvar_t* d, DvarValue value)
  {
    return iw4x::dvar::value_to_string (d, value);
  }

  const char*
  displayable_value (dvar_t* d)
  {
    if (d == nullptr)
      return "";

    return iw4x::dvar::value_to_string (d, d->current);
  }

  bool
  get_bool (const char* name)
  {
    dvar_t* d (iw4x::dvar::find_raw (name));

    if (d == nullptr)
      return false;

    // We need to gracefully coerce any underlying dvar type into a boolean.
    // Note that for strings we fall back to std::atoi () which matches the
    // engine's legacy string evaluation semantics.
    //
    switch (d->type)
    {
      case DVAR_TYPE_BOOL:
        return d->current.enabled;

      case DVAR_TYPE_INT:
      case DVAR_TYPE_ENUM:
        return d->current.integer != 0;

      case DVAR_TYPE_INT64:
        return d->current.integer64 != 0;

      case DVAR_TYPE_UINT64:
        return d->current.unsignedInt64 != 0;

      case DVAR_TYPE_FLOAT:
        return d->current.value != 0.0f;

      case DVAR_TYPE_STRING:
        return (d->current.string != nullptr) && (std::atoi (d->current.string) != 0);

      default:
        return false;
    }
  }

  float
  get_float (const char* name)
  {
    dvar_t* d (iw4x::dvar::find_raw (name));

    if (d == nullptr)
      return 0.0f;

    switch (d->type)
    {
      case DVAR_TYPE_FLOAT:
        return d->current.value;

      case DVAR_TYPE_BOOL:
        return d->current.enabled ? 1.0f : 0.0f;

      case DVAR_TYPE_INT:
      case DVAR_TYPE_ENUM:
        return static_cast<float> (d->current.integer);

      case DVAR_TYPE_INT64:
        return static_cast<float> (d->current.integer64);

      case DVAR_TYPE_UINT64:
        return static_cast<float> (d->current.unsignedInt64);

      case DVAR_TYPE_STRING:
        return (d->current.string != nullptr) ? static_cast<float> (std::atof (d->current.string)) : 0.0f;

      default:
        return 0.0f;
    }
  }

  int
  get_int_by_name (const char* name)
  {
    dvar_t* d (iw4x::dvar::find_raw (name));

    // Fetch the value as a 64-bit integer first. If the requested dvar
    // actually stores a value larger than INT_MAX, we saturate it down
    // to avoid overflow before returning to the 32-bit caller.
    //
    return d != nullptr ? iw4x::dvar::saturate_to_int (iw4x::dvar::get_int64 (d)) : 0;
  }

  const char*
  get_string (const char* name)
  {
    dvar_t* d (iw4x::dvar::find_raw (name));
    return d != nullptr ? iw4x::dvar::value_to_string (d, d->current) : "";
  }

  const char*
  get_variant_string (const char* name)
  {
    dvar_t* d (iw4x::dvar::find_raw (name));
    return d != nullptr ? iw4x::dvar::value_to_string (d, d->current) : "";
  }

  void
  set_bool (dvar_t* d, bool value)
  {
    iw4x::dvar::set_bool (d, value, DVAR_SOURCE_INTERNAL);
  }

  void
  set_bool_by_name (const char* name, bool value)
  {
    iw4x::dvar::set_bool (iw4x::dvar::find_raw (name),
                          value,
                          DVAR_SOURCE_INTERNAL);
  }

  void
  set_float (dvar_t* d, float value)
  {
    iw4x::dvar::set_float (d, value, DVAR_SOURCE_INTERNAL);
  }

  void
  set_int (dvar_t* d, int value)
  {
    iw4x::dvar::set_int (d, value, DVAR_SOURCE_INTERNAL);
  }

  void
  set_int_by_name (const char* name, int value)
  {
    iw4x::dvar::set_int (iw4x::dvar::find_raw (name),
                         value,
                         DVAR_SOURCE_INTERNAL);
  }

  void
  set_string (dvar_t* d, const char* value)
  {
    iw4x::dvar::set_string (d, value, DVAR_SOURCE_INTERNAL);
  }

  void
  set_string_by_name (const char* name, const char* value)
  {
    iw4x::dvar::set_string (iw4x::dvar::find_raw (name),
                            value,
                            DVAR_SOURCE_INTERNAL);
  }

  dvar_t*
  set_from_string_by_name (const char* name, const char* value)
  {
    return iw4x::dvar::set_from_string_by_name (name, value);
  }

  void
  set_from_string_from_source (dvar_t* d,
                               const char* value,
                               DvarSetSource source)
  {
    iw4x::dvar::set_string (d, value, source);
  }

  void
  set_command (const char* name, const char* value)
  {
    iw4x::dvar::set_command (name, value);
  }

  void
  set_variant (dvar_t* d, DvarValue value, DvarSetSource source)
  {
    iw4x::dvar::set_variant (d, value, source);
  }

  void
  set_domain_func (dvar_t* d, bool (*callback) (dvar_t*, DvarValue*))
  {
    ZoneScoped;

    if (d == nullptr)
      return;

    d->domainFunc = callback;

    if (callback == nullptr)
      return;

    // Check that the current value satisfies the new constraint. If the check
    // fails, we force a reset back to the default value.
    //
    DvarValue val (d->current);

    if (!callback (d, &val))
      iw4x::dvar::set_variant (d, d->reset, DVAR_SOURCE_INTERNAL);
  }

  int
  command ()
  {
    return dvar::cmd_dispatch ();
  }

  void
  reset_f ()
  {
    dvar::cmd_reset ();
  }

  void
  set_f ()
  {
    dvar::cmd_set ();
  }

  void
  seta_f ()
  {
    dvar::cmd_seta ();
  }

  void
  set_from_dvar_f ()
  {
    dvar::cmd_setfromdvar ();
  }

  bool
  toggle_internal ()
  {
    if (iw4x::dvar::cmd_argc () < 2)
      return false;

    if (iw4x::dvar::find_raw (iw4x::dvar::cmd_argv (1)) == nullptr)
      return false;

    dvar::cmd_toggle ();
    return true;
  }

  void
  toggle_print_f ()
  {
    dvar::cmd_toggle ();
  }

  void
  add_commands ()
  {
    ZoneScoped;

    using cmd_fn = void (*) ();

    Cmd_AddCommandInternal ("toggle",
                            reinterpret_cast<cmd_fn> (toggle_print_f),
                            reinterpret_cast<cmd_function_s*> (dvar::k_cmd_toggle));

    Cmd_AddCommandInternal ("togglep",
                            reinterpret_cast<cmd_fn> (toggle_print_f),
                            reinterpret_cast<cmd_function_s*> (dvar::k_cmd_togglep));

    Cmd_AddCommandInternal ("set",
                            reinterpret_cast<cmd_fn> (set_f),
                            reinterpret_cast<cmd_function_s*> (dvar::k_cmd_set));

    Cmd_AddCommandInternal ("seta",
                            reinterpret_cast<cmd_fn> (seta_f),
                            reinterpret_cast<cmd_function_s*> (dvar::k_cmd_seta));

    Cmd_AddCommandInternal ("reset",
                            reinterpret_cast<cmd_fn> (reset_f),
                            reinterpret_cast<cmd_function_s*> (dvar::k_cmd_reset));

    Cmd_AddCommandInternal ("setfromdvar",
                            reinterpret_cast<cmd_fn> (set_from_dvar_f),
                            reinterpret_cast<cmd_function_s*> (dvar::k_cmd_setfromdvar));
  }

  void
  init ()
  {
    ZoneScoped;

    // Mark the subsystem as initialised. Note that we do this right away
    // so any subsequent calls evaluating system state know we are active.
    //
    *reinterpret_cast<unsigned char*> (dvar::k_inited_flag) = 1;

    // Allow all flag categories during init. We temporarily lift context
    // restrictions by poking directly into the engine's thread-local storage.
    // Restrictions will naturally apply once the subsystem is fully up.
    //
    void* tls (dvar::engine_tls ());

    if (tls != nullptr)
    {
      int& allowed (*reinterpret_cast<int*> (static_cast<unsigned char*> (tls) +
                                             dvar::k_tls_allowed_off));
      allowed = -1;
    }

    add_commands ();

    dvar::init_subsystem ();
    dvar::register_owned ();
    dvar::resolve_engine_refs ();
    dvar::apply_descriptions ();
  }

  dvar_module::
  dvar_module ()
  {
    // Console commands / init
    //
    detour (Dvar_Init,                    &init);
    detour (Dvar_AddCommands,             &add_commands);
    detour (Dvar_Command,                 &command);
    detour (Dvar_Reset_f,                 &reset_f);
    detour (Dvar_Set_f,                   &set_f);
    detour (Dvar_SetA_f,                  &seta_f);
    detour (Dvar_SetFromDvar_f,           &set_from_dvar_f);
    detour (Dvar_ToggleInternal,          &toggle_internal);
    detour (Dvar_TogglePrint_f,           &toggle_print_f);
    detour (Dvar_GetCombinedString,       &get_combined_string);

    // Find / iteration
    //
    detour (Dvar_FindMalleableVar,        &find_var);
    detour (Dvar_ForEach,                 &for_each);

    // Registration
    //
    // Every type-specific registration function is redirected. For types
    // where the engine's own implementation is correct (bool, float, vec*,
    // enum, color, string) we trampoline through the original and then
    // capture the description. For int (and IW4x extensions) we fully
    // reimplement via register_variant.
    //
    //
    detour (Dvar_RegisterVariant,         &register_variant);
    detour (Dvar_RegisterInt,             &register_int);
    detour (Dvar_RegisterBool,            &register_bool);
    detour (Dvar_RegisterFloat,           &register_float);
    detour (Dvar_RegisterString,          &register_string);
    detour (Dvar_RegisterEnum,            &register_enum);
    detour (Dvar_RegisterColor,           &register_color);
    detour (Dvar_RegisterVec2,            &register_vec2);
    detour (Dvar_RegisterVec3,            &register_vec3);
    detour (Dvar_RegisterVec3Color,       &register_vec3_color);
    detour (Dvar_RegisterVec4,            &register_vec4);

    // Domain / equality
    //
    detour (Dvar_ValueInDomain,           &value_in_domain);
    detour (Dvar_ValuesEqual,             &values_equal);

    // Latched / reset
    //
    detour (Dvar_SetLatchedValue,         &set_latched_value);
    detour (Dvar_Reset,                   &reset_dvar);
    detour (Dvar_ResetScriptInfo,         &reset_script_info);
    detour (Dvar_UpdateResetValue,        &update_reset_value);

    // Flags
    //
    detour (Dvar_AddFlags,                &add_flags);

    // String conversion
    //
    detour (Dvar_StringToEnum,            &string_to_enum);
    detour (Dvar_StringToValue,           &string_to_value);
    detour (Dvar_StringToColor,           &string_to_color);
    detour (Dvar_ValueToString,           &value_to_string);
    detour (Dvar_DisplayableValue,        &displayable_value);

    // Typed getters (by name)
    //
    detour (Dvar_GetBool,                 &get_bool);
    detour (Dvar_GetFloat,                &get_float);
    detour (Dvar_GetIntByName,            &get_int_by_name);
    detour (Dvar_GetString,               &get_string);
    detour (Dvar_GetVariantString,        &get_variant_string);

    // Typed setters
    //
    detour (Dvar_SetBool,                 &set_bool);
    detour (Dvar_SetBoolByName,           &set_bool_by_name);
    detour (Dvar_SetFloat,                &set_float);
    detour (Dvar_SetInt,                  &set_int);
    detour (Dvar_SetIntByName,            &set_int_by_name);
    detour (Dvar_SetString,               &set_string);
    detour (Dvar_SetStringByName,         &set_string_by_name);
    detour (Dvar_SetFromStringByName,     &set_from_string_by_name);
    detour (Dvar_SetFromStringFromSource, &set_from_string_from_source);
    detour (Dvar_SetCommand,              &set_command);
    detour (Dvar_SetVariant,              &set_variant);
    detour (Dvar_SetDomainFunc,           &set_domain_func);
  }
}
