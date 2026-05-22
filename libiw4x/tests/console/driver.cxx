#include <cmath>
#include <map>
#include <string>

#undef NDEBUG
#include <cassert>

#include <libiw4x/console/command.hxx>
#include <libiw4x/console/completion.hxx>
#include <libiw4x/console/core.hxx>
#include <libiw4x/console/declaration.hxx>
#include <libiw4x/console/input.hxx>
#include <libiw4x/console/lexer.hxx>
#include <libiw4x/console/render.hxx>
#include <libiw4x/console/parser.hxx>
#include <libiw4x/console/types.hxx>
#include <libiw4x/console/utility.hxx>

using namespace std;
using namespace iw4x::console;

namespace
{
  void
  test_ascii_classifiers ()
  {
    assert (ascii_to_lower ('A') == 'a');
    assert (ascii_to_lower ('z') == 'z');
    assert (ascii_to_lower ('5') == '5');

    assert (ascii_is_space (' '));
    assert (ascii_is_space ('\t'));
    assert (!ascii_is_space ('x'));

    assert (ascii_is_identifier_start ('_'));
    assert (ascii_is_identifier_start ('q'));
    assert (!ascii_is_identifier_start ('9'));

    assert (ascii_is_identifier_continue ('9'));
    assert (ascii_is_identifier_continue ('.'));
    assert (ascii_is_identifier_continue ('-'));
    assert (!ascii_is_identifier_continue (' '));
  }

  void
  test_source_span ()
  {
    source_span a (2, 6);
    assert (a.length () == 4);
    assert (!a.empty ());
    assert (a.contains (2));
    assert (a.contains (5));
    assert (!a.contains (6)); // half-open: end is exclusive.

    source_span b (4, 9);
    source_span m (a.merge (b));
    assert (m.begin == 2 && m.end == 9);

    source_span z (3, 3);
    assert (z.empty ());

    assert ((source_span (1, 2) == source_span (1, 2)));
  }

  void
  test_line_column ()
  {
    const string src ("set\nname\nvalue");

    line_column at0 (resolve_line_column (src, 0));
    assert (at0.line == 1 && at0.column == 1);

    line_column at4 (resolve_line_column (src, 4));
    assert (at4.line == 2 && at4.column == 1);

    line_column past (resolve_line_column (src, 9999));
    assert (past.line == 3);
  }

  void
  test_names ()
  {
    command_name a ("OpenConsole");
    command_name b ("openconsole");
    command_name c ("opnconsole");

    assert (a == b);
    assert (!(a == c));
    assert (a.str () == "OpenConsole");
    assert (a.hash () == b.hash ());

    dvar_name d ("com_maxFps");
    assert (!d.empty ());
    assert (dvar_name ("").empty ());
  }

  void
  test_argument_value ()
  {
    argument_value s (argument_value::of_string ("hello", source_span (0, 5)));
    assert (s.kind () == value_kind::string);
    assert (s.as_string () == "hello");
    assert (s.to_text () == "hello");
    assert (s.span () == source_span (0, 5));

    argument_value i (argument_value::of_integer (42, source_span (0, 2)));
    assert (i.kind () == value_kind::integer);
    assert (i.as_integer () == 42);
    assert (i.to_text () == "42");

    argument_value bt (argument_value::of_boolean (true, source_span (0, 4)));
    assert (bt.as_boolean ());
    assert (bt.to_text () == "1");

    argument_value bf (argument_value::of_boolean (false, source_span (0, 5)));
    assert (bf.to_text () == "0");
  }

  void
  test_diagnostics ()
  {
    assert (code_string (diagnostic_code::lex_invalid_character) == "CON-1001");
    assert (code_string (diagnostic_code::none) == "CON-0000");

    diagnostics bag;
    assert (bag.empty ());
    assert (!bag.has_errors ());

    diagnostic warn;
    warn.severity = diagnostic_severity::warning;
    warn.category = diagnostic_category::lexical;
    warn.code = diagnostic_code::lex_invalid_escape;
    warn.message = "unknown escape sequence";
    bag.add (move (warn));

    assert (bag.size () == 1);
    assert (!bag.has_errors ());

    diagnostic err;
    err.severity = diagnostic_severity::error;
    err.category = diagnostic_category::syntax;
    err.code = diagnostic_code::syn_unexpected_token;
    err.message = "unexpected token";
    err.span = source_span (3, 4);
    assert (err.summary () == "error[CON-2001] syntax: unexpected token");

    bag.add (move (err));
    assert (bag.has_errors ());
    assert (bag.size () == 2);

    diagnostics other;
    diagnostic fatal;
    fatal.severity = diagnostic_severity::fatal;
    fatal.category = diagnostic_category::internal;
    fatal.code = diagnostic_code::internal_invariant;
    fatal.message = "boom";
    other.add (move (fatal));

    bag.merge (move (other));
    assert (bag.size () == 3);
    assert (bag.has_errors ());
  }

  void
  test_lex_empty ()
  {
    lex_result r (lex (""));
    assert (r.ok ());
    assert (r.tokens.size () == 1);
    assert (r.tokens[0].kind == token_kind::end_of_input);
    assert (r.tokens[0].span == source_span (0, 0));

    lex_result w (lex ("   \t  "));
    assert (w.ok ());
    assert (w.tokens.size () == 1);
    assert (w.tokens[0].kind == token_kind::end_of_input);
  }

