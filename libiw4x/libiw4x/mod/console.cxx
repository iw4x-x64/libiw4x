#include <libiw4x/mod/console.hxx>

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <tracy/Tracy.hpp>

#include <libiw4x/console/abi.hxx>
#include <libiw4x/console/completion.hxx>
#include <libiw4x/console/core.hxx>
#include <libiw4x/console/input.hxx>
#include <libiw4x/console/render.hxx>
#include <libiw4x/console/utility.hxx>

#include <libiw4x/detour.hxx>
#include <libiw4x/import.hxx>
#include <libiw4x/logger.hxx>

using namespace std;

namespace iw4x::mod
{
  // TODO: This file is still doing too much. The module should eventually
  // become the composition point that installs the detours and wires the
  // console to the engine, while the actual console behaviour should live in
  // console/.
  //
  // In particular, input editing, history navigation, completion cycling, and
  // open/close policy belong in controller.*xx. The render-model projection,
  // completion popup rows, inline completion text, and detail panel shaping
  // belong in view.*xx. Keeping them here for now makes the transition easier,
  // but the split is important: mod/console.cxx should know when the engine
  // calls us, not how the console UI works.
  //
  namespace
  {
    // This is the one bit of geometry that both completion cycling and
    // rendering need to agree on. Keep it here rather than in the renderer: the
    // renderer should only draw the model it is given, not decide how the
    // interactive selection follows the visible window.
    //
    constexpr size_t popup_rows {10};
    constexpr size_t popup_scrolloff {4};
    constexpr size_t scroll_page_rows {3};

    struct completion_session
    {
      vector<console::completion_match> matches;
      size_t selected {0};
      size_t view {0};
      size_t base {0};
      bool cycling {false};
    };

    struct ui_state
    {
      bool open {false};
      bool output {true};
      console::input_buffer input;
      console::history hist;
      console::screen scr;
      completion_session comp;
    };

    struct modifiers
    {
      bool ctrl {false};
      bool shift {false};
      bool alt {false};
    };

    enum class action
    {
      none,
      submit,
      cancel,
      backspace,
      erase_forward,
      left,
      right,
      word_left,
      word_right,
      line_start,
      line_end,
      kill_word_back,
      kill_word_forward,
      kill_to_start,
      kill_to_end,
      clear_screen,
      history_prev,
      history_next,
      page_up,
      page_down,
      scroll_up,
      scroll_down,
      complete,
    };

    struct key_binding
    {
      int key;
      action act;
    };

    constexpr key_binding base_bindings [] {
      {K_ENTER,         action::submit},
      {K_KP_ENTER,      action::submit},
      {K_ESCAPE,        action::cancel},
      {K_BACKSPACE,     action::backspace},
      {8,               action::backspace},
      {K_DEL,           action::erase_forward},
      {K_LEFTARROW,     action::left},
      {K_KP_LEFTARROW,  action::left},
      {K_RIGHTARROW,    action::right},
      {K_KP_RIGHTARROW, action::right},
      {K_HOME,          action::line_start},
      {K_KP_HOME,       action::line_start},
      {K_END,           action::line_end},
      {K_KP_END,        action::line_end},
      {K_UPARROW,       action::history_prev},
      {K_KP_UPARROW,    action::history_prev},
      {K_DOWNARROW,     action::history_next},
      {K_KP_DOWNARROW,  action::history_next},
      {K_PGUP,          action::page_up},
      {K_KP_PGUP,       action::page_up},
      {K_PGDN,          action::page_down},
      {K_KP_PGDN,       action::page_down},
      {K_MWHEELUP,      action::scroll_up},
      {K_MWHEELDOWN,    action::scroll_down},
      {K_TAB,           action::complete},
    };

    ui_state&
    ui ()
    {
      static ui_state s;
      return s;
    }

    bool
    toggle_key (int k) noexcept
    {
      return k == '`' || k == '~';
    }

    bool
    printable (int k) noexcept
    {
      const unsigned u (static_cast<unsigned> (k));
      return u >= 0x20 && u <= 0x7E && !toggle_key (k);
    }

    int
    lower_key (int k) noexcept
    {
      return k >= 'A' && k <= 'Z' ? k - 'A' + 'a' : k;
    }

    bool
    tracked_key (int k) noexcept
    {
      return k >= 0 && k < 256;
    }

    action
    find_action (const key_binding* b, size_t n, int k) noexcept
    {
      for (size_t i (0); i != n; ++i)
      {
        if (b [i].key == k)
          return b [i].act;
      }

      return action::none;
    }

    action
    base_action (int k) noexcept
    {
      return find_action (base_bindings,
                          sizeof (base_bindings) / sizeof (base_bindings [0]),
                          k);
    }

