#include <libiw4x/console/types.hxx>

#include <cassert>
#include <utility>

using namespace std;

namespace iw4x::console
{
  line_column
  resolve_line_column (string_view source, source_offset offset) noexcept
  {
    const size_t limit (source.size ());

    // Clamp the offset into range.
    //
    // Notice that a diagnostic span may legitimately point one past the end
    // (the end_of_input position). We never want this helper to read out of
    // bounds when rendering it, so we bound the target index against the actual
    // size of the source.
    //
    const size_t target (offset < limit ? offset : limit);

    line_column lc {};

    for (size_t i (0); i < target; ++i)
    {
      if (source[i] == '\n')
      {
        ++lc.line;
        lc.column = 1;
      }
      else
        ++lc.column;
    }

    return lc;
  }

  string_view
  to_string (token_kind k) noexcept
  {
    switch (k)
    {
      case token_kind::identifier:          return "identifier";
      case token_kind::number:              return "number";
      case token_kind::string:              return "string";
      case token_kind::separator:           return "separator";
      case token_kind::end_of_input:        return "end-of-input";
      case token_kind::invalid:             return "invalid";
      case token_kind::unterminated_string: return "unterminated-string";
    }

    CONSOLE_UNREACHABLE ();
  }

  bool name::
  equals (const name& other) const noexcept
  {
    // First do a quick reject based on length.
    //
    if (text_.size () != other.text_.size ())
      return false;

    // If the lengths match, perform a byte-by-byte case-insensitive
    // comparison.
    //
    for (size_t i (0); i < text_.size (); ++i)
      if (ascii_to_lower (text_[i]) != ascii_to_lower (other.text_[i]))
        return false;

    return true;
  }

  size_t name::
  hash () const noexcept
  {
    // We compute FNV-1a over the case-folded bytes.
    //
    // Notice that folding here is exactly what makes the hash agree with
    // equals(). That is, two names that differ only in case must inevitably
    // land in the same bucket.
    //
    size_t h (1469598103934665603ull);

    for (char c: text_)
    {
      h ^= static_cast<unsigned char> (ascii_to_lower (c));
      h *= 1099511628211ull;
    }

    return h;
  }

  argument_value argument_value::
  of_string (string v, source_span s)
  {
    argument_value a;
    a.kind_ = value_kind::string;
    a.span_ = s;
    a.string_ = move (v);
    return a;
  }

  argument_value argument_value::
  of_integer (int64_t v, source_span s) noexcept
  {
    argument_value a;
    a.kind_ = value_kind::integer;
    a.span_ = s;
    a.integer_ = v;
    return a;
  }

  argument_value argument_value::
  of_floating (double v, source_span s) noexcept
  {
    argument_value a;
    a.kind_ = value_kind::floating;
    a.span_ = s;
    a.floating_ = v;
    return a;
  }

  argument_value argument_value::
  of_boolean (bool v, source_span s) noexcept
  {
    argument_value a;
    a.kind_ = value_kind::boolean;
    a.span_ = s;
    a.boolean_ = v;
    return a;
  }

  const string& argument_value::
  as_string () const noexcept
  {
    assert (kind_ == value_kind::string);
    return string_;
  }

  int64_t argument_value::
  as_integer () const noexcept
  {
    assert (kind_ == value_kind::integer);
    return integer_;
  }

  double argument_value::
  as_floating () const noexcept
  {
    assert (kind_ == value_kind::floating);
    return floating_;
  }

  bool argument_value::
  as_boolean () const noexcept
  {
    assert (kind_ == value_kind::boolean);
    return boolean_;
  }

  string argument_value::
  to_text () const
  {
    switch (kind_)
    {
      case value_kind::string:   return string_;
      case value_kind::integer:  return format ("{}", integer_);
      case value_kind::floating: return format ("{}", floating_);
      case value_kind::boolean:  return boolean_ ? "1" : "0";
    }

    CONSOLE_UNREACHABLE ();
  }

  string_view
  to_string (value_kind k) noexcept
  {
    switch (k)
    {
    case value_kind::string:   return "string";
    case value_kind::integer:  return "integer";
    case value_kind::floating: return "floating";
    case value_kind::boolean:  return "boolean";
    }

    CONSOLE_UNREACHABLE ();
  }

  string_view
  to_string (completion_kind k) noexcept
  {
    switch (k)
    {
      case completion_kind::command:  return "command";
      case completion_kind::dvar:     return "dvar";
      case completion_kind::alias:    return "alias";
      case completion_kind::argument: return "argument";
    }

    CONSOLE_UNREACHABLE ();
  }

  string_view
  to_string (diagnostic_severity s) noexcept
  {
    switch (s)
    {
      case diagnostic_severity::note:    return "note";
      case diagnostic_severity::warning: return "warning";
      case diagnostic_severity::error:   return "error";
      case diagnostic_severity::fatal:   return "fatal";
    }

    CONSOLE_UNREACHABLE ();
  }

  string_view
  to_string (diagnostic_category c) noexcept
  {
    switch (c)
    {
      case diagnostic_category::lexical:      return "lexical";
      case diagnostic_category::syntax:       return "syntax";
      case diagnostic_category::declaration:  return "declaration";
      case diagnostic_category::invocation:   return "invocation";
      case diagnostic_category::completion:   return "completion";
      case diagnostic_category::dvar_binding: return "dvar-binding";
      case diagnostic_category::abi:          return "abi";
      case diagnostic_category::internal:     return "internal";
    }

    CONSOLE_UNREACHABLE ();
  }

  string
  code_string (diagnostic_code c) noexcept
  {
    if (c == diagnostic_code::none)
      return "CON-0000";

    return format ("CON-{:04}", static_cast<uint16_t> (c));
  }

  string diagnostic::
  summary () const
  {
    return format ("{}[{}] {}: {}",
                        to_string (severity),
                        code_string (code),
                        to_string (category),
                        message);
  }

  void diagnostics::
  add (diagnostic d)
  {
    if (d.severity == diagnostic_severity::error ||
        d.severity == diagnostic_severity::fatal)
      ++error_count_;

    entries_.push_back (move (d));
  }

  void diagnostics::
  merge (diagnostics other)
  {
    error_count_ += other.error_count_;

    // If our entry list is empty, we can just steal the other's vector
    // directly. Otherwise, we must append them using move iterators.
    //
    if (entries_.empty ())
      entries_ = move (other.entries_);
    else
      entries_.insert (entries_.end (),
                       make_move_iterator (other.entries_.begin ()),
                       make_move_iterator (other.entries_.end ()));
  }
}
