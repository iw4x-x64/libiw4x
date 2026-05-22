#include <libiw4x/console/command.hxx>

#include <cassert>
#include <charconv>
#include <utility>

#include <libiw4x/console/utility.hxx>

using namespace std;

namespace iw4x::console
{
  namespace
  {
    // Fold the command name into a normalized, lower-case string.
    //
    // We use this string as the primary key in our command lookup table. Note
    // that we do an in-place mutation to avoid extra allocations during the
    // loop.
    //
    string
    fold (const command_name& n)
    {
      string key (n.str ());

      for (char& c : key)
        c = ascii_to_lower (c);

      return key;
    }

    // Attempt to interpret a raw string as a boolean value.
    //
    // We recognize the traditional engine vocabulary here so that existing
    // configuration files and old console muscle memory (like "1/true/yes/on")
    // still work correctly. Note that everything is handled case-insensitively.
    //
    bool
    parse_boolean (string_view s, bool& out) noexcept
    {
      string lc (s);
      for (char& c : lc)
        c = ascii_to_lower (c);

      if (lc == "1" || lc == "true" || lc == "yes" || lc == "on")
      {
        out = true;
        return true;
      }

      if (lc == "0" || lc == "false" || lc == "no" || lc == "off")
      {
        out = false;
        return true;
      }

      return false;
    }

    // Coerce a supplied argument string into the specific type required by the
    // parameter declaration.
    //
    // If the argument cannot be converted to the required type, we record an
    // error diagnostic and return an empty optional.
    //
    optional<argument_value>
    coerce (const parameter& p,
            const argument_value& a,
            size_t position,
            diagnostics& diags)
    {
      // Helper to emit a formatted type error.
      //
      auto type_error = [&] (string detail)
      {
        diagnostic d;
        d.severity = diagnostic_severity::error;
        d.category = diagnostic_category::invocation;
        d.code = diagnostic_code::inv_argument_type;
        d.message = format ("argument {} ('{}') expects {}",
                            position + 1,
                            p.name,
                            to_string (p.kind));
        d.detail = move (detail);
        d.span = a.span ();
        diags.add (move (d));
      };

      // If the parameter specifies a closed domain of values, we treat this as
      // a membership test against the original text rather than a pure type
      // coercion.
      //
      // Note that this happens regardless of the underlying type kind.
      //
      if (!p.known_values.empty ())
      {
        const string text (a.to_text ());
        bool member (false);

        for (const string& v : p.known_values)
        {
          if (v.size () != text.size ())
            continue;

          bool eq (true);
          for (size_t i (0); i < v.size (); ++i)
          {
            if (ascii_to_lower (v [i]) != ascii_to_lower (text [i]))
            {
              eq = false;
              break;
            }
          }

          if (eq)
          {
            member = true;
            break;
          }
        }

        if (!member)
        {
          type_error (format ("'{}' is not one of the accepted values", text));
          return nullopt;
        }
      }

      switch (p.kind)
      {
        case parameter_kind::word:
        case parameter_kind::dvar_ref:
          // For words and dvar references, any single token is structurally
          // valid. We simply carry the text through as a string value.
          //
          return argument_value::of_string (a.to_text (), a.span ());

        case parameter_kind::integer:
        {
          if (a.kind () == value_kind::integer)
            return a;

          const string text (a.to_text ());
          int64_t v {};
          auto [ptr, ec] (from_chars (text.data (), text.data () + text.size (), v));

          if (ec == errc {} && ptr == text.data () + text.size ())
            return argument_value::of_integer (v, a.span ());

          type_error (format ("'{}' is not an integer", text));
          return nullopt;
        }

        case parameter_kind::floating:
        {
          if (a.kind () == value_kind::floating)
            return a;

          // Implicitly promote integer arguments to floats if the parameter
          // demands a floating-point value.
          //
          if (a.kind () == value_kind::integer)
            return argument_value::of_floating (static_cast<double> (a.as_integer ()), a.span ());

          const string text (a.to_text ());
          double v {};
          auto [ptr, ec] (from_chars (text.data (), text.data () + text.size (), v));

          if (ec == errc {} && ptr == text.data () + text.size ())
            return argument_value::of_floating (v, a.span ());

          type_error (format ("'{}' is not a number", text));
          return nullopt;
        }

        case parameter_kind::boolean:
        {
          bool v {};
          if (parse_boolean (a.to_text (), v))
            return argument_value::of_boolean (v, a.span ());

          type_error (format ("'{}' is not a boolean", a.to_text ()));
          return nullopt;
        }
      }

      CONSOLE_UNREACHABLE ();
    }

    // Reconstruct the raw invocation tail by joining the original argument
    // texts with a single space.
    //
    // This is generally used for 'raw' commands (like 'say' or 'rcon') where
    // the engine expects the remainder of the line undisturbed.
    //
    string
    join_raw (const invocation& inv)
    {
      string out;

      for (size_t i (0); i < inv.arguments.size (); ++i)
      {
        if (i != 0)
          out.push_back (' ');

        out += inv.arguments [i].to_text ();
      }

      return out;
    }
  }

  const argument_value& invocation_context::
  arg (size_t i) const noexcept
  {
    assert (i < arguments_.size ());
    return arguments_ [i];
  }

  int64_t invocation_context::
  integer (size_t i) const noexcept
  {
    return arg (i).as_integer ();
  }

  double invocation_context::
  floating (size_t i) const noexcept
  {
    return arg (i).as_floating ();
  }

  bool invocation_context::
  boolean (size_t i) const noexcept
  {
    return arg (i).as_boolean ();
  }

