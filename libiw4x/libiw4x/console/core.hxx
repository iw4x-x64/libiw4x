#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <libiw4x/console/command.hxx>
#include <libiw4x/console/parser.hxx>
#include <libiw4x/console/types.hxx>

namespace iw4x::console
{
  // Forward declare completion_result.
  //
  // The completion.hxx header includes us for the dvar_gateway definition,
  // so we cannot include it back without creating a cyclic dependency. The
  // console coordinator only names completion_result as a by-value return
  // type, which means a forward declaration is sufficient. The full definition
  // is naturally included in core.cxx where it is actually instantiated.
  //
  struct completion_result;

  // Represent the outcome of a dvar assignment.
  //
  // We report one of these from the gateway so the core can raise the
  // appropriate diagnostic. Note that lookup failures are modeled separately,
  // typically by an absent value. The not_found enumerator means the name
  // resolved to no dvar. The type_error and domain_error enumerators mirror the
  // dvar module's own semantic distinctions. Finally, read_only covers write
  // attempts that the underlying engine refuses.
  //
  enum class dvar_set_status : std::uint8_t
  {
    ok,
    not_found,
    type_error,
    domain_error,
    read_only
  };

  // Capture the result of asking the gateway to set a dvar.
  //
  // It bundles the status of the operation with an optional diagnostic string
  // for further context in case of a failure.
  //
  struct dvar_set_outcome
  {
    dvar_set_status status {dvar_set_status::ok};
    std::optional<std::string> detail;
  };

  // Provide a read-only snapshot of a single dvar.
  //
  // We use this descriptor primarily for completion popups and lookup displays
  // where we just need a disconnected view of the variable's current state.
  //
  struct dvar_descriptor
  {
    dvar_name name;
    std::string current_value;
    std::string description;
  };

  // Provide the console's view of the dvar subsystem.
  //
  // This interface acts as the single seam between the console's semantics and
  // the dvar module. The engine-backed implementation (in the ABI layer)
  // forwards each call to iw4x::dvar, holding the appropriate locks and
  // respecting type and domain rules. It never duplicates the dvar state.
  // Alternatively, a test implementation can back these with a simple in-memory
  // map. Either way, the core depends only on this interface, so no engine
  // details leak.
  //
  class dvar_gateway
  {
  public:
    virtual
    ~dvar_gateway () = default;

    // Return true if a dvar with this specific name is registered.
    //
    virtual bool
    exists (const dvar_name& name) const = 0;

    // Retrieve the current value rendered as a string.
    //
    // If the requested dvar does not exist, this returns a nullopt.
    //
    virtual std::optional<std::string>
    get (const dvar_name& name) const = 0;

    // Assign a string value to a dvar.
    //
    // The value is parsed and validated by the underlying dvar layer, which
    // will communicate any semantic errors back via the outcome struct.
    //
    virtual dvar_set_outcome
    set (const dvar_name& name, std::string_view value) = 0;

    // Enumerate every available dvar for completion purposes.
    //
    // The callback is invoked exactly once per dvar. Implementations must
    // guarantee that they do not register or remove dvars during iteration, as
    // that would invalidate the traversal.
    //
    virtual void
    enumerate (const std::function<void (const dvar_descriptor&)>& fn) const = 0;
  };

  // Represent the outcome of evaluating an invocation as a dvar interaction.
  //
  // The output member, when present, contains the text the console should
  // actually print to the user (such as the formatted value of a dvar lookup).
  // The diags member carries any binding failures or parse errors.
  //
  struct dvar_eval_result
  {
    std::optional<std::string> output;
    diagnostics diags;

    // Return true if the evaluation completed without diagnostic errors.
    //
    bool
    ok () const noexcept
    {
      return !diags.has_errors ();
    }
  };

  // Evaluate an invocation whose head names a dvar.
  //
  // Note that callers must ensure gateway.exists(invocation name) is true. This
  // path is only reached after command resolution has definitively failed and
  // the name has been confirmed as a dvar. With no arguments, this performs a
  // lookup and fills the output with the formatted current value. With
  // arguments, it joins them and assigns the value, mapping any failure to a
  // dvar_binding diagnostic anchored at the invocation span.
  //
  dvar_eval_result
  evaluate_dvar (dvar_gateway& gateway, const invocation& inv);

  // Act as the console coordinator.
  //
  // This is the primary runtime object driven by the integration boundary. It
  // owns the command table, borrows a dvar gateway, and translates a raw
  // console line into discrete actions. Specifically, it parses the line, and
  // for each statement, dispatches a known command, falls back to a dvar
  // lookup/assignment, or hands the statement off to the engine. Output and
  // diagnostics flow out through caller-supplied sinks, so the coordinator
  // never logs or touches I/O itself.
  //
  // Note that the coordinator holds no engine state. Its only window to the
  // engine is the gateway (for dvars) and the optional engine executor (for
  // native commands it does not own).
  //
  class console
  {
  public:
    // Define the destination for text output, such as echoes or dvar lookup
    // results.
    //
    using output_sink = std::function<void (std::string_view)>;

    // Define the destination for diagnostics, which the boundary will usually
    // route to the application's logging facility.
    //
    using diagnostic_sink = std::function<void (const diagnostic&)>;

    // Define how to forward a statement the console does not own to the
    // engine's native command buffer.
    //
    // This is optional. If left unset, an unrecognised name is simply
    // reported as an unknown command instead of being forwarded.
    //
    using engine_executor = std::function<void (std::string_view)>;

    // Construct a console coordinator with the required dependencies.
    //
    explicit
    console (dvar_gateway& gateway,
             output_sink out,
             diagnostic_sink diag);

    // Register a console-owned command into the command table.
    //
    // If validation or collision failures occur, they are reported through
    // the diagnostic sink and the command is simply ignored.
    //
    void
    add_command (command_declaration decl, command_handler handler);

    // Set the fallback executor for engine-native commands.
    //
    void
    set_engine_executor (engine_executor exec);

    // Evaluate a full console line.
    //
    // This parses the line, then resolves and acts on each statement
    // sequentially. Any diagnostics and output generated during evaluation
    // are emitted through the corresponding sinks.
    //
    void
    evaluate (std::string_view line);

    // Produce completions for the token at the given cursor offset within the
    // line.
    //
    // The coordinator determines the completion context based on the line's
    // grammatical structure. For instance, the first token completes against
    // commands and dvars. A subsequent token completes against the resolved
    // command's argument domain (its known values, or, for a dvar reference,
    // dvar names).
    //
    completion_result
    complete (std::string_view line, std::size_t cursor) const;

    // Expose the internal command table for read-only inspection.
    //
    const command_table&
    commands () const noexcept
    {
      return table_;
    }

  private:
    command_table table_;
    dvar_gateway* gateway_;
    output_sink out_;
    diagnostic_sink diag_;
    engine_executor engine_exec_;
  };
}
