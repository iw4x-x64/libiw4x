#pragma once

#include <cstddef>
#include <cstdint>
#include <compare>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <libiw4x/console/utility.hxx>

namespace iw4x::console
{
  // Byte offset into the original console input.
  //
  // The idea is that since the console line is bounded well under 2^32 bytes, a
  // 32-bit offset is ample and keeps our spans cheap to copy and store. Note
  // that offsets are measured in bytes, not codepoints. The console language is
  // ASCII, so the two coincide for valid input. For invalid bytes, however, we
  // still need an unambiguous position.
  //
  using source_offset = std::uint32_t;

  // Half-open range [begin, end) over the source.
  //
  // The invariant here is begin <= end. We treat an empty span (begin == end)
  // as a zero-width position. This is quite handy for pointing at insertion
  // points or at the exact end-of-input. Think of spans as pure values: they
  // are produced by the lexer, threaded through the parser onto every node, and
  // surfaced in diagnostics so a message can point right at the offending text.
  //
  struct source_span
  {
    source_offset begin {0};
    source_offset end {0};

    // Default-construct an empty span pointing at the start of the input.
    //
    constexpr
    source_span () noexcept = default;

    // Construct a span from explicit boundaries.
    //
    // Note that we cannot use assert() inside a constexpr constructor without
    // giving up constant evaluation on the happy path. Because of this, the
    // begin <= end invariant is checked by the lexer at the point of
    // construction instead.
    //
    constexpr explicit
    source_span (source_offset b, source_offset e) noexcept
      : begin (b), end (e) {}

    // Return the length of the span in bytes.
    //
    constexpr source_offset
    length () const noexcept
    {
      return end - begin;
    }

    // Check if the span is zero-width.
    //
    constexpr bool
    empty () const noexcept
    {
      return begin == end;
    }

    // Check if a specific byte offset falls within this span.
    //
    constexpr bool
    contains (source_offset o) const noexcept
    {
      return o >= begin && o < end;
    }

    // Calculate the smallest span covering both operands.
    //
    // This is typically used when a parser node spans several tokens. We merge
    // the first and last token spans to get the node's total extent.
    //
    constexpr source_span
    merge (source_span other) const noexcept
    {
      return source_span (begin < other.begin ? begin : other.begin,
                          end > other.end ? end : other.end);
    }

    // Check for exact span equality.
    //
    friend constexpr bool
    operator == (source_span, source_span) noexcept = default;
  };

  // 1-based line and column projection.
  //
  // Internally, diagnostics are reported against flat byte offsets. This
  // struct is strictly the presentation-time conversion for human-facing
  // output. Computing it is O(offset) in the worst case, which is completely
  // irrelevant for single console lines.
  //
  struct line_column
  {
    std::uint32_t line {1};
    std::uint32_t column {1};
  };

  // Resolve a byte offset into a human-readable line and column.
  //
  line_column
  resolve_line_column (std::string_view source, source_offset offset) noexcept;

  // Lexical categories emitted by the lexer.
  //
  // Note that the lexer never tries to emit a "best guess" or recover from
  // bad input. Ambiguous or malformed input simply becomes an error token
  // (like invalid or unterminated_string) carrying its own diagnostic. It
  // is up to the parsers downstream to switch over these and handle them.
  //
  enum class token_kind : std::uint8_t
  {
    // Bare word.
    //
    // Command names, dvar names, and unquoted arguments all begin life as
    // identifiers. Disambiguating between a command and a dvar is strictly
    // the parser's and resolver's job, not the lexer's.
    //
    identifier,

    // Numeric literal (integer or floating point).
    //
    // The lexer recognizes the shape, but the parser actually performs the
    // typed conversion into an argument_value.
    //
    number,

    // Quoted string literal.
    //
    // The token's span covers the quotes, and the decoded value (with
    // escapes properly resolved) is carried alongside it.
    //
    string,

    // Structural separators.
    //
    // Reserved for future command composition. For example, using ';' to
    // sequence commands. We emit these as distinct tokens so the grammar
    // can evolve without needing a re-lex.
    //
    separator,