  void
  test_lex_words_and_spans ()
  {
    lex_result r (lex ("set name value"));
    assert (r.ok ());

    assert (r.tokens.size () == 4);
    assert (r.tokens[0].kind == token_kind::identifier);
    assert (r.tokens[0].lexeme == "set");
    assert (r.tokens[0].value == "set");
    assert (r.tokens[0].span == source_span (0, 3));

    assert (r.tokens[1].lexeme == "name");
    assert (r.tokens[1].span == source_span (4, 8));

    assert (r.tokens[2].lexeme == "value");
    assert (r.tokens[2].span == source_span (9, 14));

    assert (r.tokens[3].kind == token_kind::end_of_input);
    assert (r.tokens[3].span == source_span (14, 14));
  }

  void
  test_lex_numbers ()
  {
    assert (is_number_lexeme ("42"));
    assert (is_number_lexeme ("-3.14"));
    assert (is_number_lexeme ("+5"));
    assert (is_number_lexeme ("1e5"));
    assert (is_number_lexeme ("2.5E-3"));
    assert (is_number_lexeme (".5"));

    assert (!is_number_lexeme ("12fps"));
    assert (!is_number_lexeme ("com_maxfps"));
    assert (!is_number_lexeme ("1e"));
    assert (!is_number_lexeme ("-"));
    assert (!is_number_lexeme (""));

    lex_result r (lex ("set com_maxfps 125"));
    assert (r.tokens[0].kind == token_kind::identifier); // set
    assert (r.tokens[1].kind == token_kind::identifier); // com_maxfps
    assert (r.tokens[2].kind == token_kind::number);     // 125
    assert (r.tokens[2].value == "125");
  }

  void
  test_lex_strings ()
  {
    lex_result r (lex ("\"a\\nb\\t\\\"c\""));
    assert (r.ok ());
    assert (r.tokens.size () == 2);
    assert (r.tokens[0].kind == token_kind::string);
    assert (r.tokens[0].value == "a\nb\t\"c");
    assert (r.tokens[0].lexeme == "\"a\\nb\\t\\\"c\"");

    lex_result s (lex ("'hello world'"));
    assert (s.ok ());
    assert (s.tokens[0].kind == token_kind::string);
    assert (s.tokens[0].value == "hello world");
  }

  void
  test_lex_unterminated_string ()
  {
    lex_result r (lex ("\"oops"));
    assert (!r.ok ());
    assert (r.tokens[0].kind == token_kind::unterminated_string);
    assert (r.tokens[0].value == "oops");
    assert (r.diags.has_errors ());
    assert (r.diags.entries ().front ().code ==
            diagnostic_code::lex_unterminated_string);
  }

  void
  test_lex_invalid_character ()
  {
    string src ("a\x01""b");
    lex_result r (lex (src));
    assert (!r.ok ());

    assert (r.tokens.size () == 4);
    assert (r.tokens[0].kind == token_kind::identifier);
    assert (r.tokens[1].kind == token_kind::invalid);
    assert (r.tokens[1].span == source_span (1, 2));
    assert (r.tokens[2].kind == token_kind::identifier);
    assert (r.diags.entries ().front ().code ==
            diagnostic_code::lex_invalid_character);
  }

  void
  test_lex_unknown_escape_recovers ()
  {
    lex_result r (lex ("\"a\\qb\""));
    assert (r.ok ());
    assert (!r.diags.empty ());
    assert (r.diags.entries ().front ().severity ==
            diagnostic_severity::warning);
    assert (r.tokens[0].value == "aqb");
  }

  void
  test_lex_separator ()
  {
    lex_result r (lex ("a;b"));
    assert (r.ok ());
    assert (r.tokens.size () == 4);
    assert (r.tokens[0].kind == token_kind::identifier);
    assert (r.tokens[1].kind == token_kind::separator);
    assert (r.tokens[1].lexeme == ";");
    assert (r.tokens[2].kind == token_kind::identifier);
  }

  const invocation&
  expect_invocation (const statement& s)
  {
    assert (holds_alternative<invocation> (s));
    return get<invocation> (s);
  }

  void
  test_parse_simple_invocation ()
  {
    parse_result r (parse ("map mp_rust"));
    assert (r.ok ());
    assert (r.statements.size () == 1);

    const invocation& inv (expect_invocation (r.statements[0]));
    assert (inv.name == command_name ("map"));
    assert (inv.arguments.size () == 1);
    assert (inv.arguments[0].kind () == value_kind::string);
    assert (inv.arguments[0].as_string () == "mp_rust");
    assert (inv.span == source_span (0, 11));
  }

  void
  test_parse_typed_arguments ()
  {
    parse_result r (parse ("set com_maxfps 125 0.5 -7"));
    assert (r.ok ());

    const invocation& inv (expect_invocation (r.statements[0]));
    assert (inv.name == command_name ("set"));
    assert (inv.arguments.size () == 4);

    assert (inv.arguments[0].kind () == value_kind::string);  // com_maxfps
    assert (inv.arguments[0].as_string () == "com_maxfps");

    assert (inv.arguments[1].kind () == value_kind::integer); // 125
    assert (inv.arguments[1].as_integer () == 125);

    assert (inv.arguments[2].kind () == value_kind::floating); // 0.5
    assert (inv.arguments[2].as_floating () == 0.5);

    assert (inv.arguments[3].kind () == value_kind::integer); // -7
    assert (inv.arguments[3].as_integer () == -7);
  }