  string_view invocation_context::
  text (size_t i) const noexcept
  {
    return arg (i).as_string ();
  }

  const command_record* command_table::
  find (const command_name& n) const
  {
    const uint32_t* id (name_index_.find (fold (n)));

    if (id == nullptr || *id == 0)
      return nullptr;

    assert (*id <= records_.size ());
    return &records_ [*id - 1];
  }

  registration_result command_table::
  with_command (command_declaration decl, command_handler handler) const
  {
    registration_result r {*this, {}};

    r.diags.merge (validate_declaration (decl));

    if (r.diags.has_errors ())
      return r;

    // Check for collisions.
    //
    // Both the canonical command name and every alias must be available in the
    // current index. That is, we do not allow shadowing.
    //
    auto collides = [this] (const command_name& n)
    {
      return name_index_.find (fold (n)) != nullptr;
    };

    auto reject = [&r] (const string& subject)
    {
      diagnostic d;
      d.severity = diagnostic_severity::error;
      d.category = diagnostic_category::declaration;
      d.code = diagnostic_code::decl_duplicate_name;
      d.message = format ("'{}' is already registered", subject);
      r.diags.add (move (d));
    };

    if (collides (decl.name))
      reject (decl.name.str ());

    for (const command_name& a : decl.aliases)
    {
      if (collides (a))
        reject (a.str ());
    }

    if (r.diags.has_errors ())
      return r;

    // Construct the newly registered record and the resulting command table.
    //
    // Keep in mind that our IDs are 1-based, whereas the records_ collection
    // naturally stores them at index (id - 1).
    //
    const uint32_t id (static_cast<uint32_t> (records_.size ()) + 1);

    command_record record;
    record.id = static_cast<command_id> (id);
    record.declaration = move (decl);
    record.handler = move (handler);

    command_table next;
    next.records_ = records_.push_back (record);
    next.name_index_ = name_index_.set (fold (record.declaration.name), id);

    for (const command_name& a : record.declaration.aliases)
      next.name_index_ = next.name_index_.set (fold (a), id);

    r.table = move (next);
    return r;
  }

  bind_result
  bind (const command_record& record, const invocation& inv)
  {
    bind_result r;
    const command_declaration& decl (record.declaration);

    // If the command requests raw input, we bypass the typed binding.
    //
    // The handler just wants the rest of the line exactly as it was typed by
    // the user, so we pack it all together.
    //
    if (has_flag (decl.flags, command_flags::raw_input))
    {
      r.context.emplace (decl,
                         inv.name,
                         vector<argument_value> {},
                         join_raw (inv));
      return r;
    }

    const size_t supplied (inv.arguments.size ());
    const size_t required (decl.required_count ());
    const size_t declared (decl.parameters.size ());
    const bool   variadic (decl.is_variadic ());

    auto arity_error = [&] (string detail)
    {
      diagnostic d;
      d.severity = diagnostic_severity::error;
      d.category = diagnostic_category::invocation;
      d.code = diagnostic_code::inv_arity_mismatch;
      d.message =
        format ("command '{}' called with the wrong number of arguments",
                decl.name.str ());
      d.detail = move (detail);
      d.span = inv.span;
      r.diags.add (move (d));
    };

    if (supplied < required)
      arity_error (format ("expected at least {}, got {}", required, supplied));
    else if (!variadic && supplied > declared)
      arity_error (format ("expected at most {}, got {}", declared, supplied));

    if (r.diags.has_errors ())
      return r;

    // Iterate through and coerce each supplied argument against its governing
    // parameter.
    //
    // For arguments that fall past the declared parameter list, we govern them
    // by the final variadic tail. Note that we only reach this state if the
    // command is actually variadic, thanks to our arity checks above.
    //
    vector<argument_value> bound;
    bound.reserve (supplied);

    for (size_t i (0); i < supplied; ++i)
    {
      const parameter& p (i < declared ? decl.parameters [i]
                                       : decl.parameters.back ());

      optional<argument_value> v (coerce (p, inv.arguments [i], i, r.diags));

      if (v.has_value ())
        bound.push_back (move (*v));
    }

    if (r.diags.has_errors ())
      return r;

    assert (bound.size () == supplied);
    r.context.emplace (decl, inv.name, move (bound), join_raw (inv));
    return r;
  }

  // Dispatch an invocation against a command table.
  //
  // We perform the lookup, execute the binding process, and if successful,
  // invoke the underlying handler. Any structural or type errors along the way
  // are accumulated into the returned diagnostics.
  //
  diagnostics
  dispatch (const command_table& table, const invocation& inv)
  {
    const command_record* record (table.find (inv.name));

    if (record == nullptr)
    {
      diagnostics diags;
      diagnostic d;
      d.severity = diagnostic_severity::error;
      d.category = diagnostic_category::invocation;
      d.code = diagnostic_code::inv_unknown_command;
      d.message = format ("unknown command '{}'", inv.name.str ());
      d.span = inv.head.span;
      d.recovery_hint = "press Tab to see matching commands";
      diags.add (move (d));
      return diags;
    }

    bind_result bound (bind (*record, inv));

    if (!bound.ok ())
      return move (bound.diags);

    // At this point, the handler is guaranteed to run with a completely
    // validated and typed context.
    //
    // Note that a registered command without a bound handler is considered a
    // fatal programmer error, not a runtime user error.
    //
    assert (record->handler && "registered command has no handler");
    record->handler (*bound.context);

    return move (bound.diags);
  }
}