    // End of input.
    //
    // This is always the final token. It carries a zero-width span at the
    // input length so diagnostics can point "just past the end".
    //
    end_of_input,

    // Invalid byte that cannot begin any valid token.
    //
    // This is always accompanied by a lexical diagnostic.
    //
    invalid,

    // String literal whose closing quote is missing.
    //
    // Accompanied by a lexical diagnostic. The span runs all the way to
    // the end-of-input.
    //
    unterminated_string,
  };

  // Convert the token kind to a string representation for debugging.
  //
  std::string_view
  to_string (token_kind) noexcept;

  // A single lexeme produced by the lexer.
  //
  // The lexeme member holds the literal text exactly as it appeared in the
  // source. For strings, value holds the decoded text (with escapes processed),
  // whereas lexeme includes the surrounding quotes. For everything else, value
  // is just a copy of lexeme. We keep both so the parser can work with clean
  // values while diagnostics can still quote the raw text back to the user.
  //
  struct token
  {
    token_kind  kind   {token_kind::invalid};
    source_span span   {};
    std::string lexeme {};
    std::string value  {};
  };

  // Case-insensitive identity for engine-style names.
  //
  // Both command and dvar names compare case-insensitively to match the
  // engine's behavior. Here we capture the semantics once in this strong type.
  // The stored text preserves the original casing for display purposes, but
  // equality and hashing fold to lower case behind the scenes.
  //
  class name
  {
  public:
    // Default-construct an empty name.
    //
    name () = default;

    // Construct a name from a string.
    //
    explicit
    name (std::string text) noexcept
      : text_ (std::move (text)) {}

    // Access the preserved, original-case string.
    //
    const std::string&
    str () const noexcept
    {
      return text_;
    }

    // Check if the name is empty.
    //
    bool
    empty () const noexcept
    {
      return text_.empty ();
    }

    // Perform a case-insensitive equality check.
    //
    bool
    equals (const name& other) const noexcept;

    // Compute a case-insensitive hash for table lookups.
    //
    std::size_t
    hash () const noexcept;

    // Equality operator wrapping the equals() method.
    //
    friend bool
    operator == (const name& a, const name& b) noexcept
    {
      return a.equals (b);
    }

  private:
    std::string text_;
  };

  // Distinct strong types for different name flavors.
  //
  // We use distinct strong types for command and dvar names. Even though they
  // share the same underlying case-insensitive string logic, passing a dvar
  // name to a function expecting a command name is a logic error. We want
  // the compiler to catch this for us.
  //
  class command_name: public name
  {
  public:
    using name::name;
  };

  class dvar_name: public name
  {
  public:
    using name::name;
  };

  // Opaque table handles.
  //
  // command_id and declaration_id act as indices into the immutable command and
  // declaration tables. We use strong integer wrappers so a raw size_t can
  // never be mistaken for a handle, and so a handle from one table cannot
  // accidentally be used against another. A default-constructed id represents
  // the reserved "none" value.
  //
  enum class command_id: std::uint32_t {none = 0};
  enum class declaration_id: std::uint32_t {none = 0};

  // Kind tag for a parsed literal argument.
  //
  enum class value_kind : std::uint8_t
  {
    string,
    integer,
    floating,
    boolean,
  };

  // Convert the value kind to a string representation.
  //
  std::string_view
  to_string (value_kind) noexcept;

  // Typed argument value alongside its originating span.
  //
  // The parser produces these from raw tokens, and command binding consumes
  // them. The idea is to store the value in its decoded native form (rather
  // than as text) so that command handlers receive typed input and don't have
  // to re-parse strings. The attached span allows a binding-time diagnostic
  // (for example, "expected integer") to point back exactly at the offending
  // argument.
  //
  class argument_value
  {
  public:
    // Factory methods for each supported type.
    //
    static argument_value
    of_string (std::string v, source_span s);

    static argument_value
    of_integer (std::int64_t v, source_span s) noexcept;

    static argument_value
    of_floating (double v, source_span s) noexcept;