  void
  test_parse_slash_prefix ()
  {
    parse_result r (parse ("/quit"));
    const invocation& inv (expect_invocation (r.statements[0]));
    assert (inv.name == command_name ("quit"));
    assert (inv.head.lexeme == "/quit"); // Original preserved.

    parse_result b (parse ("\\reconnect"));
    assert (expect_invocation (b.statements[0]).name ==
            command_name ("reconnect"));
  }

  void
  test_parse_statement_separation ()
  {
    parse_result r (parse ("vid_restart ; map mp_crash"));
    assert (r.ok ());
    assert (r.statements.size () == 2);
    assert (expect_invocation (r.statements[0]).name ==
            command_name ("vid_restart"));
    assert (expect_invocation (r.statements[1]).name ==
            command_name ("map"));
  }

  void
  test_parse_empty_statement_warns ()
  {
    parse_result r (parse ("a ;; b"));
    assert (r.ok ());
    assert (r.statements.size () == 2);

    bool saw_empty (false);
    for (const diagnostic& d : r.diags.entries ())
      if (d.code == diagnostic_code::syn_empty_input)
        saw_empty = true;
    assert (saw_empty);
  }

  void
  test_parse_bad_head ()
  {
    parse_result r (parse ("42 foo ; map x"));
    assert (!r.ok ());
    assert (r.statements.size () == 2);
    assert (holds_alternative<invalid_statement> (r.statements[0]));
    assert (expect_invocation (r.statements[1]).name == command_name ("map"));

    bool saw_unexpected (false);
    for (const diagnostic& d : r.diags.entries ())
      if (d.code == diagnostic_code::syn_unexpected_token)
        saw_unexpected = true;
    assert (saw_unexpected);
  }

  void
  test_parse_empty_input ()
  {
    parse_result r (parse (""));
    assert (r.ok ());
    assert (r.statements.empty ());
  }

  void
  test_parse_merges_lexical_diagnostics ()
  {
    parse_result r (parse ("say \"hello"));
    assert (!r.ok ());

    bool saw_lex (false);
    for (const diagnostic& d : r.diags.entries ())
      if (d.code == diagnostic_code::lex_unterminated_string)
        saw_lex = true;
    assert (saw_lex);
  }

  parameter
  param (string name, parameter_kind k, bool required, bool variadic = false)
  {
    parameter p;
    p.name = move (name);
    p.kind = k;
    p.required = required;
    p.variadic = variadic;
    return p;
  }

  void
  test_declaration_validation ()
  {
    command_declaration ok;
    ok.name = command_name ("bind");
    ok.parameters = {param ("key", parameter_kind::word, true),
                    param ("action", parameter_kind::word, false)};
    assert (!validate_declaration (ok).has_errors ());

    command_declaration ro;
    ro.name = command_name ("bad");
    ro.parameters = {param ("a", parameter_kind::word, false),
                    param ("b", parameter_kind::word, true)};
    assert (validate_declaration (ro).has_errors ());

    command_declaration vmid;
    vmid.name = command_name ("bad2");
    vmid.parameters = {param ("rest", parameter_kind::word, true, true),
                      param ("tail", parameter_kind::word, true)};
    assert (validate_declaration (vmid).has_errors ());

    command_declaration dup;
    dup.name = command_name ("quit");
    dup.aliases = {command_name ("quit")};
    assert (validate_declaration (dup).has_errors ());

    assert (ok.required_count () == 1);

    command_declaration vok;
    vok.name = command_name ("echo");
    vok.parameters = {param ("words", parameter_kind::word, true, true)};
    assert (!validate_declaration (vok).has_errors ());
    assert (vok.is_variadic ());
  }

  command_declaration
  decl (string name, vector<parameter> params,
        command_flags flags = command_flags::none)
  {
    command_declaration d;
    d.name = command_name (move (name));
    d.parameters = move (params);
    d.flags = flags;
    return d;
  }

  invocation
  only_invocation (string_view src)
  {
    parse_result r (parse (src));
    assert (r.statements.size () == 1);
    assert (holds_alternative<invocation> (r.statements[0]));
    return get<invocation> (r.statements[0]);
  }

  void
  test_command_table_registration ()
  {
    command_table empty;
    assert (empty.size () == 0);

    command_declaration q (decl ("quit", {}));
    q.aliases = {command_name ("exit")};

    registration_result r (empty.with_command (q, [] (const invocation_context&) {}));
    assert (r.ok ());
    assert (r.table.size () == 1);

    assert (r.table.find (command_name ("quit")) != nullptr);
    assert (r.table.find (command_name ("QUIT")) != nullptr);
    assert (r.table.find (command_name ("exit")) != nullptr);
    assert (r.table.find (command_name ("nope")) == nullptr);

    assert (empty.size () == 0);

    registration_result dupe (
      r.table.with_command (decl ("quit", {}),
                            [] (const invocation_context&) {}));
    assert (!dupe.ok ());
    assert (dupe.table.size () == 1);
  }

  void
  test_dispatch_typed ()
  {
    int seen_int (0);
    string seen_name;

    command_table t;
    registration_result r (t.with_command (
      decl ("set", {param ("name", parameter_kind::dvar_ref, true),
                   param ("value", parameter_kind::integer, true)}),
      [&] (const invocation_context& c)
      {
        seen_name = c.text (0);
        seen_int = static_cast<int> (c.integer (1));
      }));
    assert (r.ok ());

    diagnostics d (dispatch (r.table, only_invocation ("set com_maxfps 125")));
    assert (!d.has_errors ());
    assert (seen_name == "com_maxfps");
    assert (seen_int == 125);
  }

