#include <libiw4x/console/core.hxx>

#include <utility>

#include <libiw4x/console/completion.hxx>

using namespace std;

namespace iw4x::console
{
  namespace
  {
    // Join an invocation's arguments into a single value string.
    //
    // Note that dvar assignment is inherently value-oriented. For example, an
    // invocation like "set name a b c" feeds the raw "a b c" string down to the
    // dvar layer. It is then up to the dvar layer to parse this payload
    // according to the specific dvar's type (a vector dvar, for instance, will
    // naturally consume several components). We mirror this semantics here by
    // simply joining the argument tail with single spaces.
    //
    string
    join_value (const invocation& inv)
    {
      string out;

      for (size_t i (0); i < inv.arguments.size (); ++i)
      {
        if (i != 0)
          out.push_back (' ');

        out += inv.arguments[i].to_text ();
      }

      return out;
    }

    // Reconstruct a command line string for the engine from a parsed
    // invocation.
    //
    // It's important that we rebuild the command from the canonical,
    // slash-stripped name and the typed arguments rather than naively
    // forwarding the raw source text. A user-typed "/set cg_fov 20" must reach
    // the engine as "set cg_fov 20", because the engine's command lookup
    // mechanism rejects the leading slash.
    //
    // Furthermore, we must quote any arguments that are empty or contain
    // whitespace to allow the engine's tokenizer to treats them as a single
    // cohesive argument, just as the user intended.
    //
    string
    engine_command (const invocation& inv)
    {
      auto needs_quoting = [] (const string& s)
      {
        if (s.empty ())
          return true;

        for (char c : s)
        {
          if (ascii_is_space (c) || c == '"' || c == ';')
            return true;
        }

        return false;
      };

      string out (inv.name.str ());

      for (const argument_value& a : inv.arguments)
      {
        out.push_back (' ');

        const string text (a.to_text ());

        if (needs_quoting (text))
        {
          out.push_back ('"');
          out += text;
          out.push_back ('"');
        }
        else
          out += text;
      }

      return out;
    }

    // Translate a failed dvar assignment outcome into a user-facing diagnostic.
    //
    // We map the low-level domain, type, or access errors into actionable
    // diagnostics with clear messages and potential recovery hints.
    //
    void
    report_set_failure (const invocation& inv,
                        const dvar_set_outcome& outcome,
                        diagnostics& diags)
    {
      diagnostic d;
      d.severity = diagnostic_severity::error;
      d.category = diagnostic_category::dvar_binding;
      d.span = inv.span;
      d.detail = outcome.detail;

      switch (outcome.status)
      {
      case dvar_set_status::not_found:
        d.code = diagnostic_code::dvar_not_found;
        d.message = format ("variable '{}' does not exist",
                            inv.name.str ());
        break;

      case dvar_set_status::type_error:
        d.code = diagnostic_code::dvar_type_mismatch;
        d.message = format ("value is not valid for variable '{}'",
                            inv.name.str ());
        break;

      case dvar_set_status::domain_error:
        d.code = diagnostic_code::dvar_domain_violation;
        d.message = format ("value is out of range for variable '{}'",
                            inv.name.str ());
        break;

      case dvar_set_status::read_only:
        d.code = diagnostic_code::dvar_type_mismatch;
        d.message = format ("variable '{}' is read-only", inv.name.str ());
        d.recovery_hint = "this variable cannot be changed from the console";
        break;

      case dvar_set_status::ok:
        // This is not a failure. The caller should never invoke us in this
        // case, so we just bail out early.
        //
        return;
      }

      diags.add (move (d));
    }
  }

  dvar_eval_result
  evaluate_dvar (dvar_gateway& gateway, const invocation& inv)
  {
    dvar_eval_result result;

    const dvar_name name (inv.name.str ());

    // If there are no arguments, this is a bare name lookup, so we simply
    // report the current value. Note that the exists () precondition generally
    // guarantees that get () will return a valid optional. However, we stay
    // defensive here just in case the underlying state shifted.
    //
    if (inv.arguments.empty ())
    {
      optional<string> value (gateway.get (name));

      if (value.has_value ())
        result.output = format ("\"{}\" is \"{}\"", name.str (), *value);
      else
      {
        diagnostic d;
        d.severity = diagnostic_severity::error;
        d.category = diagnostic_category::dvar_binding;
        d.code = diagnostic_code::dvar_not_found;
        d.message = format ("variable '{}' does not exist", name.str ());
        d.span = inv.span;

        result.diags.add (move (d));
      }

      return result;
    }

    // Otherwise, it is an assignment. Proceed to set the value and report any
    // resulting failures back to the user.
    //
    dvar_set_outcome outcome (gateway.set (name, join_value (inv)));

    if (outcome.status != dvar_set_status::ok)
      report_set_failure (inv, outcome, result.diags);

    return result;
  }

  namespace
  {
    // Determine if a character represents a boundary between console words.
    //
    // This is used for cursor-context analysis during completion. We consider
    // both standard whitespace and the semicolon (the statement separator) as
    // natural word boundaries.
    //
    bool
    is_word_boundary (char c) noexcept
    {
      return ascii_is_space (c) || c == ';';
    }

    // Count the number of whole words within the [0, end) range of the line.
    //
    // This helper allows us to determine exactly which argument position the
    // user's cursor currently sits on, driving our context-aware completion.
    //
    size_t
    word_index (string_view line, size_t end) noexcept
    {
      size_t count (0);
      bool in_word (false);

      for (size_t i (0); i < end; ++i)
      {
        const bool boundary (is_word_boundary (line[i]));

        if (!boundary && !in_word)
        {
          ++count;
          in_word = true;
        }
        else if (boundary)
        {
          in_word = false;
        }
      }

      return count;
    }
  }

