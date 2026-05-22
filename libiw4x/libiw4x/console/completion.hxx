#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <immer/vector.hpp>

#include <libiw4x/console/command.hxx>
#include <libiw4x/console/core.hxx>
#include <libiw4x/console/types.hxx>

namespace iw4x::console
{
  // This represents a single completable entity in a snapshot.
  //
  // The `text` member is the full candidate string that a match would insert.
  // We use `kind` to drive both presentation and filtering. The `annotation` is
  // an optional, short descriptive text that is typically shown inline beside
  // the candidate (for example, a command's summary or a dvar's current value).
  // Then we have `description`, the optional full help text. It may span
  // several lines, so it is usually surfaced separately (like in a detail
  // panel) to avoid cramming multi-line content into the single-line
  // annotation. Finally, `source` can optionally reference the declaration from
  // which this candidate originated.
  //
  struct completion_candidate
  {
    std::string                   text;
    completion_kind               kind {completion_kind::command};
    std::optional<std::string>    annotation;
    std::optional<std::string>    description;
    std::optional<declaration_id> source;
  };

  // This is an immutable index of candidates.
  //
  // We normally build it once from a command table (and optionally a dvar
  // gateway), and then query it repeatedly. The underlying persistent storage
  // means that the index can be easily snapshotted and shared across threads
  // without any copying or locking overhead.
  //
  class completion_index
  {
  public:
    completion_index () = default;

    const immer::vector<completion_candidate>&
    candidates () const noexcept
    {
      return candidates_;
    }

    std::size_t
    size () const noexcept
    {
      return candidates_.size ();
    }

    // Append a candidate and return the newly grown index.
    //
    // Note that since the underlying storage is persistent, this current index
    // remains entirely unchanged.
    //
    completion_index
    with (completion_candidate c) const;

  private:
    immer::vector<completion_candidate> candidates_;
  };

  // Build a completion index populated with command names and aliases from the
  // provided command table.
  //
  completion_index
  index_commands (const command_table& table);

  // Extend an existing completion index with every dvar known to the given
  // gateway.
  //
  completion_index
  index_dvars (completion_index base, const dvar_gateway& gateway);

  // This dictates what we are trying to complete and where the completion
  // should replace existing text in the source.
  //
  // The `query` is the partial text that the user has typed so far. The
  // `replacement` is the source range that the chosen completion is expected to
  // overwrite. We use `expected` to optionally narrow the candidate set down to
  // a specific kind. For example, we might only want commands when completing
  // the head of a line, or only dvars immediately after a "set" command.
  //
  struct completion_request
  {
    std::string query;
    source_span replacement {};
    std::optional<completion_kind> expected;
  };

  // These are the knobs that control our ranking heuristics.
  //
  // The `min_score` discards weak matches, while `max_results` caps the
  // returned list. Note that these default values closely mirror the old
  // console's tuning. This is done to feel familiar out of the box, while still
  // leaving us enough room to tweak and adjust later if necessary.
  //
  struct completion_config
  {
    int min_score {40};
    std::size_t max_results {64};
  };

  // This represents a single, ranked suggestion.
  //
  // The `text` is the actual string we want to insert, and `replacement` is the
  // range it will overwrite. The `score` is the final rank, clamped strictly to
  // the [0, 100] range. Members like `kind`, `annotation`, `description`, and
  // `source` simply carry through from the original candidate. Note that
  // `description` is the full, possibly multi-line help text intended for a
  // detail panel. We also provide `detail` to optionally explain a borderline
  // or otherwise surprising match, which is extremely handy for tracing and
  // debugging the ranker.
  //
  struct completion_match
  {
    std::string                   text;
    source_span                   replacement {};
    int                           score {0};
    completion_kind               kind {completion_kind::command};
    std::optional<std::string>    annotation;
    std::optional<std::string>    description;
    std::optional<std::string>    detail;
    std::optional<declaration_id> source;
  };

  // This is the final outcome of a completion query.
  //
  // The `matches` vector is sorted by descending score. We use deterministic
  // tie-breaking here (prefix matches first, then by kind, and finally by
  // case-insensitive text). The `diags` member carries any notes or errors.
  //
  struct completion_result
  {
    std::vector<completion_match> matches;
    diagnostics diags;

    bool
    empty () const noexcept
    {
      return matches.empty ();
    }
  };

  // Rank the global index against a specific request, yielding a sorted list
  // of matches.
  //
  completion_result
  complete (const completion_index& index,
            const completion_request& request,
            const completion_config& config = {});

  // Rank an explicit, ad-hoc candidate list against a request.
  //
  // We primarily use this for argument completion where the candidate set is
  // contextual (such as a parameter's known values, or dvar names for a dvar
  // reference parameter), rather than querying the global index.
  //
  completion_result
  complete_from (const std::vector<completion_candidate>& candidates,
                 const completion_request& request,
                 const completion_config& config = {});

  // Slide a fixed-size view window so that a selected index always remains
  // visible.
  //
  // Given a `selected` index into a list of `total` items, a `window`
  // representing the number of rows the UI can show at once, a `scrolloff`
  // margin (the context we want to keep around the selection), and the
  // `current` first-visible index, this function returns the new first-visible
  // index.
  //
  // The window only moves once the selection comes within the `scrolloff`
  // distance of an edge. Think of this as Vim's `scrolloff` behavior. The
  // result is always clamped to keep the window within [0, total). When all
  // items fit in the window, the offset is naturally 0.
  //
  std::size_t
  scroll_view_offset (std::size_t selected,
                      std::size_t total,
                      std::size_t window,
                      std::size_t scrolloff,
                      std::size_t current) noexcept;
}
