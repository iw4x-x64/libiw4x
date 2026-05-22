#pragma once

#include <string_view>
#include <variant>
#include <vector>

#include <libiw4x/console/types.hxx>

namespace iw4x::console
{
  // Command or dvar invocation.
  //
  // This represents a head name followed by zero or more typed arguments.
  //
  // Note that we keep the original identifier token as `head` to preserve its
  // casing and source span. The `name` is the head folded into a
  // case-insensitive `command_name`.
  //
  // Note also that we also strip any single leading '/' or '\' here to matches
  // the engine's tolerance for slash prefixes on console commands. That is, the
  // `arguments` are the converted literal values in order, and the `span`
  // naturally covers the whole invocation.
  //
  struct invocation
  {
    token                       head;
    command_name                name;
    std::vector<argument_value> arguments;
    source_span                 span;
  };

  // Statement that the parser could not form into a valid invocation.
  //
  // You might wonder why we don't just throw an exception or return null.
  // Representing invalid syntax explicitly like this is what lets a malformed
  // statement coexist with valid ones on the very same line. The accompanying
  // diagnostic (which you can find in `parse_result::diags`) explains exactly
  // why it failed. Notice that we still record the span of the offending text
  // so the caller can easily highlight it.
  //
  struct invalid_statement
  {
    source_span span;
  };

  // Single parsed statement.
  //
  // It can be either a parsed invocation or an invalid statement placeholder.
  //
  using statement = std::variant<invocation, invalid_statement>;

  // The outcome of a parse operation.
  //
  // The `statements` list holds one entry per ';'-separated, non-empty
  // statement, in the exact order they appeared. The `diags` member carries any
  // syntax diagnostics. If `parse(string_view)` was used, the lexical
  // diagnostics are also merged in here. The `ok ()` helper returns false if
  // any of these diagnostics is a hard error.
  //
  struct parse_result
  {
    std::vector<statement> statements;
    diagnostics diags;

    bool
    ok () const noexcept
    {
      return !diags.has_errors ();
    }
  };

  // Parse a previously lexed token stream.
  //
  // The token vector must be well-formed in the lexer's sense, meaning it ends
  // with a single `end_of_input` token. Note that with this overload, lexical
  // diagnostics (if any) are the caller's responsibility to carry around. We
  // only report syntax diagnostics here.
  //
  parse_result
  parse (std::vector<token> const& tokens);

  // Lex and parse the source text in one step.
  //
  // This is a convenience for the common case. Lexical and syntax diagnostics
  // are neatly merged into the result, with lexical coming first, so you get a
  // single bag describing the whole line.
  //
  parse_result
  parse (std::string_view source);
}
