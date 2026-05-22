#include <libiw4x/console/input.hxx>

#include <algorithm>
#include <utility>

#include <libiw4x/console/utility.hxx>

using namespace std;

namespace iw4x::console
{
  pair<size_t, size_t> input_buffer::
  selection () const noexcept
  {
    // Return the start and end indices of the current selection. If we don't
    // have an active selection, both boundaries simply collapse to the cursor.
    //
    if (!anchor_.has_value () || *anchor_ == cursor_)
      return {cursor_, cursor_};

    return {min (*anchor_, cursor_), max (*anchor_, cursor_)};
  }

  string input_buffer::
  selected_text () const
  {
    // Extract the substring that corresponds to the active selection.
    //
    const auto [lo, hi] (selection ());
    return text_.substr (lo, hi - lo);
  }

  void input_buffer::
  place_cursor (size_t pos, bool select)
  {
    pos = min (pos, text_.size ());

    if (select)
    {
      // If we are starting a new selection, we anchor it at the previous
      // cursor position before moving.
      //
      if (!anchor_.has_value ())
        anchor_ = cursor_;
    }
    else
      anchor_.reset ();

    cursor_ = pos;
  }

  void input_buffer::
  insert (string_view s)
  {
    if (has_selection ())
    {
      // Overwrite the selected text with the new insertion.
      //
      const auto [lo, hi] (selection ());
      text_.erase (lo, hi - lo);
      cursor_ = lo;
    }

    anchor_.reset ();

    // We need to respect the maximum buffer length. Note that we truncate the
    // inserted run rather than rejecting it wholesale. This matches the
    // behavior of a typical input field that simply stops accepting text once
    // it becomes full.
    //
    const size_t room (text_.size () < max_length ? max_length - text_.size () : 0);
    const string_view clipped (s.substr (0, min (s.size (), room)));

    text_.insert (cursor_, clipped);
    cursor_ += clipped.size ();
  }

  void input_buffer::
  backspace ()
  {
    if (has_selection ())
    {
      // A backspace with an active selection simply erases the selection.
      //
      const auto [lo, hi] (selection ());
      text_.erase (lo, hi - lo);
      cursor_ = lo;
    }
    else if (cursor_ > 0)
    {
      // Otherwise, we just erase the character preceding the
      // cursor.
      //
      text_.erase (cursor_ - 1, 1);
      --cursor_;
    }

    anchor_.reset ();
  }

  void input_buffer::
  erase_forward ()
  {
    if (has_selection ())
    {
      const auto [lo, hi] (selection ());
      text_.erase (lo, hi - lo);
      cursor_ = lo;
    }
    else if (cursor_ < text_.size ())
    {
      // Erase the character at the cursor.
      //
      text_.erase (cursor_, 1);
    }

    anchor_.reset ();
  }

  void input_buffer::
  move_left (bool select)
  {
    // Note that if we move left without actively selecting while a selection
    // is already present, we simply collapse the selection to its start.
    //
    if (!select && has_selection ())
    {
      cursor_ = selection ().first;
      anchor_.reset ();
      return;
    }

    place_cursor (cursor_ > 0 ? cursor_ - 1 : 0, select);
  }

  void input_buffer::
  move_right (bool select)
  {
    // Similar to moving left, moving right without selecting collapses the
    // selection to its end.
    //
    if (!select && has_selection ())
    {
      cursor_ = selection ().second;
      anchor_.reset ();
      return;
    }

    place_cursor (cursor_ + 1, select);
  }

  size_t input_buffer::
  word_left_index () const noexcept
  {
    size_t p (cursor_);

    // Scan backwards to find the start of the current or previous word. We
    // skip trailing whitespace first, then the word characters themselves.
    //
    while (p > 0 &&  ascii_is_space (text_[p - 1])) --p;
    while (p > 0 && !ascii_is_space (text_[p - 1])) --p;

    return p;
  }

  size_t input_buffer::
  word_right_index () const noexcept
  {
    size_t p (cursor_);
    const size_t n (text_.size ());

    // Scan forwards to find the end of the current or next word. Note that we
    // skip any leading whitespace, then the word itself, landing exactly at the
    // word's end. This is the emacs forward-word convention. It mirrors
    // word_left_index () and makes forward word-kill delete a word without
    // swallowing the gap after it.
    //
    while (p < n &&  ascii_is_space (text_[p])) ++p;
    while (p < n && !ascii_is_space (text_[p])) ++p;

    return p;
  }

  void input_buffer::
  move_word_left (bool select)
  {
    place_cursor (word_left_index (), select);
  }

  void input_buffer::
  move_word_right (bool select)
  {
    place_cursor (word_right_index (), select);
  }

  void input_buffer::
  erase_range (size_t lo, size_t hi)
  {
    if (lo < hi)
    {
      text_.erase (lo, hi - lo);
      cursor_ = lo;
    }

    anchor_.reset ();
  }

