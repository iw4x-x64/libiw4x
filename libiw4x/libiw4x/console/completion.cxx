#include <libiw4x/console/completion.hxx>

#include <algorithm>
#include <exception>
#include <utility>

#include <rapidfuzz/fuzz.hpp>

#include <libiw4x/console/utility.hxx>

using namespace std;

namespace iw4x::console
{
  namespace
  {
    // Convert a string view to lowercase using simple ASCII rules.
    //
    // We do this instead of relying on the standard library's locale-aware
    // transformations since our console commands and variables are ASCII.
    //
    string
    to_lower (string_view s)
    {
      string out (s);
      for (char& c : out)
        c = ascii_to_lower (c);
      return out;
    }

    // Check if index i is the start of a word within the candidate.
    //
    // A word starts at offset 0, after a separator (such as an underscore,
    // dash, dot, slash, or space), or at a lower-to-upper case transition
    // (camelCase). This is what allows a query like "tcp" to rank the candidate
    // "TcpWriteSocket" highly, as each query letter lands on a boundary.
    //
    bool
    is_word_start (string_view candidate, size_t i) noexcept
    {
      if (i == 0)
        return true;

      const char prev (candidate[i - 1]);
      const char cur (candidate[i]);

      if (prev == '_' || prev == '-' || prev == '.' || prev == '/' ||
          prev == ' ')
        return true;

      const bool prev_lower (prev >= 'a' && prev <= 'z');
      const bool cur_upper  (cur  >= 'A' && cur  <= 'Z');

      return prev_lower && cur_upper;
    }

    // Count the number of query characters that match the candidate at word
    // boundaries.
    //
    // This assumes the query is a (case-insensitive) subsequence of the
    // candidate. If it turns out the query is not a subsequence at all, we
    // return -1.
    //
    int
    boundary_hits (string_view ql, string_view cl, string_view craw) noexcept
    {
      int hits (0);
      size_t qi (0);

      for (size_t ci (0); ci < cl.size () && qi < ql.size (); ++ci)
      {
        if (ql[qi] == cl[ci])
        {
          if (is_word_start (craw, ci))
            ++hits;
          ++qi;
        }
      }

      return qi == ql.size () ? hits : -1;
    }

    // The result of scoring a candidate.
    //
    // We keep track of the numeric value as well as whether this was a prefix
    // match, which we will use later for tie-breaking.
    //
    struct scored
    {
      int value {0};
      bool prefix {false};
    };

    // Score a candidate against the user's query.
    //
    // This wraps rapidfuzz to provide typo tolerance, but layers on the
    // semantics we expect from console completion. For instance, an empty query
    // simply lists everything. A prefix match is highly favored, and a
    // boundary-aligned subsequence is boosted.
    //
    // The result is a clamped [0, 100] score along with a prefix flag. If the
    // score falls below the specified threshold, we return nullopt to indicate
    // no match.
    //
    optional<scored>
    score_candidate (string_view query_lower,
                     string_view candidate,
                     int threshold)
    {
      if (query_lower.empty ())
        return scored {100, false}; // ordering falls back to text.

      const string cand_lower (to_lower (candidate));

      // Notice that exact and prefix matches are ranked above any fuzzy match.
      // That is, we want the obvious completion to always wins out over a
      // loosely matched alternative.
      //
      if (cand_lower == query_lower)
        return scored {100, true};

      if (cand_lower.starts_with (query_lower))
      {
        // Shorter remainders rank slightly higher. For example, "map" beats
        // "mapname" when the user's query is "map".
        //
        const size_t extra (cand_lower.size () - query_lower.size ());
        const int penalty (static_cast<int> (min<size_t> (extra, 8)));

        return scored {95 - penalty, true};
      }

      // Calculate the fuzzy base score from rapidfuzz.
      //
      // Using partial_ratio finds the best-matching window of the candidate,
      // which suits short queries against longer names.
      //
      double base {0.0};
      try
      {
        base = rapidfuzz::fuzz::partial_ratio (query_lower, cand_lower);
      }
      catch (...)
      {
        return nullopt; // surfaced by the caller as a backend failure
      }

      int value (static_cast<int> (base));

      const int hits (boundary_hits (query_lower, cand_lower, candidate));
      if (hits > 0)
        value += min (12, hits * 4);

      value = clamp (value, 0, 99);

      if (value < threshold)
        return nullopt;

      return scored {value, false};
    }

    // Establish a stable ordering for completion matches.
    //
    // We sort by score descending. If there is a tie, prefix matches win. After
    // that, we sort by kind ascending, then case-insensitive text, and finally
    // fall back to case-sensitive text to make the ordering fully
    // deterministic.
    //
    bool
    rank_less (const completion_match& a, bool ap,
               const completion_match& b, bool bp)
    {
      if (a.score != b.score)
        return a.score > b.score;

      if (ap != bp)
        return ap; // prefix matches float to the top

      if (a.kind != b.kind)
        return static_cast<int> (a.kind) < static_cast<int> (b.kind);

      const string al (to_lower (a.text));
      const string bl (to_lower (b.text));

      if (al != bl)
        return al < bl;

      return a.text < b.text;
    }
  }

  completion_index completion_index::
  with (completion_candidate c) const
  {
    completion_index next;

    // Notice that we return a new completion index to maintains the
    // immutability of the underlying persistent data structure.
    //
    next.candidates_ = candidates_.push_back (move (c));
    return next;
  }

