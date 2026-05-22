#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <immer/map.hpp>
#include <immer/vector.hpp>

#include <libiw4x/console/declaration.hxx>
#include <libiw4x/console/parser.hxx>
#include <libiw4x/console/types.hxx>

namespace iw4x::console
{
  class invocation_context;

  // Command handler implementation.
  //
  // The handler runs only after the arguments have been fully bound and
  // validated. It may perform engine-facing work (typically through the ABI
  // layer). Note that the binding contract guarantees the context already
  // satisfies the declared signature, so we don't have to double-check types.
  //
  using command_handler = std::function<void (const invocation_context&)>;

  // Single registered command.
  //
  struct command_record
  {
    command_id          id {command_id::none};
    command_declaration declaration;
    command_handler     handler;
  };

  // Bound and validated arguments handed to a command handler.
  //
  // For typed commands, arguments () holds one coerced value per supplied
  // argument. Each matches its governing parameter's kind, with the original
  // spans preserved. Note that for raw_input commands, raw () yields the
  // original argument tail as a single string and arguments () is left empty.
  // The declaration and invoked name are also kept around for handlers that
  // need to introspect their own signature.
  //
  class invocation_context
  {
  public:
    explicit
    invocation_context (const command_declaration& decl,
                        command_name invoked,
                        std::vector<argument_value> bound,
                        std::string raw)
      : declaration_ (&decl),
        invoked_ (std::move (invoked)),
        arguments_ (std::move (bound)),
        raw_ (std::move (raw)) {}

    const command_declaration&
    declaration () const noexcept
    {
      return *declaration_;
    }

    const command_name&
    name () const noexcept
    {
      return invoked_;
    }

    std::size_t
    arity () const noexcept
    {
      return arguments_.size ();
    }

    const std::vector<argument_value>&
    arguments () const noexcept
    {
      return arguments_;
    }

    const argument_value&
    arg (std::size_t i) const noexcept;

    // Typed convenience accessors.
    //
    // Each asserts the bound argument's kind. Note that binding has already
    // guaranteed this, so these are just programmer-facing shortcuts, not
    // an extra layer of validation.
    //
    std::int64_t
    integer (std::size_t i) const noexcept;

    double
    floating (std::size_t i) const noexcept;

    bool
    boolean (std::size_t i) const noexcept;

    std::string_view
    text (std::size_t i) const noexcept;

    // Original argument tail, joined by spaces.
    //
    // This is always available, but it is primarily used as the main accessor
    // for raw_input commands.
    //
    const std::string&
    raw () const noexcept
    {
      return raw_;
    }

  private:
    const command_declaration* declaration_;
    command_name invoked_;
    std::vector<argument_value> arguments_;
    std::string raw_;
  };

  // Result of registering a command into a table.
  //
  struct registration_result;

  // Immutable table of commands.
  //
  // Note that lookups are case-insensitive and resolve both canonical names
  // and aliases. The table never mutates in place. Instead, with_command
  // returns a completely new value.
  //
  class command_table
  {
  public:
    command_table () = default;

    // Register declaration and handler.
    //
    // This validates the declaration and checks for name and alias collisions
    // against the current table. Returns a new table on success.
    //
    registration_result
    with_command (command_declaration decl, command_handler handler) const;

    // Resolve a name (canonical or alias) to its record.
    //
    // Returns a null pointer if the command is unknown. Note that the returned
    // pointer remains valid for the entire lifetime of this table value.
    //
    const command_record*
    find (const command_name& n) const;

    std::size_t
    size () const noexcept
    {
      return records_.size ();
    }

    // Iterate every registered record.
    //
    // Note that this only includes canonical entries, not aliases. We mainly
    // use this for completion to enumerate command names.
    //
    const immer::vector<command_record>&
    records () const noexcept
    {
      return records_;
    }

  private:
    // Map case-folded name (canonical or alias) to a 1-based command id.
    //
    // The records_ vector stores these records at index (id - 1). Note that a
    // 0 id is never stored, as it represents a non-existent command.
    //
    immer::map<std::string, std::uint32_t> name_index_;
    immer::vector<command_record> records_;
  };

  // Result of registering a command into a table.
  //
  // On success, diags is empty and table is the new, larger table. On
  // failure, diags explains the reason (for example, an invalid declaration,
  // or a name/alias that collides with an existing entry) and the table is
  // returned completely unchanged.
  //
  struct registration_result
  {
    command_table table;
    diagnostics diags;

    bool
    ok () const noexcept
    {
      return !diags.has_errors ();
    }
  };

  // Outcome of binding an invocation against a command's signature.
  //
  struct bind_result
  {
    std::optional<invocation_context> context;
    diagnostics diags;

    bool
    ok () const noexcept
    {
      return context.has_value () && !diags.has_errors ();
    }
  };

  // Bind a parsed invocation against a resolved command record.
  //
  // This validates arity and coerces each argument to its parameter kind. The
  // exception is if the command is raw_input, in which case the text is just
  // passed through as is. It produces a ready invocation_context on success,
  // or diagnostics pinned to the offending argument spans on failure.
  //
  bind_result
  bind (const command_record& record, const invocation& inv);

  // Resolve, bind, and dispatch an invocation against a table.
  //
  // Returns diagnostics describing any failure (such as an unknown command,
  // arity mismatch, or type error). On success, the bag is error-free and
  // the handler has actually run. Note that this is the command layer's
  // top-level entry point; the core layer simply calls it after parsing.
  //
  diagnostics
  dispatch (const command_table& table, const invocation& inv);
}