    modifiers
    current_modifiers () noexcept
    {
      return {static_cast<bool> (playerKeys->keys [K_CTRL].down),
              static_cast<bool> (playerKeys->keys [K_SHIFT].down),
              static_cast<bool> (playerKeys->keys [K_ALT].down)};
    }

    action
    ctrl_action (int k) noexcept
    {
      if (k == K_LEFTARROW || k == K_KP_LEFTARROW)
        return action::word_left;

      if (k == K_RIGHTARROW || k == K_KP_RIGHTARROW)
        return action::word_right;

      switch (lower_key (k))
      {
        case 'a': return action::line_start;
        case 'e': return action::line_end;
        case 'b': return action::left;
        case 'f': return action::right;
        case 'd': return action::erase_forward;
        case 'w': return action::kill_word_back;
        case 'u': return action::kill_to_start;
        case 'k': return action::kill_to_end;
        case 'l': return action::clear_screen;
      }

      return action::none;
    }

    action
    alt_action (int k) noexcept
    {
      switch (lower_key (k))
      {
        case 'b': return action::word_left;
        case 'f': return action::word_right;
        case 'd': return action::kill_word_forward;
      }

      return action::none;
    }

    action
    resolve_action (int k, const modifiers& m) noexcept
    {
      // Modifiers form overlays on top of the base key map. This keeps the
      // engine key names out of the editing code and, perhaps more importantly,
      // keeps Emacs-style bindings from being scattered through the action
      // dispatcher below.
      //
      if (m.ctrl)
        return ctrl_action (k);

      if (m.alt)
        return alt_action (k);

      return base_action (k);
    }

    bool
    istarts_with (string_view s, string_view p) noexcept
    {
      if (p.size () > s.size ())
        return false;

      for (size_t i (0); i != p.size (); ++i)
      {
        if (console::ascii_to_lower (s [i]) != console::ascii_to_lower (p [i]))
          return false;
      }

      return true;
    }

    vector<string>
    split_lines (string_view s)
    {
      vector<string> r;
      string l;

      for (char c: s)
      {
        if (c == '\n')
        {
          r.push_back (move (l));
          l.clear ();
        }
        else if (c != '\r')
          l.push_back (c);
      }

      r.push_back (move (l));

      if (r.size () > 1 && r.back ().empty ())
        r.pop_back ();

      return r;
    }

    void
    reset_completion (completion_session& c)
    {
      c.matches.clear ();
      c.selected = 0;
      c.view = 0;
      c.base = 0;
      c.cycling = false;
    }

    void
    reset_cycle (completion_session& c)
    {
      c.selected = 0;
      c.view = 0;
      c.base = 0;
      c.cycling = false;
    }

    void
    follow_selection (completion_session& c)
    {
      c.view = console::scroll_view_offset (c.selected,
                                            c.matches.size (),
                                            popup_rows,
                                            popup_scrolloff,
                                            c.view);
    }

    void
    recompute_completion (ui_state& s)
    {
      // Any normal edit ends the explicit Tab cycle. We still keep the match
      // list live after the edit, but it once again represents the current
      // input text rather than the fixed replacement span captured when the
      // cycle started.
      //
      reset_cycle (s.comp);

      if (s.input.empty ())
      {
        s.comp.matches.clear ();
        return;
      }

      s.comp.matches = console_instance ()
                         .complete (s.input.text (), s.input.cursor ())
                         .matches;
    }

    void
    input_changed (ui_state& s)
    {
      // Editing and history browsing both invalidate the history navigation
      // anchor. Keeping that rule in one helper avoids the usual small
      // differences between backspace, character insertion, and kill commands.
      //
      s.hist.reset_navigation ();
      recompute_completion (s);
    }

    void
    assign_input (ui_state& s, string t)
    {
      s.input.assign (t);
      recompute_completion (s);
    }

    void
    cycle_completion (ui_state& s, int d)
    {
      completion_session& c (s.comp);

      if (c.matches.empty ())
        return;

      const size_t n (c.matches.size ());

      if (!c.cycling)
      {
        c.cycling = true;
        c.base = c.matches.front ().replacement.begin;
        c.selected = d >= 0 ? 0 : n - 1;
      }
      else
        c.selected = (c.selected + (d >= 0 ? 1 : n - 1)) % n;

      follow_selection (c);

      const console::completion_match& m (c.matches [c.selected]);
      const size_t e (s.input.cursor ());
      string t (s.input.text ());

      if (c.base <= e && e <= t.size ())
      {
        t.replace (c.base, e - c.base, m.text);
        s.input.assign (t);
      }
    }

    string
    inline_text (const ui_state& s)
    {
      const completion_session& c (s.comp);

      if (c.cycling || c.matches.empty ())
        return {};

      const console::completion_match& m (c.matches.front ());
      if (m.replacement.end > s.input.text ().size ())
        return {};

      const string q (
        s.input.text ().substr (m.replacement.begin, m.replacement.length ()));

      if (istarts_with (m.text, q) && m.text.size () > q.size ())
        return m.text.substr (q.size ());

      return {};
    }