  completion_index
  index_commands (const command_table& table)
  {
    completion_index index;

    // We iterate through all command records and inject them into the index.
    //
    for (const command_record& r : table.records ())
    {
      completion_candidate c;
      c.text = r.declaration.name.str ();
      c.kind = completion_kind::command;

      if (!r.declaration.description.empty ())
        c.annotation = r.declaration.description;

      c.source = static_cast<declaration_id> (
        static_cast<uint32_t> (r.id));

      index = index.with (move (c));

      // Aliases are completable too. We tag them distinctly so that callers can
      // present them in the UI.
      //
      for (const command_name& a : r.declaration.aliases)
      {
        completion_candidate ac;
        ac.text = a.str ();
        ac.kind = completion_kind::alias;
        ac.annotation = format ("alias for {}", r.declaration.name.str ());
        index = index.with (move (ac));
      }
    }

    return index;
  }

  completion_index
  index_dvars (completion_index base, const dvar_gateway& gateway)
  {
    // Append dvars from the gateway to the existing index.
    //
    gateway.enumerate (
      [&base] (const dvar_descriptor& d)
      {
        completion_candidate c;
        c.text = d.name.str ();
        c.kind = completion_kind::dvar;

        const string& s (d.description);

        if (!s.empty ())
        {
          // See if we have a multi-line description. If so, the first line is
          // the annotation and the rest becomes the detailed description.
          //
          auto p (s.find ('\n'));

          if (p != string::npos)
          {
            // Watch out for Windows-style line endings. If the newline is
            // preceded by a carriage return, we need to strip it from the
            // annotation.
            //
            size_t e (p != 0 && s[p - 1] == '\r' ? p - 1 : p);

            c.annotation  = s.substr (0, e);
            c.description = s.substr (p + 1);
          }
          else
            c.annotation = s;
        }

        base = base.with (move (c));
      });

    return base;
  }

  completion_result
  complete_from (const vector<completion_candidate>& candidates,
                 const completion_request& request,
                 const completion_config& config)
  {
    completion_result result;
    const string query_lower (to_lower (request.query));

    // Pair each surviving match with its prefix flag so we can sort them, then
    // strip the flag when transferring to the final result.
    //
    vector<pair<completion_match, bool>> ranked;
    ranked.reserve (candidates.size ());

    for (const completion_candidate& cand : candidates)
    {
      if (request.expected.has_value () && cand.kind != *request.expected)
        continue;

      optional<scored> s (
        score_candidate (query_lower, cand.text, config.min_score));

      if (!s.has_value ())
        continue;

      completion_match m;
      m.text = cand.text;
      m.replacement = request.replacement;
      m.score = s->value;
      m.kind = cand.kind;
      m.annotation = cand.annotation;
      m.description = cand.description;
      m.source = cand.source;

      ranked.emplace_back (move (m), s->prefix);
    }

    stable_sort (ranked.begin (), ranked.end (),
                 [] (const auto& a, const auto& b)
                 {
                   return rank_less (a.first, a.second, b.first, b.second);
                 });

    if (ranked.size () > config.max_results)
      ranked.resize (config.max_results);

    result.matches.reserve (ranked.size ());
    for (auto& [m, prefix] : ranked)
    {
      (void) prefix;
      result.matches.push_back (move (m));
    }

    // If the query is not empty but we found no matches, we emit a diagnostic
    // note. This is not an error per se; the user simply typed an unknown name.
    //
    if (result.matches.empty () && !request.query.empty ())
    {
      diagnostic d;
      d.severity = diagnostic_severity::note;
      d.category = diagnostic_category::completion;
      d.code = diagnostic_code::cmp_no_candidates;
      d.message = format ("no completions for '{}'", request.query);
      d.span = request.replacement;
      result.diags.add (move (d));
    }

    return result;
  }

  completion_result
  complete (const completion_index& index,
            const completion_request& request,
            const completion_config& config)
  {
    // Snapshot the relevant candidates into a flat vector first.
    //
    // Because the index uses persistent data structures under the hood, copying
    // these small candidate objects is relatively cheap and isolates the
    // ranking algorithm from the storage representation.
    //
    vector<completion_candidate> flat;
    flat.reserve (index.size ());

    for (const completion_candidate& c : index.candidates ())
      flat.push_back (c);

    return complete_from (flat, request, config);
  }

  size_t
  scroll_view_offset (size_t selected, size_t total,
                      size_t window, size_t scrolloff,
                      size_t current) noexcept
  {
    // If everything fits, or if the window is degenerate, just pin to the top.
    //
    if (window == 0 || total <= window)
      return 0;

    const size_t max_off (total - window);
    size_t off (min (current, max_off));

    // If we scrolled too far down relative to the selection, we bring the top
    // down so there are at least 'scrolloff' rows of context above it.
    //
    if (selected < off + scrolloff)
      off = selected > scrolloff ? selected - scrolloff : 0;

    // If we scrolled too far up, we push the top down so there are 'scrolloff'
    // rows of context below the selection. Notice that in this branch, the
    // condition selected + scrolloff >= window holds, so the subtraction is
    // safe from underflow.
    //
    else if (selected + scrolloff > off + window - 1)
      off = selected + scrolloff + 1 - window;

    return min (off, max_off);
  }
}
