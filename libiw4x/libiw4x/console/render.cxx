#include <libiw4x/console/render.hxx>

#include <algorithm>
#include <string>

#include <libiw4x/import.hxx>

using namespace std;

namespace iw4x::console
{
  console_layout
  compute_layout (rect viewport, const console_metrics& m)
  {
    console_layout layout;

    // Calculate the base area by applying our margin to the viewport. Then, we
    // carve the input box from the top, leave a small gap, and the remaining
    // space becomes the output window.
    //
    const rect area (viewport.inset (m.margin));

    auto [input_box, below](area.split_top (m.input_height ()));
    layout.input_box = input_box;

    auto [gap_strip, output_box](below.split_top (m.gap));
    (void) gap_strip;
    layout.output_box = output_box;

    // Now for the output box itself. We inset it by the padding and then split
    // a fixed-width column off the right side to serve as the scrollbar track.
    //
    const rect content (output_box.inset (m.padding));
    auto [track, text] (content.split_right (m.scrollbar_width));
    layout.scrollbar_track = track;
    layout.output_text = text;
    layout.visible_lines = m.line_height > 0.0f ? static_cast<size_t> (text.h / m.line_height) : 0;

    return layout;
  }

  optional<rect>
  scrollbar_thumb (rect track, size_t total_lines, size_t visible_lines, size_t scroll_lines, float min_thumb)
  {
    // If the total lines fit entirely within the visible area, there is
    // nothing to scroll, so we just return an empty thumb.
    //
    if (total_lines <= visible_lines)
      return nullopt;

    const size_t max_scroll (total_lines - visible_lines);

    // We need to clamp the scroll offset here. The caller's scroll counter
    // might run ahead of the actual useful range since it doesn't know the
    // viewport height when it's being incremented. If we don't clamp, we end
    // up with a fraction greater than 1, which pushes the thumb right off the
    // top of the track.
    //
    const size_t clamped (min (scroll_lines, max_scroll));

    // Calculate the thumb height. It's proportional to the visible ratio, but
    // we make sure it doesn't get squashed below our minimum usable height.
    //
    const float ratio (static_cast<float> (visible_lines) / static_cast<float> (total_lines));
    float thumb_h (max (min_thumb, track.h * ratio));
    thumb_h = min (thumb_h, track.h);

    // To calculate the vertical offset, remember that the scroll counter counts
    // up from the bottom. A value of 0 pins the thumb to the bottom of the
    // track, and max_scroll lifts it completely to the top.
    //
    const float frac (max_scroll != 0 ? static_cast<float> (clamped) / static_cast<float> (max_scroll) : 0.0f);
    const float ty (track.top () + (track.h - thumb_h) * (1.0f - frac));

    return rect {track.x, ty, track.w, thumb_h};
  }

  render_model
  build_model (const screen& scr, const input_buffer& input)
  {
    render_model m;

    // We start by lifting the screen and input buffer state into our model.
    //
    const vector<string>& lines (scr.lines ());
    m.total_lines = lines.size ();
    m.scroll_lines = scr.scroll ();
    m.input = input.text ();
    m.cursor_column = input.cursor ();
    m.selection = input.selection ();

    // If there are lines to display, we slice out the ones currently visible
    // based on the current scroll position.
    //
    if (!lines.empty ())
    {
      const size_t last (lines.size () - 1);
      const size_t bottom (last >= scr.scroll () ? last - scr.scroll () : 0);

      m.output.assign (lines.begin (), lines.begin () + static_cast<ptrdiff_t> (bottom + 1));
    }

    return m;
  }

  namespace
  {
    Material*
    white_material () noexcept
    {
      return iw4x::cls != nullptr ? iw4x::cls->whiteMaterial : nullptr;
    }

    void
    fill_rect (rect r, const rgba& c)
    {
      if (c.a <= 0.0f || r.empty ())
        return;

      Material* m (white_material ());
      if (m == nullptr)
        return;

      const float cv[4] {c.r, c.g, c.b, c.a};
      iw4x::R_AddCmdDrawStretchPic (r.x, r.y, r.w, r.h, 0, 0, 0, 0, cv, m);
    }