    size_t
    visible_suggestions (const completion_session& c) noexcept
    {
      if (c.view >= c.matches.size ())
        return 0;

      return min (popup_rows, c.matches.size () - c.view);
    }

    void
    append_suggestions (console::render_model& m, const completion_session& c)
    {
      m.suggestion_total = c.matches.size ();
      m.suggestion_view_offset = c.view;

      const size_t n (visible_suggestions (c));
      m.suggestions.reserve (n);

      for (size_t i (0); i != n; ++i)
      {
        const size_t j (c.view + i);
        const console::completion_match& cm (c.matches [j]);

        console::render_suggestion r;
        r.name = cm.text;
        r.selected = j == c.selected;

        if (cm.annotation)
          r.annotation = *cm.annotation;

        m.suggestions.push_back (move (r));
      }
    }

    void
    append_detail (console::render_model& m, const completion_session& c)
    {
      // The popup row is where the full description can breathe. Splitting
      // here, rather than in the renderer, keeps the render model already
      // shaped like the final screen.
      //
      if (c.selected < c.matches.size ())
      {
        const console::completion_match& cm (c.matches [c.selected]);

        if (cm.description)
          m.detail_lines = split_lines (*cm.description);
      }
    }

    console::render_model
    make_render_model (ui_state& s, int local)
    {
      // The screen object owns scroll clamping, but only the engine-facing
      // layer knows how many rows the current layout leaves for the output
      // window. Refresh the visible row count before asking for the model.
      //
      s.scr.set_visible_rows (console::visible_output_rows (local));

      console::render_model m (console::build_model (s.scr, s.input));
      m.active = true;
      m.show_output_window = s.output;
      m.inline_completion = inline_text (s);

      append_suggestions (m, s.comp);
      append_detail (m, s.comp);

      return m;
    }

    string
    diagnostic_text (const console::diagnostic& d)
    {
      string r (d.summary ());

      if (d.detail)
        r += " (" + *d.detail + ")";

      if (d.recovery_hint)
        r += " [hint: " + *d.recovery_hint + "]";

      return r;
    }

    void
    log_diagnostic (const console::diagnostic& d)
    {
      const string m (diagnostic_text (d));

      // Warnings and errors should not disappear into the log file only. The
      // console is the place where the user just made the mistake, so echo the
      // diagnostic there as well and leave notes as logger-only chatter.
      //
      if (d.severity >= console::diagnostic_severity::warning)
        ui ().scr.print (m + "\n");

      switch (d.severity)
      {
        case console::diagnostic_severity::note:
          log::debug ("console: {}", m);
          break;
        case console::diagnostic_severity::warning:
          log::warning ("console: {}", m);
          break;
        case console::diagnostic_severity::error:
        case console::diagnostic_severity::fatal:
          log::error ("console: {}", m);
          break;
      }
    }

    void
    emit_output (string_view s)
    {
      string l (s);
      l.push_back ('\n');

      ui ().scr.print (l);
      Sys_Print (l.c_str ());
    }
  }

  console::console&
  console_instance ()
  {
    static console::engine_dvar_gateway g;
    static console::console c (g, &emit_output, &log_diagnostic);
    return c;
  }

  namespace
  {
    void
    set_catcher (int local, bool on)
    {
      int& k (clientUIActives [local].keyCatchers);

      if (on)
        k |= KEYCATCH_CONSOLE;
      else
        k &= ~KEYCATCH_CONSOLE;
    }

    void
    track_engine_key (int k, bool down)
    {
      if (!tracked_key (k))
        return;

      // We consume key-down events while the console is open, which means the
      // engine does not get its normal chance to maintain this state. Mirror
      // just enough of the original bookkeeping so closing the console cannot
      // leave a movement key logically stuck down.
      //
      playerKeys->keys [k].down = down;

      if (down)
      {
        if (++playerKeys->keys [k].repeats == 1)
          ++playerKeys->anyKeyDown;
      }
      else
      {
        playerKeys->keys [k].repeats = 0;

        if (--playerKeys->anyKeyDown < 0)
          playerKeys->anyKeyDown = 0;
      }
    }

    void
    submit (ui_state& s)
    {
      const string l (s.input.text ());

      s.scr.print ("] " + l + "\n");
      s.hist.push (l);

      if (!l.empty ())
        console_instance ().evaluate (l);

      s.input.clear ();
      s.scr.scroll_to_bottom ();
      reset_completion (s.comp);
    }

    void
    history_previous (ui_state& s)
    {
      if (!s.comp.matches.empty ())
        cycle_completion (s, -1);
      else if (auto t = s.hist.previous (s.input.text ()))
        assign_input (s, *t);
      else
        recompute_completion (s);
    }

