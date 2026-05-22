#pragma once

#include <string_view>
#include <vector>

#include <libiw4x/console/types.hxx>

namespace iw4x::console
{
  // Result of a lexer pass.
  //
  // Notice that the tokens vector always ends with the end_of_input token. The
  // diags member holds every lexical diagnostic produced during the scan. We
  // consider the result ok () if the scan was completely clean or if it only
  // produced warnings. For instance, the lexer might encounter an unknown
  // escape sequence but recover by taking the character literally, which would
  // issue a warning but not fail the lexing process entirely.
  //
  struct lex_result
  {
    std::vector<token> tokens;
    diagnostics diags;

    // Return true if no hard errors were encountered during the lexing phase.
    //
    bool
    ok () const noexcept
    {
      return !diags.has_errors ();
    }
  };

  // Lex a complete console line.
  //
  // The source string view is only borrowed for the duration of this call.
  // We make sure that the resulting tokens own copies of their lexeme and
  // decoded values. This means the returned lex_result can safely outlive
  // the input source buffer.
  //
  lex_result
  lex (std::string_view source);

  // Classify a word lexeme as numeric-shaped.
  //
  // We expose this primarily for the parser, which needs the exact same
  // predicate when deciding whether an identifier-position token is actually
  // a number. It is also useful for our tests.
  //
  // A word is considered numeric if it matches an optional sign, followed
  // by an integer or a decimal form, and finally an optional exponent.
  //
  // Note that this function only recognizes the shape of the lexeme. It
  // does not perform any actual numeric conversion or range-checking on
  // the underlying value.
  //
  bool
  is_number_lexeme (std::string_view lexeme) noexcept;
}