  console::
  console (dvar_gateway& gateway, output_sink out, diagnostic_sink diag)
    : gateway_ (&gateway),
      out_ (move (out)),
      diag_ (move (diag))
  {
  }

  void console::
  add_command (command_declaration decl, command_handler handler)
  {
    registration_result r (
      table_.with_command (move (decl), move (handler)));

    if (r.ok ())
    {
      table_ = move (r.table);
      return;
    }

    // If registration failed, we surface the underlying diagnostics to explain
    // why. Importantly, we do not adopt the returned (and unchanged) table.
    //
    if (diag_)
    {
      for (const diagnostic& d : r.diags.entries ())
        diag_ (d);
    }
  }

  void console::
  set_engine_executor (engine_executor exec)
  {
    engine_exec_ = move (exec);
  }

  void console::
  evaluate (string_view line)
  {
    parse_result pr (parse (line));

    auto emit_diag = [this] (const diagnostics& bag)
    {
      if (!diag_)
        return;

      for (const diagnostic& d : bag.entries ())
        diag_ (d);
    };

    // Always emit lexical and syntax diagnostics first, regardless of the
    // actual execution outcome.
    //
    emit_diag (pr.diags);

    for (const statement& s : pr.statements)
    {
      // If the statement is invalid, parse () has already produced a
      // diagnostic.
      //
      if (!holds_alternative<invocation> (s))
        continue;

      const invocation& inv (get<invocation> (s));

      // Check if this is a command we natively own.
      //
      if (table_.find (inv.name) != nullptr)
      {
        emit_diag (dispatch (table_, inv));
        continue;
      }

      // Check if this is a recognized dvar lookup or assignment.
      //
      if (gateway_->exists (dvar_name (inv.name.str ())))
      {
        dvar_eval_result r (evaluate_dvar (*gateway_, inv));

        if (r.output && out_)
          out_ (*r.output);

        emit_diag (r.diags);
        continue;
      }

      // Hand a reconstructed command to the engine as a fallback mechanism,
      // provided an executor is wired up. Rebuilding from the structured
      // invocation strips the leading slash and normalizes quoting so the
      // engine accepts it gracefully.
      //
      if (engine_exec_)
      {
        engine_exec_ (engine_command (inv));
        continue;
      }

      // Nobody owns this invocation. Yield a helpful diagnostic.
      //
      if (diag_)
      {
        diagnostic d;
        d.severity = diagnostic_severity::error;
        d.category = diagnostic_category::invocation;
        d.code = diagnostic_code::inv_unknown_command;
        d.message = format ("unknown command or variable '{}'",
                            inv.name.str ());
        d.span = inv.head.span;
        d.recovery_hint = "press Tab to see matching names";

        diag_ (d);
      }
    }
  }

  completion_result console::
  complete (string_view line, size_t cursor) const
  {
    if (cursor > line.size ())
      cursor = line.size ();

    // First, locate the start of the word that the cursor is currently inside,
    // or situated right at the end of.
    //
    size_t ws (cursor);

    while (ws > 0 && !is_word_boundary (line[ws - 1]))
      --ws;

    completion_request request;
    request.query = string (line.substr (ws, cursor - ws));
    request.replacement = source_span (static_cast<source_offset> (ws),
                                       static_cast<source_offset> (cursor));

    const size_t index (word_index (line, ws));

    // If we are on the first word, the completion context is broad. We want to
    // complete against command names, aliases, as well as dvar names.
    //
    if (index == 0)
    {
      completion_index idx (index_dvars (index_commands (table_), *gateway_));

      // Note that we must explicitly qualify the call to reach the
      // free-standing complete () ranking function. An unqualified name would
      // stubbornly resolve to this very member function.
      //
      return iw4x::console::complete (idx, request);
    }

    // If we are on a subsequent word, we must complete against the resolved
    // command's specific argument domain. We parse the line fragment to recover
    // the head name, and then resolve its parameter schema.
    //
    parse_result pr (parse (line));

    const invocation* inv (nullptr);

    for (const statement& s : pr.statements)
    {
      if (holds_alternative<invocation> (s))
      {
        inv = &get<invocation> (s);
        break;
      }
    }

    if (inv == nullptr)
      return {};

    const command_record* record (table_.find (inv->name));

    if (record == nullptr)
      return {}; // not a command we know and thus no typed argument domain

    const vector<parameter>& params (record->declaration.parameters);

    if (params.empty ())
      return {};

    const size_t pos (index - 1);
    const parameter& p (pos < params.size () ? params [pos] : params.back ());

    // If the parameter expects a dvar reference, restrict the completion
    // candidates strictly to known dvar names.
    //
    if (p.kind == parameter_kind::dvar_ref)
    {
      completion_index idx (index_dvars (completion_index {}, *gateway_));
      completion_request dreq (request);
      dreq.expected = completion_kind::dvar;

      return ::iw4x::console::complete (idx, dreq);
    }

    // Finally, if this is a closed value domain, complete against its explicit
    // members.
    //
    if (!p.known_values.empty ())
    {
      vector<completion_candidate> domain;
      domain.reserve (p.known_values.size ());

      for (const string& v : p.known_values)
      {
        completion_candidate cand;
        cand.text = v;
        cand.kind = completion_kind::argument;
        domain.push_back (move (cand));
      }

      return complete_from (domain, request);
    }

    return {};
  }
}