    void
    history_next (ui_state& s)
    {
      if (!s.comp.matches.empty ())
        cycle_completion (s, +1);
      else if (auto t = s.hist.next ())
        assign_input (s, *t);
      else
        recompute_completion (s);
    }

    bool
    perform (ui_state& s, action a, const modifiers& m)
    {
      const bool sel (m.shift);

      switch (a)
      {
        case action::submit:
          submit (s);
          break;

        case action::cancel:
          s.open = false;
          reset_completion (s.comp);
          return true;

        case action::backspace:
          s.input.backspace ();
          input_changed (s);
          break;

        case action::erase_forward:
          s.input.erase_forward ();
          input_changed (s);
          break;

        case action::left:
          s.input.move_left (sel);
          recompute_completion (s);
          break;

        case action::right:
          s.input.move_right (sel);
          recompute_completion (s);
          break;

        case action::word_left:
          s.input.move_word_left (sel);
          recompute_completion (s);
          break;

        case action::word_right:
          s.input.move_word_right (sel);
          recompute_completion (s);
          break;

        case action::line_start:
          s.input.move_home (sel);
          recompute_completion (s);
          break;

        case action::line_end:
          s.input.move_end (sel);
          recompute_completion (s);
          break;

        case action::kill_word_back:
          s.input.erase_word_before ();
          input_changed (s);
          break;

        case action::kill_word_forward:
          s.input.erase_word_after ();
          input_changed (s);
          break;

        case action::kill_to_start:
          s.input.erase_to_start ();
          input_changed (s);
          break;

        case action::kill_to_end:
          s.input.erase_to_end ();
          input_changed (s);
          break;

        case action::clear_screen:
          s.scr.clear ();
          break;

        case action::history_prev:
          history_previous (s);
          break;

        case action::history_next:
          history_next (s);
          break;

        case action::page_up:
          s.scr.scroll_up (scroll_page_rows);
          break;

        case action::page_down:
          s.scr.scroll_down (scroll_page_rows);
          break;

        case action::scroll_up:
          s.scr.scroll_up (1);
          break;

        case action::scroll_down:
          s.scr.scroll_down (1);
          break;

        case action::complete:
          cycle_completion (s, +1);
          break;

        case action::none:
          break;
      }

      return false;
    }

    void
    toggle_console (ui_state& s, int local, const modifiers& m)
    {
      // Shift-grave is deliberately not another way to close the console. It
      // opens the input layer if necessary and then only toggles the output
      // overlay, which gives us a cheap "type with log hidden" mode.
      //
      if (m.shift)
      {
        if (!s.open)
          s.open = true;

        s.output = !s.output;
      }
      else
        s.open = !s.open;

      set_catcher (local, s.open);
    }

    void
    cl_key_event (int local, int key, int down, unsigned int time)
    {
      ui_state& s (ui ());
      const bool pressed (down != 0);

      track_engine_key (key, pressed);

      if (toggle_key (key))
      {
        if (pressed)
          toggle_console (s, local, current_modifiers ());

        return;
      }

      // Releases still belong to the engine. Key-down events also belong to it
      // whenever our console is closed. The only events we consume are the
      // editing presses while the console is active.
      //
      if (!pressed || !s.open)
      {
        CL_KeyEvent (local, key, down, time);
        return;
      }

      const modifiers m (current_modifiers ());
      const action a (resolve_action (key, m));

      if (perform (s, a, m))
        set_catcher (local, false);
    }

    void
    cl_char_event (int local, int key)
    {
      ui_state& s (ui ());

      if (!s.open)
      {
        CL_CharEvent (local, key);
        return;
      }

      if (printable (key))
      {
        const char c (static_cast<char> (key));
        s.input.insert (string_view (&c, 1));
        input_changed (s);
      }
    }

    void
    cg_draw_full_screen_ui (int local)
    {
      ZoneScoped;

      CG_DrawFullScreenUI (local);

      ui_state& s (ui ());
      if (!s.open)
        return;

      console::render_model m (make_render_model (s, local));
      console::draw (m, local);
    }

    void
    sys_print (const char* s)
    {
      if (s != nullptr)
        ui ().scr.print (s);

      Sys_Print (s);
    }
  }

  console_module::
  console_module ()
  {
    ZoneScoped;

    console::console& c (console_instance ());

    // Unknown statements are still engine commands. The console layer gets the
    // first chance to parse its own declarations, and anything it does not own
    // falls back to the original command buffer path.
    //
    c.set_engine_executor ([] (string_view s)
    {
      console::execute (s);
    });

    detour (CL_KeyEvent, &cl_key_event);
    detour (CL_CharEvent, &cl_char_event);
    detour (CG_DrawFullScreenUI, &cg_draw_full_screen_ui);
    detour (Sys_Print, &sys_print);
  }
}