  void
  test_dispatch_errors ()
  {
    command_table t;
    registration_result r (t.with_command (
      decl ("set", {param ("name", parameter_kind::word, true),
                   param ("value", parameter_kind::integer, true)}),
      [] (const invocation_context&) {}));

    // Unknown command.
    //
    {
      diagnostics d (dispatch (r.table, only_invocation ("nope x")));
      assert (d.has_errors ());
      assert (d.entries ().front ().code ==
              diagnostic_code::inv_unknown_command);
    }

    // Too few arguments.
    //
    {
      diagnostics d (dispatch (r.table, only_invocation ("set onlyname")));
      assert (d.has_errors ());
      assert (d.entries ().front ().code ==
              diagnostic_code::inv_arity_mismatch);
    }

    // Too many arguments (no variadic).
    //
    {
      diagnostics d (dispatch (r.table, only_invocation ("set a 1 extra")));
      assert (d.has_errors ());
      assert (d.entries ().front ().code ==
              diagnostic_code::inv_arity_mismatch);
    }

    // Type error: value is not an integer.
    //
    {
      diagnostics d (dispatch (r.table, only_invocation ("set a notanumber")));
      assert (d.has_errors ());
      assert (d.entries ().front ().code ==
              diagnostic_code::inv_argument_type);
    }
  }

  void
  test_dispatch_boolean_and_variadic ()
  {
    bool flag (false);
    size_t count (0);

    command_table t;
    registration_result a (t.with_command (
      decl ("toggle_hud", {param ("on", parameter_kind::boolean, true)}),
      [&] (const invocation_context& c) { flag = c.boolean (0); }));

    registration_result b (a.table.with_command (
      decl ("echo", {param ("words", parameter_kind::word, true, true)}),
      [&] (const invocation_context& c) { count = c.arity (); }));
    assert (b.ok ());

    assert (!dispatch (b.table, only_invocation ("toggle_hud on")).has_errors ());
    assert (flag);

    assert (!dispatch (b.table, only_invocation ("echo a b c d")).has_errors ());
    assert (count == 4);
  }

  void
  test_dispatch_raw_input ()
  {
    string captured;

    command_table t;
    registration_result r (t.with_command (
      decl ("say", {param ("message", parameter_kind::word, true, true)},
            command_flags::raw_input),
      [&] (const invocation_context& c) { captured = c.raw (); }));
    assert (r.ok ());

    assert (!dispatch (r.table, only_invocation ("say hello there world"))
               .has_errors ());
    assert (captured == "hello there world");
  }

  class fake_dvar_gateway : public dvar_gateway
  {
  public:
    map<string, string> values;
    map<string, string> descriptions; // optional, by name
    string read_only_name;

    bool
    exists (const dvar_name& name) const override
    {
      return values.find (lower (name.str ())) != values.end ();
    }

    optional<string>
    get (const dvar_name& name) const override
    {
      auto it (values.find (lower (name.str ())));
      if (it == values.end ())
        return nullopt;
      return it->second;
    }

    dvar_set_outcome
    set (const dvar_name& name, string_view value) override
    {
      const string key (lower (name.str ()));

      if (values.find (key) == values.end ())
        return {dvar_set_status::not_found, {}};

      if (key == read_only_name)
        return {dvar_set_status::read_only, {}};

      if (value == "out_of_range")
        return {dvar_set_status::domain_error, "value exceeds maximum"};

      values[key] = string (value);
      return {dvar_set_status::ok, {}};
    }

    void
    enumerate (const function<void (const dvar_descriptor&)>& fn)
        const override
    {
      for (const auto& [k, v] : values)
      {
        auto d (descriptions.find (k));
        fn (dvar_descriptor {dvar_name (k), v,
                            d != descriptions.end () ? d->second : ""});
      }
    }

  private:
    static string
    lower (string s)
    {
      for (char& c : s)
        c = ascii_to_lower (c);
      return s;
    }
  };

  void
  test_dvar_lookup ()
  {
    fake_dvar_gateway g;
    g.values["com_maxfps"] = "125";

    dvar_eval_result r (evaluate_dvar (g, only_invocation ("com_maxfps")));
    assert (r.ok ());
    assert (r.output.has_value ());
    assert (*r.output == "\"com_maxfps\" is \"125\"");
  }

  void
  test_dvar_assignment ()
  {
    fake_dvar_gateway g;
    g.values["com_maxfps"] = "60";

    dvar_eval_result r (evaluate_dvar (g, only_invocation ("com_maxfps 250")));
    assert (r.ok ());
    assert (!r.output.has_value ()); // Assignment prints nothing.
    assert (g.values["com_maxfps"] == "250");
  }

  void
  test_dvar_domain_failure ()
  {
    fake_dvar_gateway g;
    g.values["sv_maxclients"] = "18";

    dvar_eval_result r (
      evaluate_dvar (g, only_invocation ("sv_maxclients out_of_range")));
    assert (!r.ok ());
    assert (r.diags.entries ().front ().code ==
            diagnostic_code::dvar_domain_violation);
    assert (r.diags.entries ().front ().detail.has_value ());
  }

  void
  test_dvar_read_only ()
  {
    fake_dvar_gateway g;
    g.values["version"] = "1.0";
    g.read_only_name = "version";

    dvar_eval_result r (evaluate_dvar (g, only_invocation ("version 2.0")));
    assert (!r.ok ());
    assert (g.values["version"] == "1.0");
  }