    // Draw a filled box with darker borders. We derive the borders by carving
    // strips from the box using the same split algebra we used for the layout.
    // This is quite nice because it avoids any manual edge coordinate math.
    //
    void
    draw_box (rect r, const rgba& fill, float border)
    {
      fill_rect (r, fill);

      if (border <= 0.0f)
        return;

      const rgba edge (fill.scaled (0.5f));

      fill_rect (r.split_left (border).first, edge);
      fill_rect (r.split_right (border).first, edge);
      fill_rect (r.split_top (border).first, edge);
      fill_rect (r.split_bottom (border).first, edge);
    }

    void
    draw_text (point p, const char* text, const rgba& c, Font_s* font)
    {
      const float cv[4] {c.r, c.g, c.b, c.a};
      iw4x::R_AddCmdDrawText (text, 0x7FFFFFFF, font, p.x, p.y, 1.0f, 1.0f, 0.0f, cv, 0);
    }

    float
    text_width (const string& s, Font_s* font)
    {
      return static_cast<float> (iw4x::R_TextWidth (s.c_str (), 0x7FFFFFFF, font));
    }

    void
    draw_output (const render_model& model,
                 const console_layout& layout,
                 const console_metrics& m,
                 const console_theme& theme,
                 Font_s* font)
    {
      draw_box (layout.output_box, theme.output_bg, m.border);
      draw_box (layout.scrollbar_track, theme.scrollbar_track, 0.0f);

      if (auto thumb = scrollbar_thumb (layout.scrollbar_track,
                                        model.total_lines,
                                        layout.visible_lines,
                                        model.scroll_lines,
                                        m.scrollbar_min_thumb))
      {
        draw_box (*thumb, theme.scrollbar_thumb, 0.0f);
      }

      // Render the output text. We start with the newest lines at the bottom
      // and climb upwards until we either run out of lines or fill the area.
      //
      const rect area (layout.output_text);
      float line_top (area.bottom () - m.line_height);

      for (auto it (model.output.rbegin ()); it != model.output.rend () && line_top >= area.top (); ++it)
      {
        draw_text ({area.left (), line_top + m.line_height}, it->c_str (), theme.text, font);
        line_top -= m.line_height;
      }
    }

    void
    draw_input (const render_model& model,
                const console_layout& layout,
                const console_metrics& m,
                const console_theme& theme,
                Font_s* font)
    {
      draw_box (layout.input_box, theme.input_bg, m.border);

      const rect content (layout.input_box.inset (m.padding));
      const float baseline (content.top () + m.line_height);
      float x (content.left ());

      draw_text ({x, baseline}, "]", theme.accent, font);
      x += text_width ("] ", font);

      // Draw the selection highlight behind the text. We figure out the span
      // by calculating the text width up to the selection start, and up to the
      // selection end, and then we fill that band.
      //
      const auto [sel_lo, sel_hi] (model.selection);
      if (sel_lo < sel_hi && sel_hi <= model.input.size ())
      {
        const float lo_x (x + text_width (model.input.substr (0, sel_lo), font));
        const float hi_x (x + text_width (model.input.substr (0, sel_hi), font));
        fill_rect ({lo_x, content.top (), hi_x - lo_x, content.h}, theme.selection);
      }

      draw_text ({x, baseline}, model.input.c_str (), theme.text, font);

      // If we have an inline completion, draw it right after the user's text
      // using the dimmed theme color.
      //
      if (!model.inline_completion.empty ())
      {
        const float typed_w (text_width (model.input, font));
        draw_text ({x + typed_w, baseline}, model.inline_completion.c_str (), theme.dim, font);
      }

      // Finally, draw the blinking caret at the current cursor column. We
      // simply toggle it every 500 milliseconds.
      //
      if ((iw4x::Sys_Milliseconds () / 500) % 2 == 0)
      {
        const string before (model.input.substr (0, model.cursor_column));
        draw_text ({x + text_width (before, font), baseline}, "_", theme.accent, font);
      }
    }

