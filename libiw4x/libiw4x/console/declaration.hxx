#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <libiw4x/console/types.hxx>

namespace iw4x::console
{
  // The expected type of a command parameter.
  //
  // Note that binding (in the command layer) will coerce each supplied
  // argument to the parameter's kind, or report a typed diagnostic when it
  // cannot. Here `word` is the permissive kind: any single token is accepted
  // verbatim, which we use for names and free-form text. Similarly, `dvar_ref`
  // is a word that additionally drives dvar-name completion.
  //
  enum class parameter_kind : std::uint8_t
  {
    word,
    integer,
    floating,
    boolean,
    dvar_ref,
  };

  // Return the string representation of the parameter kind.
  //
  std::string_view
  to_string (parameter_kind) noexcept;

  // A single parameter in a command signature.
  //
  // There are a few invariants that we enforce during validate_declaration().
  // Specifically, a variadic parameter, if present, must be the last one.
  // Furthermore, no required parameter can follow an optional one. Note also
  // that `known_values`, when non-empty, forms the closed set of accepted or
  // suggested values for this parameter and is what feeds our argument
  // completion machinery.
  //
  struct parameter
  {
    std::string              name;
    parameter_kind           kind {parameter_kind::word};
    bool                     required {true};
    bool                     variadic {false};
    std::string              description;
    std::vector<std::string> known_values;
  };

  // Behavioural flags on a command.
  //
  // This is a bitmask so a command can carry several flags at once. The
  // `raw_input` flag opts a command out of typed binding. This means its
  // handler receives the original argument text rather than coerced values.
  // We use this for commands that need to parse their own tail, such as "say".
  // The `cheat_protected` and `developer_only` flags mirror the engine's
  // notion of guarded commands. They are surfaced to the integration boundary,
  // which then ultimately decides enforcement.
  //
  enum class command_flags : std::uint32_t
  {
    none            = 0,
    raw_input       = 1u << 0,
    cheat_protected = 1u << 1,
    developer_only  = 1u << 2,
  };

  // Combine two command flags into a single bitmask.
  //
  constexpr command_flags
  operator| (command_flags a, command_flags b) noexcept
  {
    return static_cast<command_flags> (static_cast<std::uint32_t> (a) |
                                       static_cast<std::uint32_t> (b));
  }

  // Check if a specific flag (or set of flags) is present in the bitmask.
  //
  constexpr bool
  has_flag (command_flags set, command_flags flag) noexcept
  {
    return (static_cast<std::uint32_t> (set) &
            static_cast<std::uint32_t> (flag)) != 0;
  }

  // The declarative description of a single command.
  //
  // Here `name` is the canonical invocation name, while `aliases` provides
  // alternative names that all resolve back to this exact same command. We
  // use `description` to document the command for help text and completion
  // annotations. The `parameters` list forms the ordered signature.
  //
  struct command_declaration
  {
    command_name              name;
    std::string               description;
    std::vector<parameter>    parameters;
    command_flags             flags {command_flags::none};
    std::vector<command_name> aliases;

    // Count of leading required parameters.
    //
    // This is the number of parameters before the first optional or variadic
    // one. Note that this is dynamically derived from the signature and is
    // primarily useful for fast arity checks.
    //
    std::size_t
    required_count () const noexcept;

    // Return true if the last parameter is variadic.
    //
    // If true, this means the command can accept a trailing run of values
    // rather than being constrained to a strict number of arguments.
    //
    bool
    is_variadic () const noexcept;
  };

  // Validate a command declaration in isolation.
  //
  // This returns diagnostics describing every structural problem we found. If
  // the bag is empty, it means the declaration is perfectly well-formed. Note
  // that we only check signature ordering and alias/name sanity here. We
  // specifically do not check for collisions with other commands, which is the
  // registry's job since doing so requires access to the entire command table.
  //
  diagnostics
  validate_declaration (const command_declaration& decl);
}
