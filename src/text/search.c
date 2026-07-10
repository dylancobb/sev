#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "buffer.h"
#include "search.h"

void search_session_free(SearchSession *s) {
    if (s->query_buf) {
        buffer_delete(s->query_buf);
        s->query_buf = NULL;
    }
    if (s->replace_buf) {
        buffer_delete(s->replace_buf);
        s->replace_buf = NULL;
    }
    free(s->matches);
    s->matches   = NULL;
    s->match_cap = 0;
}

static const char *find_case_insensitive(const char *hay, size_t hlen,
                                         const char *ndl, size_t nlen) {
    if (nlen > hlen) return NULL;
    for (size_t i = 0; i <= hlen - nlen; i++)
        if (strncasecmp(hay + i, ndl, nlen) == 0) return hay + i;
    return NULL;
}

static bool is_word_char(uint32_t cp) {
    return (cp >= 'A' && cp <= 'Z') ||
           (cp >= 'a' && cp <= 'z') ||
           (cp >= '0' && cp <= '9') ||
           cp == '_';
}

void search_session_recompute(SearchSession *s, const char *text, size_t text_len,
                               const char *query, size_t query_len,
                               size_t range_start, size_t range_end) {
    s->match_count = 0;
    if (query_len == 0 || text_len == 0) return;
    if (range_start > text_len) range_start = text_len;
    if (range_end > text_len) range_end = text_len;
    if (range_end < range_start) range_end = range_start;

    const char *p   = text + range_start;
    const char *end = text + range_end;
    while (p < end) {
        const char *hit = s->case_sensitive
            ? memmem(p, (size_t)(end - p), query, query_len)
            : find_case_insensitive(p, (size_t)(end - p), query, query_len);
        if (!hit) break;

        if (s->match_whole_words) {
            size_t hit_start = (size_t)(hit - text);
            size_t hit_end   = hit_start + query_len;
            bool left_ok  = hit_start == 0 || !is_word_char((unsigned char)text[hit_start - 1]);
            bool right_ok = hit_end == text_len || !is_word_char((unsigned char)text[hit_end]);
            if (!left_ok || !right_ok) {
                p = hit + 1;
                continue;
            }
        }

        if (s->match_count >= s->match_cap) {
            size_t new_cap = s->match_cap ? s->match_cap * 2 : 64;
            SearchMatch *nm = realloc(s->matches, new_cap * sizeof(SearchMatch));
            if (!nm) break;
            s->matches   = nm;
            s->match_cap = new_cap;
        }
        s->matches[s->match_count++] = (SearchMatch){
            .start = (size_t)(hit - text),
            .end   = (size_t)(hit - text) + query_len
        };
        p = hit + 1;
    }

    if (s->backward) {
        s->active_match_index = s->match_count - 1; // wrap: all ahead → last match
        for (size_t i = s->match_count; i-- > 0; ) {
            if (s->matches[i].start <= s->point) {
                s->active_match_index = i;
                break;
            }
        }
    } else {
        s->active_match_index = 0; // wrap: all behind → first match
        for (size_t i = 0; i < s->match_count; i++) {
            if (s->matches[i].start >= s->point) {
                s->active_match_index = i;
                break;
            }
        }
    }

    if (s->match_count == 0)
        snprintf(s->count_str, sizeof(s->count_str), "0/0");
    else
        snprintf(s->count_str, sizeof(s->count_str), "%zu/%zu",
                 s->active_match_index + 1, s->match_count);
}

size_t search_session_next_match(SearchSession *s) {
    if (s->match_count == 0) return 0;
    s->active_match_index = (s->active_match_index + 1) % s->match_count;
    snprintf(s->count_str, sizeof(s->count_str), "%zu/%zu",
             s->active_match_index + 1, s->match_count);
    return s->matches[s->active_match_index].start;
}

size_t search_session_prev_match(SearchSession *s) {
    if (s->match_count == 0) return 0;
    s->active_match_index =
        (s->active_match_index + s->match_count - 1) % s->match_count;
    snprintf(s->count_str, sizeof(s->count_str), "%zu/%zu",
             s->active_match_index + 1, s->match_count);
    return s->matches[s->active_match_index].start;
}

size_t search_session_replace_target(const SearchSession *s, size_t cursor_pos) {
    if (s->match_count == 0) return (size_t)-1;

    for (size_t i = 0; i < s->match_count; i++)
        if (cursor_pos >= s->matches[i].start && cursor_pos < s->matches[i].end)
            return i;

    if (s->backward) {
        for (size_t i = s->match_count; i-- > 0; )
            if (s->matches[i].start <= cursor_pos) return i;
        return s->match_count - 1; // wrap: all ahead → last match
    } else {
        for (size_t i = 0; i < s->match_count; i++)
            if (s->matches[i].start >= cursor_pos) return i;
        return 0; // wrap: all behind → first match
    }
}
