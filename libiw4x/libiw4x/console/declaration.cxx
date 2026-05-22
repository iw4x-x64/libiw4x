#include <libiw4x/console/declaration.hxx>

#include <utility>

using namespace std;

namespace iw4x::console
{
  string_view
  to_string (parameter_kind k) noexcept
  {
    switch (k)
    {
      case parameter_kind::word:     return "word";
      case parameter_kind::integer:  return "integer";
      case parameter_kind::floating: return "floating";
      case parameter_kind::boolean:  return "boolean";
      case parameter_kind::dvar_ref: return "dvar";
    }

    CONSOLE_UNREACHABLE ();
  }

  size_t
  command_declaration::required_count () const noexcept
  {
    size_t n (0);

    for (const parameter& p : parameters)
    {
      if (!p.required || p.variadic)
        break;

      ++n;
    }

    return n;
  }

  bool command_declaration::
  is_variadic () const noexcept
  {
    return !parameters.empty () && parameters.back ().variadic;
  }

  diagnostics
  validate_declaration (const command_declaration& decl)
  {
    diagnostics diags;

    auto fail = [&diags, &decl] (diagnostic_code code, string message)
    {
      diagnostic d;
      d.severity = diagnostic_severity::error;
      d.category = diagnostic_category::declaration;
      d.code = code;
      d.message = move (message);
      d.detail = format ("in declaration of command '{}'",
                             decl.name.str ());
      diags.add (move (d));
    };

    // The command must be named.
    //
    if (decl.name.empty ())
      fail (diagnostic_code::decl_bad_signature, "command has no name");

    // Signature ordering. We walk the parameters tracking two things: whether
    // we have already seen an optional parameter (after which no required one
    // may appear) and whether we have already seen the variadic tail (after
    // which nothing may appear). These mirror the rules the old console
    // enforced, captured here as validation rather than scattered checks.
    //
    bool seen_optional (false);
    bool seen_variadic (false);

    for (size_t i (0); i < decl.parameters.size (); ++i)
    {
      const parameter& p (decl.parameters[i]);

      if (seen_variadic)
      {
        fail (diagnostic_code::decl_bad_signature,
              format ("parameter '{}' follows a variadic parameter. that is "
                      "the variadic parameter must be last", p.name));
        break;
      }

      if (p.required && seen_optional)
      {
        fail (diagnostic_code::decl_bad_signature,
              format ("required parameter '{}' follows an optional one. that "
                      "is the required parameters must come first", p.name));
        break;
      }

      if (!p.required)
        seen_optional = true;

      if (p.variadic)
      {
        seen_variadic = true;

        // A variadic parameter is conceptually open-ended, so it cannot also be
        // "optional" in the arity sense. We treat a variadic as satisfying its
        // own minimum via required/optional independently. No extra check
        // needed here beyond ordering.
        //
      }
    }

    // Aliases must be non-empty and must not duplicate the canonical name.
    //
    for (const command_name& a : decl.aliases)
    {
      if (a.empty ())
      {
        fail (diagnostic_code::decl_invalid_alias, "command has an empty alias");
      }
      else if (a == decl.name)
      {
      fail (diagnostic_code::decl_invalid_alias,
            format ("alias '{}' duplicates the command name", a.str ()));
      }
    }

    return diags;
  }
}