    void
    draw_popup (const render_model& model,
                const console_layout& layout,
                const console_metrics& m,
                const console_theme& theme,
                Font_s* font)
    {
      if (model.suggestions.empty ())
        return;

      const bool has_above (model.suggestion_view_offset > 0);
      const bool has_below (model.suggestion_view_offset + model.suggestions.size () < model.suggestion_total);
      const size_t rows (model.suggestions.size () + (has_above ? 1u : 0u) + (has_below ? 1u : 0u));

      const float row_h (m.line_height + 4.0f);

      // We anchor the suggestion popup to hang from the top of the output
      // window. It spans the same width and grows in height to fit its rows.
      //
      const rect popup {layout.output_box.x,
                        layout.output_box.y,
                        layout.output_box.w,
                        row_h * static_cast<float> (rows) + 2.0f * m.padding};
      draw_box (popup, theme.popup_bg, m.border);

      // To walk through the rows, we repeatedly slice a row-height strip off
      // the top of our remaining interior space.
      //
      rect rest (popup.inset (m.padding));
      auto next_row = [&rest, row_h] () -> rect
      {
        auto [row, remainder](rest.split_top (row_h));
        rest = remainder;
        return row;
      };

      auto draw_label = [&] (rect row, const string& text, const rgba& c)
      {
        draw_text ({row.left () + m.padding, row.top () + m.line_height}, text.c_str (), c, font);
      };

      if (has_above)
        draw_label (next_row (), "  ^ more above", theme.dim);

      for (const render_suggestion& s : model.suggestions)
      {
        const rect row (next_row ());

        if (s.selected)
          draw_box (row, theme.selection, 0.0f);

        draw_label (row, s.name, s.selected ? theme.accent : theme.text);

        if (!s.annotation.empty ())
        {
          const float name_w (text_width (s.name, font));
          draw_text ({row.left () + m.padding + name_w + 16.0f, row.top () + m.line_height},
                     s.annotation.c_str (),
                     theme.dim,
                     font);
        }
      }

      if (has_below)
      {
        const size_t remaining (model.suggestion_total - model.suggestion_view_offset - model.suggestions.size ());
        draw_label (next_row (), "  v " + to_string (remaining) + " more", theme.dim);
      }

      // If there's extra detail for the selected item, we draw a detail box
      // stacked directly beneath the main popup.
      //
      if (!model.detail_lines.empty ())
      {
        const rect detail {popup.x,
                           popup.bottom () + 2.0f,
                           popup.w,
                           row_h * static_cast<float> (model.detail_lines.size ()) + 2.0f * m.padding};
        draw_box (detail, theme.detail_bg, m.border);

        rect drest (detail.inset (m.padding));
        for (const string& line : model.detail_lines)
        {
          auto [row, remainder] (drest.split_top (row_h));
          drest = remainder;
          draw_text ({row.left (), row.top () + m.line_height}, line.c_str (), theme.text, font);
        }
      }
    }
  }

  namespace
  {
    // This is our per-frame engine context. It gathers the active placement,
    // the console font, our derived metrics, and the computed layout. Resolving
    // all of this in one place keeps the drawing and the viewport queries in
    // agreement, and it confines all engine state reads to a single point.
    //
    struct frame
    {
      Font_s* font;
      console_metrics metrics;
      console_layout layout;
    };

    optional<frame>
    begin_frame (int local_client_num)
    {
      ScreenPlacement* scr (iw4x::ScrPlace_GetActivePlacement (local_client_num));
      if (scr == nullptr)
        return nullopt;

      Font_s* font (iw4x::cls != nullptr ? iw4x::cls->consoleFont : nullptr);
      if (font == nullptr)
        return nullopt;

      const float font_h (static_cast<float> (iw4x::R_TextHeight (font)));
      if (font_h <= 0.0f)
        return nullopt;

      console_metrics metrics;
      metrics.line_height = font_h;

      const rect viewport {scr->realViewportPosition.x,
                           scr->realViewportPosition.y,
                           scr->realViewportSize.x,
                           scr->realViewportSize.y};

      return frame {font, metrics, compute_layout (viewport, metrics)};
    }
  }

  size_t
  visible_output_rows (int local_client_num)
  {
    if (auto f = begin_frame (local_client_num))
      return f->layout.visible_lines;

    return 0;
  }

  void
  draw (const render_model& model, int local_client_num)
  {
    if (!model.active)
      return;

    auto f (begin_frame (local_client_num));
    if (!f)
      return;

    const console_theme theme;

    // The composition order matters here. We render the output window first so
    // it sits underneath. Then comes the input box. Finally, the suggestion
    // popup goes on top to guarantee it is never obscured.
    //
    if (model.show_output_window)
      draw_output (model, f->layout, f->metrics, theme, f->font);

    draw_input (model, f->layout, f->metrics, theme, f->font);
    draw_popup (model, f->layout, f->metrics, theme, f->font);
  }
}
