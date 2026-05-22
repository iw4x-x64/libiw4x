#include <libiw4x/console/lexer.hxx>

#include <cassert>
#include <utility>

#include <libiw4x/console/utility.hxx>

using namespace std;

namespace iw4x::console
{
  namespace
  {
    // Determine if a byte can appear inside a bare word.
    //
    // The idea is that words are runs of visible ASCII characters, excluding
    // those that trigger some other token. For instance, quotes start strings
    // and ';' is the structural separator. Everything else that is printable
    // (letters, digits, punctuation, path separators, signs) is fair game. This
    // allows arguments like file paths and flags to survive the lexing process
    // intact.
    //
    CONSOLE_FORCEINLINE bool
    is_word_byte (unsigned char c) noexcept
    {
      // Exclude control characters, space, DEL, or any non-ASCII byte.
      //
      if (c < 0x21 || c > 0x7E)
        return false;

      return c != '"' && c != '\'' && c != ';';
    }

    // The scanning cursor.
    //
    // This is a thin wrapper over the borrowed source string that tracks our
    // current offset and centralizes bounds-safe peeking. By keeping the index
    // discipline isolated in one place, we allow the rest of the lexer to read
    // much more like a grammar specification.
    //
    class cursor
    {
    public:
      explicit
      cursor (string_view s) noexcept
        : src_ (s) {}

      // Check if we have reached the end of the input stream.
      //
      bool
      at_end () const noexcept
      {
        return pos_ >= src_.size ();
      }

      // Get the current offset as a strongly-typed source_offset.
      //
      source_offset
      offset () const noexcept
      {
        return static_cast<source_offset> (pos_);
      }

      // Return the current byte, or '\0' at the end of the input.
      //
      // Note that if we encounter a genuine NUL byte in the input, it is
      // handled as an invalid character before we ever need to rely on this
      // sentinel. As a result, this overload remains unambiguous in practice.
      //
      char
      peek () const noexcept
      {
        return at_end () ? '\0' : src_[pos_];
      }

      // Look ahead by a specific number of bytes.
      //
      char
      peek_at (size_t ahead) const noexcept
      {
        const size_t i (pos_ + ahead);
        return i < src_.size () ? src_[i] : '\0';
      }

      // Consume and return the current byte. We assert that we are not reading
      // past the end of the input.
      //
      char
      advance () noexcept
      {
        assert (!at_end ());
        return src_[pos_++];
      }

    private:
      string_view src_;
      size_t pos_ {0};
    };

    // Build a span.
    //
    // Here we assert the half-open invariant right at the point of creation to
    // catch any logical errors as early as possible.
    //
    CONSOLE_FORCEINLINE source_span
    make_span (source_offset begin, source_offset end) noexcept
    {
      assert (begin <= end);
      return source_span (begin, end);
    }

    // Scan a quoted string starting at the opening quote.
    //
    // We accept both single and double quotes, though the closing quote must
    // naturally match the opener. Escape sequences inside the string are
    // decoded into their actual values. However, we keep the raw text
    // (including the quotes) in the lexeme so that diagnostics can quote it
    // verbatim. Note that if we hit the end of the input without closing the
    // string, we yield an unterminated_string token that spans to the end,
    // along with an error diagnostic.
    //
    token
    scan_string (cursor& c, diagnostics& diags)
    {
      const source_offset begin (c.offset ());
      const char quote (c.advance ()); // Consume the opening quote.

      string value;

      while (!c.at_end ())
      {
        const char ch (c.peek ());

        if (ch == quote)
        {
          c.advance (); // Consume the closing quote.

          // Note that the lexeme (which is the raw source slice, quotes
          // included) will be filled later by the main lex () function. Here,
          // we only take ownership of the decoded value with all escapes fully
          // resolved.
          //
          token t;
          t.kind = token_kind::string;
          t.span = make_span (begin, c.offset ());
          t.value = move (value);
          return t;
        }

        if (ch == '\\')
        {
          c.advance (); // Consume the backslash.

          // If we hit the end of the input right after a backslash, we just
          // break and fall through to the unterminated string error path.
          //
          if (c.at_end ())
            break;

          const source_offset esc_at (c.offset () - 1);
          const char e (c.advance ());

          switch (e)
          {
            case '\\': value.push_back ('\\'); break;
            case '"':  value.push_back ('"');  break;
            case '\'': value.push_back ('\''); break;
            case 'n':  value.push_back ('\n'); break;
            case 't':  value.push_back ('\t'); break;
            case 'r':  value.push_back ('\r'); break;
            case '0':  value.push_back ('\0'); break;
            default:
            {
              // We encountered an unknown escape sequence. We recover by taking
              // the character literally, but we should issue a warning so the
              // author realizes the sequence was not understood.
              //
              diagnostic d;
              d.severity = diagnostic_severity::warning;
              d.category = diagnostic_category::lexical;
              d.code = diagnostic_code::lex_invalid_escape;
              d.message = format ("unknown escape sequence '\\{}'", e);
              d.span = make_span (esc_at, c.offset ());
              d.recovery_hint = "the character after the backslash was kept "
                                "literally";
              diags.add (move (d));

              value.push_back (e);
              break;
            }
          }

          continue;
        }

        value.push_back (ch);
        c.advance ();
      }

      // Reached end-of-input without finding a matching closing quote.
      //
      const source_offset end (c.offset ());

      diagnostic d;
      d.severity = diagnostic_severity::error;
      d.category = diagnostic_category::lexical;
      d.code = diagnostic_code::lex_unterminated_string;
      d.message = "unterminated string literal";
      d.span = make_span (begin, end);
      d.recovery_hint = format ("add a closing {} quote",
                                quote == '"' ? "double" : "single");
      diags.add (move (d));

      token t;
      t.kind = token_kind::unterminated_string;
      t.span = make_span (begin, end);
      t.value = move (value);
      return t;
    }

