#include <libiw4x/console/parser.hxx>

#include <cassert>
#include <charconv>
#include <span>
#include <utility>

#include <libiw4x/console/lexer.hxx>

using namespace std;

namespace iw4x::console
{
  namespace
  {
    // Translate the token's lexical shape into a concrete, typed argument
    // value.
    //
    // The lexer has already classified the token's shape, so here we commit to
    // a specific value. We try parsing numbers as integers first. If that
    // overflows or fails, we fall back to floating point. If neither parses, we
    // just keep it as a string. Strings and identifiers just carry their
    // decoded or raw text.
    //
    // Note that invalid tokens are not passed here. The caller skips them
    // because the lexer has already diagnosed them.
    //
    argument_value
    convert_argument (const token& t)
    {
      switch (t.kind)
      {
        case token_kind::number:
        {
          const string& s (t.value);

          int64_t iv {};
          auto [ip, iec] (from_chars (s.data (), s.data () + s.size (), iv));

          if (iec == errc {} && ip == s.data () + s.size ())
            return argument_value::of_integer (iv, t.span);

          double dv {};
          auto [dp, dec] (from_chars (s.data (), s.data () + s.size (), dv));

          if (dec == errc {} && dp == s.data () + s.size ())
            return argument_value::of_floating (dv, t.span);

          // The shape looked like a number, but neither integer nor floating
          // point conversions accepted it. For example, it might be a value way
          // out of double's range. Preserve the raw text so we don't lose
          // anything.
          //
          return argument_value::of_string (s, t.span);
        }

        case token_kind::string:
        case token_kind::unterminated_string:
        case token_kind::identifier:
          return argument_value::of_string (t.value, t.span);

        default:
          // Separator, end_of_input, or invalid tokens are handled by the
          // caller and should never reach argument conversion.
          //
          assert (false && "convert_argument called on a non-argument token");
          return argument_value::of_string (t.lexeme, t.span);
      }
    }

    // Strip a single leading slash from a command or variable head name.
    //
    // The engine accepts "/map" and "\map" as synonyms for "map" on the console
    // line. We honour exactly one leading slash here so that an
    // argument-bearing path is not accidentally mangled.
    //
    string
    canonical_head (string_view lexeme)
    {
      if (!lexeme.empty () && (lexeme.front () == '/' || lexeme.front () == '\\'))
        return string (lexeme.substr (1));

      return string (lexeme);
    }

    // Parse a single statement from a slice of tokens representing the run
    // between separators.
    //
    // We append the resulting statement to the output vector and accumulate
    // any syntax diagnostics. Note that an empty slice (which happens with
    // two adjacent separators, or a leading/trailing one) produces no
    // statement and just yields a recoverable warning.
    //
    void
    parse_statement (span<const token> toks,
                     vector<statement>& out,
                     diagnostics& diags)
    {
      if (toks.empty ())
      {
        // There is nothing between separators. This is completely harmless but
        // worth surfacing so a stray ';' does not pass completely unnoticed.
        //
        diagnostic d;
        d.severity = diagnostic_severity::warning;
        d.category = diagnostic_category::syntax;
        d.code = diagnostic_code::syn_empty_input;
        d.message = "empty statement";
        d.recovery_hint = "remove the redundant ';' separator";
        diags.add (move (d));
        return;
      }

      const token& head (toks.front ());

      // A statement must begin with an identifier name. If we see a number,
      // string, or an invalid byte in the head position, that is a syntax
      // error. The console line must name a command or dvar first.
      //
      if (head.kind != token_kind::identifier)
      {
        diagnostic d;
        d.severity = diagnostic_severity::error;
        d.category = diagnostic_category::syntax;
        d.code = diagnostic_code::syn_unexpected_token;
        d.message = format (
          "expected a command or variable name, found {}",
          to_string (head.kind));
        d.span = head.span;
        d.recovery_hint = "begin the statement with a command or variable name";
        diags.add (move (d));

        // Record the invalid statement spanning the entire slice so that
        // callers can highlight all of it in the UI.
        //
        source_span extent (head.span.merge (toks.back ().span));
        out.push_back (invalid_statement {extent});
        return;
      }

      invocation inv;
      inv.head = head;
      inv.name = command_name (canonical_head (head.lexeme));

      for (const token& t : toks.subspan (1))
      {
        // Skip over any bytes that the lexer already rejected. Their diagnostic
        // is enough and they carry no usable value.
        //
        if (t.kind == token_kind::invalid)
          continue;

        // Note that a separator never appears inside a slice since the
        // splitter consumed it, and end_of_input is strictly excluded by
        // the caller.
        //
        assert (t.kind != token_kind::separator &&
                t.kind != token_kind::end_of_input);

        inv.arguments.push_back (convert_argument (t));
      }

      inv.span = head.span.merge (toks.back ().span);
      out.push_back (move (inv));
    }
  }

  parse_result
  parse (const vector<token>& tokens)
  {
    parse_result result;

    assert (!tokens.empty () &&
            tokens.back ().kind == token_kind::end_of_input);

    size_t start (0);

    for (size_t i (0); i < tokens.size (); ++i)
    {
      const token_kind k (tokens[i].kind);

      if (k == token_kind::separator || k == token_kind::end_of_input)
      {
        span<const token> slice (tokens.data () + start, i - start);
        parse_statement (slice, result.statements, result.diags);
        start = i + 1;

        if (k == token_kind::end_of_input)
          break;
      }
    }

    return result;
  }

  parse_result
  parse (string_view source)
  {
    lex_result lexed (lex (source));
    parse_result result (parse (lexed.tokens));

    // Merge the lexical diagnostics ahead of the syntax ones so that the
    // resulting diagnostic bag reads in the actual source order of discovery.
    //
    diagnostics merged (move (lexed.diags));
    merged.merge (move (result.diags));
    result.diags = move (merged);

    return result;
  }
}
