#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace iw4x::console
{
  // Editable input line.
  //
  // It holds the text, a cursor position (which is a byte offset in the [0,
  // size] range), and an optional selection anchor. Note that editing
  // operations keep the cursor in range and either collapse or extend the
  // selection depending on the select flag. This matches the behavior of a
  // conventional text field. We also cap the line length to keep a single
  // command bounded.
  //
  class input_buffer
  {
  public:
    static constexpr std::size_t max_length = 256;

    const std::string&
    text () const noexcept
    {
      return text_;
    }

    std::size_t
    cursor () const noexcept
    {
      return cursor_;
    }

    bool
    empty () const noexcept
    {
      return text_.empty ();
    }

    bool
    has_selection () const noexcept
    {
      return anchor_.has_value () && *anchor_ != cursor_;
    }

    // Return the selection as a half-open [lo, hi) byte range.
    //
    // Note that the range is empty when there is no selection.
    //
    std::pair<std::size_t, std::size_t>
    selection () const noexcept;

    // Return the currently selected text, if any.
    //
    std::string
    selected_text () const;

    // Insert text at the current cursor position, replacing any existing
    // selection.
    //
    // Note that we truncate the result at max_length. Also note that
    // non-printable bytes are the caller's concern (the character event path is
    // expected to filter them out).
    //
    void
    insert (std::string_view s);

    // Delete the character (byte) before or after the cursor.
    //
    // If a selection exists, we delete the selection instead.
    //
    void
    backspace ();

    void
    erase_forward ();

    // Cursor movement.
    //
    // The select flag dictates whether we extend the selection from the
    // existing anchor. If it is false, the selection collapses.
    //
    void
    move_left (bool select);

    void
    move_right (bool select);

    void
    move_word_left (bool select);

    void
    move_word_right (bool select);

    void
    move_home (bool select);

    void
    move_end (bool select);

    void
    select_all ();

    // Kill operations (from the readline/emacs family).
    //
    // Each operation removes a span of text and places the cursor at the cut
    // point, dropping any prior selection. Note that if a selection exists, the
    // word and line kills act on the selection instead. This keeps them
    // intuitive when text is highlighted by the user.
    //
    void
    erase_word_before (); // Ctrl+W: Delete the word to the left.

    void
    erase_word_after ();  // Alt+D: Delete the word to the right.

    void
    erase_to_start ();    // Ctrl+U: Delete from the cursor to the line start.

    void
    erase_to_end ();      // Ctrl+K: Delete from the cursor to the line end.

    // Replace the entire input line.
    //
    // We use this for history navigation and auto-completion. It places the
    // cursor at the end of the new string and clears any active selection.
    //
    void
    assign (std::string s);

    void
    clear ();

  private:
    // Place the cursor at the specified position, optionally updating the
    // selection anchor.
    //
    void
    place_cursor (std::size_t pos, bool select);

    // Get the offset one word to the left or right of the cursor.
    //
    // We use the same whitespace-delimited rule as the word-movement
    // operations. This is factored out so that both movement and word-kill
    // operations remain strictly consistent.
    //
    std::size_t
    word_left_index () const noexcept;

    std::size_t
    word_right_index () const noexcept;

    // Erase the [lo, hi) range and place the cursor at lo, clearing the
    // selection.
    //
    void
    erase_range (std::size_t lo, std::size_t hi);

    std::string text_;
    std::size_t cursor_ {0};
    std::optional<std::size_t> anchor_;
  };

  // Command history.
  //
  // We store recently entered lines newest-last and de-duplicated (so
  // re-entering a line moves it to the front rather than adding a redundant
  // copy). The history is capped at a fixed maximum size. Navigation walks from
  // the most recent entry backwards. We also preserve the live (unsubmitted)
  // line so that returning past the newest entry successfully restores it.
  //
  class history
  {
  public:
    static constexpr std::size_t max_entries = 512;

    // Push a newly entered line into the history.
    //
    void
    push (std::string line);

    std::size_t
    size () const noexcept
    {
      return entries_.size ();
    }

    // Move to the previous (older) entry.
    //
    // The current argument is the text presently in the input line. On the
    // first call, we remember it as the live line. Returns the text to display,
    // or nullopt if we are already at the oldest entry.
    //
    std::optional<std::string>
    previous (std::string_view current);

    // Move to the next (newer) entry.
    //
    // This eventually returns the saved live line once we reach the front.
    // Returns nullopt if we are not currently navigating.
    //
    std::optional<std::string>
    next ();

    // Stop navigating.
    //
    // This is typically called when the line is manually edited or submitted.
    //
    void
    reset_navigation () noexcept;

    bool
    navigating () const noexcept
    {
      return cursor_.has_value ();
    }

  private:
    std::vector<std::string> entries_;

    // Index into the entries array while navigating.
    //
    std::optional<std::size_t> cursor_;

    // Saved unsubmitted line.
    //
    std::string live_;
  };

  // Scrollable output log.
  //
  // This accumulates printed text split into logical lines, capped at a maximum
  // number of lines. We maintain a scroll offset that counts lines up from the
  // bottom. Note that new output while scrolled does not move the view unless
  // it is explicitly pinned to the bottom.
  //
  class screen
  {
  public:
    static constexpr std::size_t max_lines = 1024;

    // Append text to the log, splitting on newlines.
    //
    // A trailing partial line is held and extended by the next print. This
    // perfectly matches the behavior of streamed engine output.
    //
    void
    print (std::string_view text);

    void
    clear ();

    const std::vector<std::string>&
    lines () const noexcept
    {
      return lines_;
    }

    std::size_t
    scroll () const noexcept
    {
      return scroll_;
    }

    void
    scroll_up (std::size_t rows);

    void
    scroll_down (std::size_t rows);

    void
    scroll_to_bottom () noexcept
    {
      scroll_ = 0;
      pinned_to_bottom_ = true;
    }

    // Inform the screen how many lines the viewport currently shows.
    //
    // The screen alone cannot derive this (it strictly depends on the font and
    // window size), so the renderer supplies it each frame. This allows us to
    // bound scrolling to the useful range. Without it, the scroll offset could
    // run past the top, detaching the scrollbar thumb from the visible content.
    //
    void
    set_visible_rows (std::size_t rows) noexcept;

    // Return the largest meaningful scroll offset.
    //
    // This is calculated given the current backlog and viewport (total lines
    // minus a screenful). It returns zero when everything fits naturally or if
    // the viewport size is not yet known.
    //
    std::size_t
    max_scroll () const noexcept;

  private:
    std::vector<std::string> lines_ {std::string {}};
    std::size_t scroll_ {0};
    std::size_t visible_rows_ {0};
    bool pinned_to_bottom_ {true};
  };
}