    // Scan a bare word.
    //
    // We extract the full contiguous sequence of valid word bytes.
    //
    token
    scan_word (cursor& c)
    {
      const source_offset begin (c.offset ());

      while (!c.at_end () &&
             is_word_byte (static_cast<unsigned char> (c.peek ())))
        c.advance ();

      const source_offset end (c.offset ());

      // We must have actually consumed something. The caller should only enter
      // this function when peeking at a valid word byte.
      //
      assert (end > begin);

      // The lexeme, its value, and the final token kind (whether it is an
      // identifier or a number) are determined and filled by lex(). The lex()
      // function holds the source view needed to slice the raw text. At this
      // stage, we only commit to the span extent.
      //
      token t;
      t.kind = token_kind::identifier;
      t.span = make_span (begin, end);
      return t;
    }

    // Scan an invalid byte and emit the corresponding token along with a
    // diagnostic. We advance past the bad byte to continue lexing.
    //
    token
    scan_invalid (cursor& c, diagnostics& diags)
    {
      const source_offset begin (c.offset ());
      const unsigned char bad (static_cast<unsigned char> (c.advance ()));
      const source_offset end (c.offset ());

      diagnostic d;
      d.severity = diagnostic_severity::error;
      d.category = diagnostic_category::lexical;
      d.code = diagnostic_code::lex_invalid_character;
      d.message = format ("invalid character 0x{:02X}", bad);
      d.span = make_span (begin, end);
      d.recovery_hint = "only printable ASCII is permitted outside quotes";
      diags.add (move (d));

      token t;
      t.kind = token_kind::invalid;
      t.span = make_span (begin, end);
      return t;
    }
  }

  // Determine if a word lexeme represents a valid number.
  //
  // We parse an optional sign, an integer part, an optional fractional part,
  // and an optional exponent to see if the entire sequence adheres to a
  // standard numeric format.
  //
  bool
  is_number_lexeme (string_view s) noexcept
  {
    if (s.empty ())
      return false;

    size_t i (0);

    // Optional sign.
    //
    if (s[i] == '+' || s[i] == '-')
      ++i;

    bool saw_digit (false);

    // Integer part.
    //
    while (i < s.size () && ascii_is_digit (s[i]))
    {
      saw_digit = true;
      ++i;
    }

    // Optional fractional part.
    //
    if (i < s.size () && s[i] == '.')
    {
      ++i;
      while (i < s.size () && ascii_is_digit (s[i]))
      {
        saw_digit = true;
        ++i;
      }
    }

    if (!saw_digit)
      return false;

    // Optional exponent part.
    //
    if (i < s.size () && (s[i] == 'e' || s[i] == 'E'))
    {
      ++i;
      if (i < s.size () && (s[i] == '+' || s[i] == '-'))
        ++i;

      bool saw_exp_digit (false);
      while (i < s.size () && ascii_is_digit (s[i]))
      {
        saw_exp_digit = true;
        ++i;
      }

      if (!saw_exp_digit)
        return false;
    }

    // The lexeme is considered numeric only if the entire string was consumed.
    // If there is a trailing letter (for example, "12fps"), then it is an
    // identifier, not a malformed number.
    //
    return i == s.size ();
  }

  // Lex the provided source string into a sequence of tokens.
  //
  // We iterate through the source using our cursor, skipping whitespace and
  // dispatching to the appropriate scanner based on the current character.
  //
  lex_result
  lex (string_view source)
  {
    lex_result result;
    cursor c (source);

    // A simple helper to slice out the exact source text for a given span.
    //
    auto slice = [source] (source_span s) -> string
    {
      return string (source.substr (s.begin, s.end - s.begin));
    };

    while (!c.at_end ())
    {
      const char ch (c.peek ());

      // Skip any ASCII whitespace.
      //
      if (ascii_is_space (ch))
      {
        c.advance ();
        continue;
      }

      // Handle quoted strings. We slice the raw text, including the quotes,
      // into the lexeme.
      //
      if (ch == '"' || ch == '\'')
      {
        token t (scan_string (c, result.diags));

        // Raw text including quotes.
        //
        t.lexeme = slice (t.span);
        result.tokens.push_back (move (t));
        continue;
      }

      // Handle the semicolon, which acts as our structural separator.
      //
      if (ch == ';')
      {
        const source_offset begin (c.offset ());
        c.advance ();

        token t;
        t.kind = token_kind::separator;
        t.span = make_span (begin, c.offset ());
        t.lexeme = ";";
        t.value = ";";
        result.tokens.push_back (move (t));
        continue;
      }

      // Handle bare words.
      //
      if (is_word_byte (static_cast<unsigned char> (ch)))
      {
        token t (scan_word (c));

        // Since the word scanner lacks the source view, it leaves the lexeme
        // and value empty. We fill them here from the span, and then refine
        // the token kind based on whether the word looks like a number.
        //
        t.lexeme = slice (t.span);
        t.value = t.lexeme;
        t.kind = is_number_lexeme (t.lexeme) ? token_kind::number
                                             : token_kind::identifier;
        result.tokens.push_back (move (t));
        continue;
      }

      // Handle invalid characters. We slice the single offending byte for
      // display in our diagnostics.
      //
      token t (scan_invalid (c, result.diags));
      t.lexeme = slice (t.span);
      result.tokens.push_back (move (t));
    }

    // Finally, terminate the token stream with a zero-width end-of-input
    // marker exactly at the end position.
    //
    token eof;
    eof.kind = token_kind::end_of_input;
    eof.span = make_span (c.offset (), c.offset ());
    result.tokens.push_back (move (eof));

    return result;
  }
}
