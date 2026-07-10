#pragma once

#include <stddef.h>
#include <stdbool.h>

struct Buffer;

typedef struct {
    size_t start;
    size_t end;
} SearchMatch;

typedef struct {
    struct Buffer *query_buf;  // owns the query text and cursor position
    float          text_scroll; // horizontal scroll offset (for future use)
    SearchMatch   *matches;
    size_t         match_count;
    size_t         match_cap;
    size_t         active_match_index;
    size_t         point;      // cursor position when search was opened
    bool           active;     // session exists: highlights + n/N work
    bool           bar_open;   // search bar UI is visible
    char           count_str[32]; // pre-formatted "N/M" or "no matches"; stable memory for Clay
    size_t         sel_anchor;    // fixed end of the text selection
    bool           sel_active;    // whether a selection is active
    bool           case_sensitive;  // when false, matching ignores case
    bool           match_whole_words; // when true, only whole-word matches are valid
    bool           backward;        // when true, ? search: initial match and n/N are reversed
    bool           match_in_selection; // when true, matches are restricted to the frozen search range
    size_t         match_range_start;  // frozen restrict-range start, captured when search was opened
    size_t         match_range_end;    // frozen restrict-range end
    bool           has_match_range;    // whether match_range_start/end were captured this session

    struct Buffer *replace_buf;      // owns replacement text and its own cursor; persists like query_buf
    bool           replace_open;     // replace bar UI is visible
    size_t         replace_sel_anchor; // fixed end of the replacement text selection
    bool           replace_sel_active; // whether a selection is active in the replacement field
} SearchSession;

// Free dynamically allocated match storage and query buffer.
void search_session_free(SearchSession *s);

// Recompute all matches from buffer text within [range_start, range_end).
// Updates active_match_index to the first match at or after s->point
// (forward) or the last match at or before s->point (backward). Call
// whenever query, text, or the scan range changes.
void search_session_recompute(SearchSession *s, const char *text, size_t text_len,
                               const char *query, size_t query_len,
                               size_t range_start, size_t range_end);

// Advance active_match_index (wraps). Returns start offset of new active match.
size_t search_session_next_match(SearchSession *s);

// Retreat active_match_index (wraps). Returns start offset of new active match.
size_t search_session_prev_match(SearchSession *s);

// Returns the index of the match that should be replaced next, given the
// buffer's live cursor position: the match containing cursor_pos if any,
// else the nearest match in the search direction (wraps around). Returns
// (size_t)-1 if match_count == 0.
size_t search_session_replace_target(const SearchSession *s, size_t cursor_pos);