    static argument_value
    of_boolean (bool v, source_span s) noexcept;

    // Get the active value kind.
    //
    value_kind
    kind () const noexcept
    {
      return kind_;
    }

    // Get the source span where this argument originated.
    //
    source_span
    span () const noexcept
    {
      return span_;
    }

    // Typed accessors.
    //
    // Each of these asserts the active kind internally. Reading the wrong
    // member is a programmer error, not a recoverable condition at runtime.
    // Callers must check kind() first (or know it from the binding context).
    //
    const std::string&
    as_string () const noexcept;

    std::int64_t
    as_integer () const noexcept;

    double
    as_floating () const noexcept;

    bool
    as_boolean () const noexcept;

    // Retrieve the original source text of the argument, regardless of kind.
    //
    // This is always available and is primarily used when forwarding to the
    // engine command buffer, which operates exclusively on text.
    //
    std::string
    to_text () const;

  private:
    value_kind kind_      {value_kind::string};
    source_span span_     {};

    std::string string_   {};
    std::int64_t integer_ {0};
    double floating_      {0.0};
    bool boolean_         {false};
  };

  // What a completion candidate actually refers to.
  //
  // The kind drives both the presentation (like icons or annotations at the
  // integration boundary) and the source of the suggestion. The completion
  // subsystem keeps these stable so callers can reliably branch on them.
  //
  enum class completion_kind: std::uint8_t
  {
    command,
    dvar,
    alias,
    argument,
  };

  // Convert the completion kind to a string representation.
  //
  std::string_view
  to_string (completion_kind) noexcept;

  // Severity level for a diagnostic.
  //
  // A note is purely informational and is typically attached to another
  // parent diagnostic. A warning denotes a recoverable concern. An error
  // denotes a failure that aborts the current operation (lex, parse, bind)
  // but still leaves the subsystem healthy. A fatal denotes a condition
  // the subsystem simply cannot continue past.
  //
  enum class diagnostic_severity : std::uint8_t
  {
    note,
    warning,
    error,
    fatal,
  };

  // Convert the diagnostic severity to a string representation.
  //
  std::string_view
  to_string (diagnostic_severity) noexcept;

  // Which subsystem produced a diagnostic.
  //
  // The category, combined with the stable code, lets integrators route and
  // filter diagnostics without having to parse error message strings.
  //
  enum class diagnostic_category : std::uint8_t
  {
    lexical,
    syntax,
    declaration,
    invocation,
    completion,
    dvar_binding,
    abi,
    internal,
  };

  // Convert the diagnostic category to a string representation.
  //
  std::string_view
  to_string (diagnostic_category) noexcept;

  // Stable, machine-readable diagnostic identifier.
  //
  // Codes never change meaning once assigned and never get reused. This
  // means external tooling and tests can pin their behavior to a code rather
  // than relying on fragile message text. The numeric value encodes the
  // category in the high digits (lexical = 1xxx, syntax = 2xxx, etc.) as an
  // organizing convention.
  //
  enum class diagnostic_code : std::uint16_t
  {
    none = 0,

    // Lexical (1xxx).
    //
    lex_invalid_character = 1001,
    lex_unterminated_string = 1002,
    lex_invalid_escape = 1003,

    // Syntax (2xxx).
    //
    syn_unexpected_token = 2001,
    syn_expected_argument = 2002,
    syn_empty_input = 2003,

    // Declaration (3xxx).
    //
    decl_duplicate_name = 3001,
    decl_bad_signature = 3002,
    decl_invalid_alias = 3003,

    // Invocation (4xxx).
    //
    inv_unknown_command = 4001,
    inv_arity_mismatch = 4002,
    inv_argument_type = 4003,

    // Completion (5xxx).
    //
    cmp_no_candidates = 5001,
    cmp_backend_failure = 5002,

    // Dvar binding (6xxx).
    //
    dvar_not_found = 6001,
    dvar_type_mismatch = 6002,
    dvar_domain_violation = 6003,