  void input_buffer::
  erase_word_before ()
  {
    if (has_selection ())
    {
      const auto [lo, hi] (selection ());
      erase_range (lo, hi);
      return;
    }

    // Erase from the start of the previous word up to the cursor.
    //
    erase_range (word_left_index (), cursor_);
  }

  void input_buffer::
  erase_word_after ()
  {
    if (has_selection ())
    {
      const auto [lo, hi] (selection ());
      erase_range (lo, hi);
      return;
    }

    // Erase from the cursor up to the end of the next word.
    //
    erase_range (cursor_, word_right_index ());
  }

  void input_buffer::
  erase_to_start ()
  {
    erase_range (0, cursor_);
  }

  void input_buffer::
  erase_to_end ()
  {
    erase_range (cursor_, text_.size ());
  }

  void input_buffer::
  move_home (bool select)
  {
    place_cursor (0, select);
  }

  void input_buffer::
  move_end (bool select)
  {
    place_cursor (text_.size (), select);
  }

  void input_buffer::
  select_all ()
  {
    if (text_.empty ())
      return;

    anchor_ = 0;
    cursor_ = text_.size ();
  }

  void input_buffer::
  assign (string s)
  {
    text_ = move (s);
    cursor_ = text_.size ();
    anchor_.reset ();
  }

  void input_buffer::
  clear ()
  {
    text_.clear ();
    cursor_ = 0;
    anchor_.reset ();
  }

  void history::
  push (string line)
  {
    reset_navigation ();

    if (line.empty ())
      return;

    // De-duplicate the history. If the user re-enters an existing command, we
    // move it to the most-recent slot.
    //
    auto it (find (entries_.begin (), entries_.end (), line));
    if (it != entries_.end ())
      entries_.erase (it);

    entries_.push_back (move (line));

    // Maintain the maximum history size. If we exceed it, drop the oldest
    // entry at the front.
    //
    if (entries_.size () > max_entries)
      entries_.erase (entries_.begin ());
  }

  optional<string> history::
  previous (string_view current)
  {
    if (entries_.empty ())
      return nullopt;

    if (!cursor_.has_value ())
    {
      // Save the currently unsubmitted live line so we can restore it later
      // if the user navigates back to the present.
      //
      live_ = string (current);
      cursor_ = entries_.size ();
    }

    // If we are already at the oldest entry, there is nowhere else to go.
    //
    if (*cursor_ == 0)
      return nullopt;

    --*cursor_;
    return entries_[*cursor_];
  }

  optional<string> history::
  next ()
  {
    if (!cursor_.has_value ())
      return nullopt;

    if (*cursor_ + 1 < entries_.size ())
    {
      ++*cursor_;
      return entries_[*cursor_];
    }

    // If we move past the newest entry, we restore the saved live line and
    // drop out of history navigation entirely.
    //
    cursor_.reset ();
    return live_;
  }

  void history::
  reset_navigation () noexcept
  {
    cursor_.reset ();
  }

  void screen::
  print (string_view text)
  {
    for (char c : text)
    {
      if (c == '\n')
        lines_.emplace_back ();
      else
        lines_.back ().push_back (c);
    }

    // If we exceed the maximum allowed lines, we erase the oldest ones from the
    // top.
    //
    if (lines_.size () > max_lines)
      lines_.erase (lines_.begin (),
                    lines_.begin () +
                      static_cast<ptrdiff_t> (lines_.size () - max_lines));

    if (pinned_to_bottom_)
      scroll_ = 0;
  }

  void screen::
  clear ()
  {
    lines_.assign (1, string {});
    scroll_ = 0;
    pinned_to_bottom_ = true;
  }

  size_t screen::
  max_scroll () const noexcept
  {
    // Note that before the first frame, the viewport height is unknown. We fall
    // back to the line count so that scrolling is not wedged at zero. Once the
    // height is known, the useful scroll maximum is simply the total backlog
    // minus one screenful.
    //
    if (visible_rows_ == 0)
      return lines_.empty () ? 0 : lines_.size () - 1;

    return lines_.size () > visible_rows_ ? lines_.size () - visible_rows_ : 0;
  }

  void screen::
  set_visible_rows (size_t rows) noexcept
  {
    visible_rows_ = rows;

    // Clamp the current scroll position against the newly calculated maximum.
    //
    scroll_ = min (scroll_, max_scroll ());
  }

  void screen::
  scroll_up (size_t rows)
  {
    pinned_to_bottom_ = false;
    scroll_ = min (scroll_ + rows, max_scroll ());
  }

  void screen::
  scroll_down (size_t rows)
  {
    if (scroll_ <= rows)
    {
      scroll_ = 0;
      pinned_to_bottom_ = true;
    }
    else
      scroll_ -= rows;
  }
}