  command_table
  sample_table ()
  {
    command_table t;
    for (const char* n : {"map", "map_restart", "maps", "vid_restart",
                          "cg_fov", "quit"})
    {
      registration_result r (
        t.with_command (decl (n, {}), [] (const invocation_context&) {}));
      assert (r.ok ());
      t = r.table;
    }
    return t;
  }

  bool
  contains_text (const completion_result& r, string_view text)
  {
    for (const completion_match& m : r.matches)
      if (m.text == text)
        return true;
    return false;
  }

  void
  test_completion_prefix_ranks_first ()
  {
    completion_index idx (index_commands (sample_table ()));

    completion_request req;
    req.query = "map";
    req.expected = completion_kind::command;

    completion_result r (complete (idx, req));
    assert (!r.empty ());

    assert (r.matches.front ().text == "map");
    assert (r.matches.front ().score == 100);
    assert (contains_text (r, "map_restart"));
    assert (contains_text (r, "maps"));

    for (size_t i (1); i < r.matches.size (); ++i)
      assert (r.matches[i - 1].score >= r.matches[i].score);
  }

  void
  test_completion_fuzzy_match ()
  {
    completion_index idx (index_commands (sample_table ()));

    completion_request req;
    req.query = "fov";
    req.expected = completion_kind::command;

    completion_result r (complete (idx, req));
    assert (contains_text (r, "cg_fov"));
  }

  void
  test_completion_empty_query_lists_all ()
  {
    completion_index idx (index_commands (sample_table ()));

    completion_request req;
    req.expected = completion_kind::command;

    completion_result r (complete (idx, req));
    assert (r.matches.size () == 6);
  }

  void
  test_completion_kind_filter_and_dvars ()
  {
    fake_dvar_gateway g;
    g.values["com_maxfps"] = "125";
    g.values["cg_fov"] = "65";

    completion_index idx (index_dvars (index_commands (sample_table ()), g));

    completion_request req;
    req.query = "cg";
    req.expected = completion_kind::dvar;

    completion_result r (complete (idx, req));
    assert (!r.empty ());
    for (const completion_match& m : r.matches)
      assert (m.kind == completion_kind::dvar);
    assert (contains_text (r, "cg_fov"));
  }

  void
  test_completion_dvar_description ()
  {
    fake_dvar_gateway g;
    g.values["cg_fov"] = "65";
    g.descriptions["cg_fov"] = "Field of view, in degrees.\nClamped to [40, 90].";

    completion_index idx (index_dvars (completion_index {}, g));

    completion_request req;
    req.query = "cg_fov";
    req.expected = completion_kind::dvar;

    completion_result r (complete (idx, req));
    assert (!r.empty ());

    const completion_match& m (r.matches.front ());
    assert (m.text == "cg_fov");

    assert (m.annotation.has_value () && *m.annotation == "Field of view, in degrees.");
    assert (m.description.has_value () && *m.description == "Clamped to [40, 90].");

    g.values["sv_quiet"] = "1";
    completion_index idx2 (index_dvars (completion_index {}, g));
    completion_request req2;
    req2.query = "sv_quiet";
    req2.expected = completion_kind::dvar;
    completion_result r2 (complete (idx2, req2));
    assert (!r2.empty ());
    assert (!r2.matches.front ().description.has_value ());
  }

  void
  test_completion_deterministic_and_capped ()
  {
    completion_index idx (index_commands (sample_table ()));

    completion_request req;
    req.expected = completion_kind::command;

    completion_config cfg;
    cfg.max_results = 3;

    completion_result a (complete (idx, req, cfg));
    completion_result b (complete (idx, req, cfg));

    assert (a.matches.size () == 3);

    assert (a.matches.size () == b.matches.size ());
    for (size_t i (0); i < a.matches.size (); ++i)
      assert (a.matches[i].text == b.matches[i].text);
  }

  void
  test_scroll_view_offset ()
  {
    constexpr size_t window (10);
    constexpr size_t scrolloff (4);

    assert (scroll_view_offset (3,  5,   window, scrolloff,  0) ==  0);
    assert (scroll_view_offset (5,  30,  window, scrolloff,  0) ==  0);
    assert (scroll_view_offset (6,  30,  window, scrolloff,  0) ==  1);
    assert (scroll_view_offset (6,  30,  window, scrolloff,  5) ==  2);
    assert (scroll_view_offset (4,  30,  window, scrolloff,  5) ==  0);
    assert (scroll_view_offset (29, 30,  window, scrolloff,  0) == 20);
    assert (scroll_view_offset (25, 30,  window, scrolloff, 20) == 20);
    assert (scroll_view_offset (0,  30,  window, scrolloff, 20) ==  0);
    assert (scroll_view_offset (5,  30,  0,      scrolloff,  0) ==  0);
  }

  void
  test_completion_argument_domain ()
  {
    auto arg = [] (string text)
    {
      completion_candidate c;
      c.text = move (text);
      c.kind = completion_kind::argument;
      return c;
    };

    vector<completion_candidate> domain {
      arg ("easy"), arg ("normal"), arg ("hard"), arg ("veteran")};

    completion_request req;
    req.query = "ea";

    completion_result r (complete_from (domain, req));
    assert (!r.empty ());
    assert (r.matches.front ().text == "easy");
  }

