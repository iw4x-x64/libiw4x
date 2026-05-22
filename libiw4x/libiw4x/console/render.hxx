#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <libiw4x/console/input.hxx>

namespace iw4x::console
{
  struct point
  {
    float x {0.0f};
    float y {0.0f};
  };

  struct extent
  {
    float w {0.0f};
    float h {0.0f};
  };

  struct rect
  {
    float x {0.0f};
    float y {0.0f};
    float w {0.0f};
    float h {0.0f};

    constexpr float
    left () const noexcept
    {
      return x;
    }

    constexpr float
    top () const noexcept
    {
      return y;
    }

    constexpr float
    right () const noexcept
    {
      return x + w;
    }

    constexpr float
    bottom () const noexcept
    {
      return y + h;
    }

    constexpr point
    origin () const noexcept
    {
      return {x, y};
    }

    constexpr extent
    size () const noexcept
    {
      return {w, h};
    }

    constexpr bool
    empty () const noexcept
    {
      return w <= 0.0f || h <= 0.0f;
    }

    // Shrink uniformly on all sides.
    //
    // Note that a margin larger than half the extent collapses that axis to
    // zero rather than inverting it.
    //
    constexpr rect
    inset (float m) const noexcept
    {
      return inset (m, m);
    }

    constexpr rect
    inset (float dx, float dy) const noexcept
    {
      const float nw (w - 2.0f * dx > 0.0f ? w - 2.0f * dx : 0.0f);
      const float nh (h - 2.0f * dy > 0.0f ? h - 2.0f * dy : 0.0f);

      return {x + dx, y + dy, nw, nh};
    }

    constexpr rect
    offset (float dx, float dy) const noexcept
    {
      return {x + dx, y + dy, w, h};
    }

    // Take a strip of the given thickness off one edge and return the strip
    // along with the remainder.
    //
    // We clamp the thickness to the available extent to prevent over-splitting.
    //
    constexpr std::pair<rect, rect>
    split_top (float t) const noexcept
    {
      const float th (t < h ? t : h);
      return {{x, y, w, th}, {x, y + th, w, h - th}};
    }

    constexpr std::pair<rect, rect>
    split_bottom (float t) const noexcept
    {
      const float th (t < h ? t : h);
      return {{x, y + h - th, w, th}, {x, y, w, h - th}};
    }

    constexpr std::pair<rect, rect>
    split_left (float t) const noexcept
    {
      const float tw (t < w ? t : w);
      return {{x, y, tw, h}, {x + tw, y, w - tw, h}};
    }

    constexpr std::pair<rect, rect>
    split_right (float t) const noexcept
    {
      const float tw (t < w ? t : w);
      return {{x + w - tw, y, tw, h}, {x, y, w - tw, h}};
    }
  };

  struct rgba
  {
    float r {0.0f};
    float g {0.0f};
    float b {0.0f};
    float a {1.0f};

    constexpr rgba
    with_alpha (float na) const noexcept
    {
      return {r, g, b, na};
    }

    // Scale the colour channels while preserving the alpha.
    //
    // The idea here is to derive border shades from a fill colour.
    //
    constexpr rgba
    scaled (float f) const noexcept
    {
      return {r * f, g * f, b * f, a};
    }
  };

  // Layout dimensions in real pixel units.
  //
  // Note that line_height is filled from the font at draw time. The rest
  // are fixed design constants gathered here so the layout reads as a
  // composition over named quantities.
  //
  struct console_metrics
  {
    float margin              {4.0f};
    float padding             {6.0f};
    float border              {2.0f};
    float gap                 {12.0f};
    float input_extra         {12.0f};
    float scrollbar_width     {10.0f};
    float scrollbar_min_thumb {16.0f};
    float line_height         {16.0f};

    constexpr float
    input_height () const noexcept
    {
      return line_height + input_extra;
    }
  };

  // Colour palette.
  //
  struct console_theme
  {
    rgba input_bg        {0.25f, 0.25f, 0.20f, 1.00f};
    rgba output_bg       {0.35f, 0.35f, 0.30f, 0.75f};
    rgba scrollbar_track {1.00f, 1.00f, 0.95f, 0.60f};
    rgba scrollbar_thumb {0.15f, 0.15f, 0.10f, 0.60f};
    rgba popup_bg        {0.40f, 0.40f, 0.35f, 1.00f};
    rgba selection       {1.00f, 1.00f, 1.00f, 0.15f};
    rgba detail_bg       {0.20f, 0.20f, 0.35f, 0.95f};
    rgba text            {0.90f, 0.90f, 0.90f, 1.00f};
    rgba accent          {1.00f, 0.80f, 0.30f, 1.00f};
    rgba dim             {0.55f, 0.55f, 0.55f, 1.00f};
  };

  // Named regions of the console, derived from the viewport and metrics.
  //
  struct console_layout
  {
    rect input_box;                // The input line's bordered box.
    rect output_box;               // The output window's bordered box.
    rect output_text;              // Where output text draws (inside output_box).
    rect scrollbar_track;          // The scrollbar's full track.
    std::size_t visible_lines {0}; // Output rows that fit in output_text.
  };

  // Compose the layout from a viewport rectangle and metrics.
  //
  console_layout
  compute_layout (rect viewport, const console_metrics& metrics);

  // Position the scrollbar thumb within its track.
  //
  // Returns nullopt when everything fits and no overflow occurs. Otherwise, we
  // size the thumb in proportion to the visible fraction and position it so
  // that the newest line (scroll_lines == 0) sits at the track bottom.
  //
  std::optional<rect>
  scrollbar_thumb (rect        track,
                   std::size_t total_lines,
                   std::size_t visible_lines,
                   std::size_t scroll_lines,
                   float       min_thumb);

  // One row in the completion popup.
  //
  struct render_suggestion
  {
    std::string name;
    std::string annotation;
    bool        selected {false};
  };

  // The content to draw this frame.
  //
  struct render_model
  {
    bool active {false};
    bool show_output_window {true};

    // Output lines up to the scrolled-to bottom, ordered oldest first.
    //
    // We render them bottom-up in draw() and stop when we run out of vertical
    // space. The total_lines and scroll_lines describe the full backlog so
    // the scrollbar thumb can be sized correctly.
    //
    std::vector<std::string> output;
    std::size_t total_lines {0};
    std::size_t scroll_lines {0};

    std::string input;
    std::size_t cursor_column {0};

    // The highlighted selection as a half-open [lo, hi) byte range over
    // the input.
    //
    // If lo == hi, there is no active selection.
    //
    std::pair<std::size_t, std::size_t> selection {0, 0};

    // Dimmed remainder of the top completion candidate beyond what the
    // user typed.
    //
    std::string inline_completion;

    std::vector<render_suggestion> suggestions;
    std::size_t                    suggestion_view_offset {0};
    std::size_t                    suggestion_total {0};
    std::vector<std::string>       detail_lines;
  };

  // Build the output, scroll, and input parts of the model.
  //
  // The completion fields are left default here so the integration layer
  // can populate them afterwards.
  //
  render_model
  build_model (const screen& scr, const input_buffer& input);

  // Issue engine draw commands for a model.
  //
  // This is engine-facing and verified in-game. It acts as a no-op when the
  // console is closed or the renderer is not ready.
  //
  void
  draw (const render_model& model, int local_client_num);

  // The number of output rows that fit in the console's output window for
  // the given client.
  //
  // Returns 0 if the renderer is not ready. The integration layer uses this
  // to bound scrolling to the visible range, which it otherwise cannot know
  // prior to layout computation. This is engine-facing.
  //
  std::size_t
  visible_output_rows (int local_client_num);
}