    // ABI (7xxx).
    //
    abi_registration_failed = 7001,
    abi_null_engine_pointer = 7002,

    // Internal (9xxx).
    //
    internal_invariant = 9001,
  };

  // Get the string representation of a diagnostic code (e.g., "CON-1001").
  //
  std::string
  code_string (diagnostic_code) noexcept;

  // Subordinate explanation attached to a primary diagnostic.
  //
  // Notes carry secondary context and are optionally anchored at their own
  // span (for example, "previous declaration was here"). They never stand
  // alone as top-level diagnostics.
  //
  struct diagnostic_note
  {
    std::string message;
    std::optional<source_span> span;
  };

  // Structured diagnostic report.
  //
  // Think of this as the single currency for reporting trouble across the
  // entire module. The lexer, parser, declaration validator, command binder,
  // and completion engine all produce these. Notice that none of them perform
  // logging directly. Logging happens exactly once at the integration boundary,
  // ensuring the semantic layers remain testable and entirely free of I/O.
  //
  // The required fields are severity, category, code, and message. Everything
  // else is optional context used to sharpen the report when available.
  //
  struct diagnostic
  {
    diagnostic_severity severity {diagnostic_severity::error};
    diagnostic_category category {diagnostic_category::internal};
    diagnostic_code     code     {diagnostic_code::none};
    std::string         message;

    std::optional<std::string>   detail;
    std::optional<source_span>   span;
    std::vector<diagnostic_note> notes;
    std::optional<std::string>   recovery_hint;

    // Render a single-line summary of the diagnostic.
    //
    // The format is "error[CON-1001] lexical: <message>", which is suitable
    // for logging at the boundary. The full structure (notes, spans, detail)
    // remains available for richer UI presentation.
    //
    std::string
    summary () const;
  };

  // Collection of diagnostics.
  //
  // Passes like the lexer or parser can generate multiple diagnostics at
  // once. We track whether any of them are errors (or worse) so the caller
  // can just check has_errors() instead of scanning the whole vector. Note
  // that warnings do not flip this flag.
  //
  class diagnostics
  {
  public:
    // Add a new diagnostic to the collection.
    //
    void
    add (diagnostic d);

    // Check if the collection is entirely empty.
    //
    bool
    empty () const noexcept
    {
      return entries_.empty ();
    }

    // Get the total number of diagnostics stored.
    //
    std::size_t
    size () const noexcept
    {
      return entries_.size ();
    }

    // Check if the collection contains any error or fatal diagnostics.
    //
    bool
    has_errors () const noexcept
    {
      return error_count_ != 0;
    }

    // Access the underlying diagnostic entries.
    //
    const std::vector<diagnostic>&
    entries () const noexcept
    {
      return entries_;
    }

    // Merge another collection of diagnostics into this one.
    //
    void
    merge (diagnostics other);

  private:
    std::vector<diagnostic> entries_;
    std::size_t error_count_ = 0;
  };

  // Console invariant violation.
  //
  // This signals a defect in our own code. A precondition or postcondition
  // that should be impossible to break was somehow broken. Prefer assert()
  // on hot paths; throw this only where a check must survive in release builds.
  //
  struct invariant_violation: std::runtime_error
  {
    explicit
    invariant_violation (const std::string& what)
      : std::runtime_error (std::format ("console invariant violated: {}",
                                         what)) {}
  };

  // Command or declaration registration failure.
  //
  struct registration_error: std::runtime_error
  {
    std::string subject;

    explicit
    registration_error (const std::string& name, const std::string& what)
      : std::runtime_error (std::format ("console registration of '{}' failed: {}",
                                         name, what)),
        subject (name) {}
  };

  // Interaction with the engine ABI failed.
  //
  // This might occur if a required engine pointer was null, or a registration
  // slot was unavailable. The boundary catches these, logs them, and degrades
  // the subsystem gracefully.
  //
  struct abi_error: std::runtime_error
  {
    explicit
    abi_error (const std::string& what)
      : std::runtime_error (std::format ("console ABI error: {}",
                                         what)) {}
  };
}