  struct capture
  {
    vector<string> output;
    vector<diagnostic> diags;
  };

  console
  make_console (fake_dvar_gateway& g, capture& cap,
               vector<string>* engine = nullptr)
  {
    console c (
      g,
      [&cap] (string_view t) { cap.output.emplace_back (t); },
      [&cap] (const diagnostic& d) { cap.diags.push_back (d); });

    if (engine != nullptr)
      c.set_engine_executor (
        [engine] (string_view t) { engine->emplace_back (t); });

    return c;
  }

  void
  test_console_dispatches_owned_command ()
  {
    fake_dvar_gateway g;
    capture cap;
    console c (make_console (g, cap));

    int called (0);
    c.add_command (decl ("greet", {}),
                  [&called] (const invocation_context&) { ++called; });

    c.evaluate ("greet");
    assert (called == 1);
    assert (cap.diags.empty ());
  }

  void
  test_console_routes_to_dvar ()
  {
    fake_dvar_gateway g;
    g.values["com_maxfps"] = "60";
    capture cap;
    console c (make_console (g, cap));

    c.evaluate ("com_maxfps");
    assert (cap.output.size () == 1);
    assert (cap.output[0] == "\"com_maxfps\" is \"60\"");

    c.evaluate ("com_maxfps 125");
    assert (g.values["com_maxfps"] == "125");
  }

  void
  test_console_engine_fallback ()
  {
    fake_dvar_gateway g;
    capture cap;
    vector<string> engine;
    console c (make_console (g, cap, &engine));

    c.evaluate ("vid_restart");
    assert (engine.size () == 1);
    assert (engine[0] == "vid_restart");
    assert (cap.diags.empty ()); // Fallback is not an error.

    engine.clear ();
    c.evaluate ("/set cg_fov 20");
    assert (engine.size () == 1);
    assert (engine[0] == "set cg_fov 20");

    engine.clear ();
    c.evaluate ("say \"hello world\"");
    assert (engine.size () == 1);
    assert (engine[0] == "say \"hello world\"");
  }

  void
  test_console_unknown_without_fallback ()
  {
    fake_dvar_gateway g;
    capture cap;
    console c (make_console (g, cap));

    c.evaluate ("bogus_name");
    bool saw_unknown (false);
    for (const diagnostic& d : cap.diags)
      if (d.code == diagnostic_code::inv_unknown_command)
        saw_unknown = true;
    assert (saw_unknown);
  }

  void
  test_console_complete_command_context ()
  {
    fake_dvar_gateway g;
    g.values["cg_fov"] = "65";
    capture cap;
    console c (make_console (g, cap));
    c.add_command (decl ("map", {}), [] (const invocation_context&) {});
    c.add_command (decl ("map_restart", {}), [] (const invocation_context&) {});

    completion_result r (c.complete ("ma", 2));
    bool saw_map (false);
    for (const completion_match& m : r.matches)
      if (m.text == "map")
        saw_map = true;
    assert (saw_map);
  }

  void
  test_console_complete_argument_context ()
  {
    fake_dvar_gateway g;
    g.values["com_maxfps"] = "60";
    g.values["cg_fov"] = "65";
    capture cap;
    console c (make_console (g, cap));

    c.add_command (
      decl ("set", {param ("name", parameter_kind::dvar_ref, true),
                   param ("value", parameter_kind::word, false)}),
      [] (const invocation_context&) {});

    completion_result r (c.complete ("set com", 7));
    assert (!r.empty ());
    for (const completion_match& m : r.matches)
      assert (m.kind == completion_kind::dvar);

    bool saw (false);
    for (const completion_match& m : r.matches)
      if (m.text == "com_maxfps")
        saw = true;
    assert (saw);
  }

  void
  test_input_editing ()
  {
    input_buffer b;
    b.insert ("hello");
    assert (b.text () == "hello");
    assert (b.cursor () == 5);

    b.move_home (false);
    assert (b.cursor () == 0);
    b.insert (">");
    assert (b.text () == ">hello");
    assert (b.cursor () == 1);

    b.move_end (false);
    b.backspace ();
    assert (b.text () == ">hell");

    b.move_word_left (false);
    assert (b.cursor () == 0);

    b.erase_forward ();
    assert (b.text () == "hell");

    input_buffer w;
    w.insert ("the quick");
    w.move_word_left (false);
    assert (w.cursor () == 4);
  }

  void
  test_input_selection ()
  {
    input_buffer b;
    b.insert ("abcdef");

    b.move_home (false);
    b.move_right (true);
    b.move_right (true);
    assert (b.has_selection ());
    assert (b.selected_text () == "ab");

    b.insert ("X");
    assert (b.text () == "Xcdef");
    assert (!b.has_selection ());

    b.select_all ();
    assert (b.selected_text () == "Xcdef");
    b.backspace ();
    assert (b.empty ());
  }

  void
  test_input_kill_ops ()
  {
    // Ctrl+W: delete the word before the cursor.
    //
    {
      input_buffer b;
      b.insert ("map mp_rust");
      b.erase_word_before ();
      assert (b.text () == "map ");
      assert (b.cursor () == 4);
    }

    // Alt+D: delete the word after the cursor.
    //
    {
      input_buffer b;
      b.insert ("map mp_rust");
      b.move_home (false);
      b.erase_word_after ();
      assert (b.text () == " mp_rust");
    }

    // Ctrl+U: kill to line start.
    //
    {
      input_buffer b;
      b.insert ("hello world");
      b.move_word_left (false);
      b.erase_to_start ();
      assert (b.text () == "world");
      assert (b.cursor () == 0);
    }

    // Ctrl+K: kill to line end.
    //
    {
      input_buffer b;
      b.insert ("hello world");
      b.move_home (false);
      b.move_word_right (false);
      b.erase_to_end ();
      assert (b.text () == "hello");
    }

    // A kill with an active selection removes the selection.
    //
    {
      input_buffer b;
      b.insert ("abcdef");
      b.move_home (false);
      b.move_right (true);
      b.move_right (true);
      b.erase_word_before ();
      assert (b.text () == "cdef");
    }
  }

  void
  test_input_max_length ()
  {
    input_buffer b;
    b.insert (string (input_buffer::max_length + 50, 'x'));
    assert (b.text ().size () == input_buffer::max_length);
  }

  void
  test_history_navigation ()
  {
    history h;
    h.push ("first");
    h.push ("second");
    h.push ("first");
    assert (h.size () == 2);

    auto a (h.previous ("typing"));
    assert (a.has_value () && *a == "first");
    auto b (h.previous ("typing"));
    assert (b.has_value () && *b == "second");
    auto c (h.previous ("typing"));
    assert (!c.has_value ());

    auto d (h.next ());
    assert (d.has_value () && *d == "first");
    auto e (h.next ());
    assert (e.has_value () && *e == "typing");
    assert (!h.navigating ());
  }

  void
  test_screen_scroll ()
  {
    screen s;
    for (int i (0); i < 5; ++i)
      s.print (format ("line{}\n", i));

    assert (s.scroll () == 0);

    s.scroll_up (2);
    assert (s.scroll () == 2);

    s.scroll_down (1);
    assert (s.scroll () == 1);

    s.scroll_to_bottom ();
    assert (s.scroll () == 0);

    s.set_visible_rows (4);
    assert (s.max_scroll () == 2);
    s.scroll_up (100);
    assert (s.scroll () == 2);

    s.set_visible_rows (10);
    assert (s.max_scroll () == 0);
    assert (s.scroll () == 0);

    s.clear ();
    assert (s.lines ().size () == 1);
  }

  void
  test_render_model ()
  {
    screen s;
    for (int i (0); i < 10; ++i)
      s.print (format ("L{}\n", i));

    input_buffer in;
    in.insert ("map mp_rust");

    render_model m (build_model (s, in));
    assert (m.total_lines == 11);
    assert (m.scroll_lines == 0);
    assert (m.output.size () == 11);
    assert (m.input == "map mp_rust");
    assert (m.cursor_column == in.cursor ());

    s.scroll_up (2);
    render_model m2 (build_model (s, in));
    assert (m2.scroll_lines == 2);
    assert (m2.output.size () == 9);
  }

  void
  test_rect_algebra ()
  {
    rect r {10.0f, 20.0f, 100.0f, 40.0f};
    rect i (r.inset (5.0f));
    assert (i.x == 15.0f && i.y == 25.0f && i.w == 90.0f && i.h == 30.0f);
    assert (r.inset (1000.0f).empty ());

    auto [top, rest] (r.split_top (12.0f));
    assert (top.h == 12.0f && top.y == r.top ());
    assert (rest.y == r.top () + 12.0f && rest.h == r.h - 12.0f);
    assert (top.bottom () == rest.top ());

    auto [col, body] (r.split_right (10.0f));
    assert (col.w == 10.0f && col.right () == r.right ());
    assert (body.right () == col.left ());

    auto [big, none] (r.split_top (9999.0f));
    assert (big.h == r.h && none.empty ());
  }

  void
  test_compute_layout ()
  {
    console_metrics m;
    m.line_height = 16.0f;

    const rect viewport {0.0f, 0.0f, 640.0f, 480.0f};
    console_layout layout (compute_layout (viewport, m));

    assert (layout.input_box.top () == m.margin);
    assert (layout.input_box.h == m.input_height ());

    assert (layout.output_box.top () >=
            layout.input_box.bottom () + m.gap - 0.001f);
    assert (layout.scrollbar_track.right () ==
            layout.output_box.inset (m.padding).right ());
    assert (layout.output_text.right () == layout.scrollbar_track.left ());
    assert (layout.visible_lines > 0);
  }

  void
  test_scrollbar_thumb ()
  {
    rect track {100.0f, 0.0f, 10.0f, 200.0f};

    assert (!scrollbar_thumb (track, 5, 10, 0, 16.0f).has_value ());

    auto bottom (scrollbar_thumb (track, 100, 20, 0, 16.0f));
    assert (bottom.has_value ());
    assert (abs (bottom->bottom () - track.bottom ()) < 0.5f);

    auto top (scrollbar_thumb (track, 100, 20, 80, 16.0f));
    assert (top.has_value ());
    assert (abs (top->top () - track.top ()) < 0.5f);

    assert (bottom->h >= 16.0f);
  }

  void
  check_lex_invariants (string_view source)
  {
    lex_result r (lex (source));

    assert (!r.tokens.empty ());
    assert (r.tokens.back ().kind == token_kind::end_of_input);
    assert (r.tokens.back ().span.begin == source.size ());
    assert (r.tokens.back ().span.end == source.size ());

    source_offset prev_begin (0);
    for (const token& t : r.tokens)
    {
      assert (t.span.begin <= t.span.end);
      assert (t.span.end <= source.size ());

      assert (t.span.begin >= prev_begin);
      prev_begin = t.span.begin;

      if (t.kind == token_kind::invalid ||
          t.kind == token_kind::unterminated_string)
        assert (!r.diags.empty ());
    }
  }

  void
  check_parse_invariants (string_view source)
  {
    parse_result r (parse (source));

    for (const statement& s : r.statements)
    {
      if (holds_alternative<invocation> (s))
      {
        const invocation& inv (get<invocation> (s));
        assert (inv.span.begin <= inv.span.end);
        assert (inv.span.end <= source.size ());

        for (const argument_value& a : inv.arguments)
        {
          assert (a.span ().begin <= a.span ().end);
          assert (a.span ().end <= source.size ());
        }
      }
      else
      {
        const invalid_statement& bad (get<invalid_statement> (s));
        assert (bad.span.end <= source.size ());
      }
    }
  }

  void
  test_edge_cases ()
  {
    const char* cases[] {
      "",
      " ",
      "\t\r\n",
      ";;;;",
      "\"",
      "\"unterminated",
      "'",
      "\\",
      "\"\\\"",
      "a\"b\"c",
      "set",
      "set ",
      "   set   x   ",
      "/map",
      "\\\\map",
      "cmd \"a b\" 'c d' 42 -3.14",
      "a;b;c;d",
      "name value extra trailing tokens here",
    };

    for (const char* c : cases)
    {
      check_lex_invariants (c);
      check_parse_invariants (c);
    }

    string control;
    for (int i (1); i < 256; ++i)
      control.push_back (static_cast<char> (i));
    check_lex_invariants (control);
    check_parse_invariants (control);

    string long_line (10000, 'x');
    check_lex_invariants (long_line);
    check_parse_invariants (long_line);
  }

  void
  test_fuzz_lex_parse_complete ()
  {
    const string alphabet (
      "abcdef ABC 0123 \"'\\;/_-. \t\n\x01\x7f\xff");

    command_table table (sample_table ());
    completion_index idx (index_commands (table));

    uint64_t state (0x9E3779B97F4A7C15ull);
    auto next = [&state] () -> uint64_t
    {
      state ^= state << 13;
      state ^= state >> 7;
      state ^= state << 17;
      return state;
    };

    for (int iter (0); iter < 4000; ++iter)
    {
      const size_t len (next () % 40);
      string input;
      input.reserve (len);
      for (size_t i (0); i < len; ++i)
        input.push_back (alphabet[next () % alphabet.size ()]);

      check_lex_invariants (input);
      check_parse_invariants (input);

      completion_request req;
      req.query = input;
      completion_result a (complete (idx, req));
      completion_result b (complete (idx, req));

      assert (a.matches.size () == b.matches.size ());
      for (size_t i (0); i < a.matches.size (); ++i)
      {
        assert (a.matches[i].text == b.matches[i].text);
        assert (a.matches[i].score >= 0 && a.matches[i].score <= 100);
      }
    }
  }

  void
  test_token_roundtrip ()
  {
    assert (to_string (token_kind::identifier) == "identifier");
    assert (to_string (token_kind::unterminated_string) == "unterminated-string");
    assert (to_string (value_kind::floating) == "floating");
    assert (to_string (completion_kind::dvar) == "dvar");
    assert (to_string (diagnostic_category::dvar_binding) == "dvar-binding");
  }
}

int
main ()
{
  test_ascii_classifiers ();
  test_source_span ();
  test_line_column ();
  test_names ();
  test_argument_value ();
  test_diagnostics ();
  test_token_roundtrip ();

  test_lex_empty ();
  test_lex_words_and_spans ();
  test_lex_numbers ();
  test_lex_strings ();
  test_lex_unterminated_string ();
  test_lex_invalid_character ();
  test_lex_unknown_escape_recovers ();
  test_lex_separator ();

  test_parse_simple_invocation ();
  test_parse_typed_arguments ();
  test_parse_slash_prefix ();
  test_parse_statement_separation ();
  test_parse_empty_statement_warns ();
  test_parse_bad_head ();
  test_parse_empty_input ();
  test_parse_merges_lexical_diagnostics ();

  test_declaration_validation ();
  test_command_table_registration ();
  test_dispatch_typed ();
  test_dispatch_errors ();
  test_dispatch_boolean_and_variadic ();
  test_dispatch_raw_input ();

  test_dvar_lookup ();
  test_dvar_assignment ();
  test_dvar_domain_failure ();
  test_dvar_read_only ();

  test_completion_prefix_ranks_first ();
  test_completion_fuzzy_match ();
  test_completion_empty_query_lists_all ();
  test_completion_kind_filter_and_dvars ();
  test_completion_dvar_description ();
  test_completion_deterministic_and_capped ();
  test_completion_argument_domain ();
  test_scroll_view_offset ();

  test_console_dispatches_owned_command ();
  test_console_routes_to_dvar ();
  test_console_engine_fallback ();
  test_console_unknown_without_fallback ();
  test_console_complete_command_context ();
  test_console_complete_argument_context ();

  test_input_editing ();
  test_input_selection ();
  test_input_kill_ops ();
  test_input_max_length ();
  test_history_navigation ();
  test_screen_scroll ();
  test_render_model ();
  test_rect_algebra ();
  test_compute_layout ();
  test_scrollbar_thumb ();

  test_edge_cases ();
  test_fuzz_lex_parse_complete ();
}
