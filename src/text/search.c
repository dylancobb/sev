#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

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

static bool whole_word_ok(const char *text, size_t text_len,
                          size_t start, size_t end) {
    bool left_ok  = start == 0 || !is_word_char((unsigned char)text[start - 1]);
    bool right_ok = end == text_len || !is_word_char((unsigned char)text[end]);
    return left_ok && right_ok;
}

static bool append_match(SearchSession *s, size_t start, size_t end) {
    if (s->match_count >= s->match_cap) {
        size_t new_cap = s->match_cap ? s->match_cap * 2 : 64;
        SearchMatch *nm = realloc(s->matches, new_cap * sizeof(SearchMatch));
        if (!nm) return false;
        s->matches   = nm;
        s->match_cap = new_cap;
    }
    s->matches[s->match_count++] = (SearchMatch){ .start = start, .end = end };
    return true;
}

// Compile the query as a PCRE2 pattern. On failure returns NULL and, when
// errbuf is given, fills it with a human-readable message.
static pcre2_code *compile_query(const char *query, size_t query_len,
                                 bool case_sensitive,
                                 char *errbuf, size_t errbuf_size) {
    int errcode;
    PCRE2_SIZE erroff;
    uint32_t opts = PCRE2_UTF | PCRE2_MULTILINE | PCRE2_MATCH_INVALID_UTF;
    if (!case_sensitive) opts |= PCRE2_CASELESS;
    pcre2_code *re = pcre2_compile((PCRE2_SPTR)query, (PCRE2_SIZE)query_len,
                                   opts, &errcode, &erroff, NULL);
    if (!re && errbuf && errbuf_size) {
        PCRE2_UCHAR msg[96];
        pcre2_get_error_message(errcode, msg, sizeof(msg) / sizeof(*msg));
        snprintf(errbuf, errbuf_size, "%s (at offset %zu)",
                 (const char *)msg, (size_t)erroff);
    }
    return re;
}

static void recompute_regex(SearchSession *s, const char *text, size_t text_len,
                            const char *query, size_t query_len,
                            size_t range_start, size_t range_end) {
    pcre2_code *re = compile_query(query, query_len, s->case_sensitive,
                                   s->regex_error, sizeof(s->regex_error));
    if (!re) return;
    pcre2_match_data *md = pcre2_match_data_create_from_pattern(re, NULL);
    if (md) {
        // Subject is the full text truncated at range_end, scanned from a
        // start offset: \b and lookbehind keep their context at range_start.
        PCRE2_SIZE offset = range_start;
        while (offset < range_end) {
            int rc = pcre2_match(re, (PCRE2_SPTR)text, (PCRE2_SIZE)range_end,
                                 offset, 0, md, NULL);
            if (rc < 0) break; // no further match, or hard error
            PCRE2_SIZE *ov = pcre2_get_ovector_pointer(md);
            size_t mstart = (size_t)ov[0], mend = (size_t)ov[1];
            if (mend > mstart &&
                (!s->match_whole_words ||
                 whole_word_ok(text, text_len, mstart, mend))) {
                if (!append_match(s, mstart, mend)) break;
            }
            if (mend > offset) {
                offset = (PCRE2_SIZE)mend;
            } else {
                // Zero-length match: skip a full UTF-8 character so the scan
                // can't loop forever or resume inside a code point.
                offset++;
                while (offset < range_end &&
                       ((unsigned char)text[offset] & 0xC0) == 0x80)
                    offset++;
            }
        }
        pcre2_match_data_free(md);
    }
    pcre2_code_free(re);
}

char *search_regex_expand_replacement(const char *text, size_t text_len,
                                      SearchMatch m, const char *query,
                                      bool case_sensitive,
                                      const char *repl_template) {
    if (!text || !query || !repl_template || m.start >= text_len) return NULL;
    pcre2_code *re = compile_query(query, strlen(query), case_sensitive, NULL, 0);
    if (!re) return NULL;

    uint32_t opts = PCRE2_ANCHORED | PCRE2_SUBSTITUTE_REPLACEMENT_ONLY |
                    PCRE2_SUBSTITUTE_OVERFLOW_LENGTH;
    PCRE2_SIZE outlen = 256;
    char *out = malloc(outlen);
    int rc = out ? pcre2_substitute(re, (PCRE2_SPTR)text, (PCRE2_SIZE)text_len,
                                    (PCRE2_SIZE)m.start, opts, NULL, NULL,
                                    (PCRE2_SPTR)repl_template, PCRE2_ZERO_TERMINATED,
                                    (PCRE2_UCHAR *)out, &outlen)
                 : PCRE2_ERROR_NOMEMORY;
    if (rc == PCRE2_ERROR_NOMEMORY && out) {
        // outlen now holds the required buffer size (incl. terminator).
        char *bigger = realloc(out, outlen);
        if (bigger) {
            out = bigger;
            rc = pcre2_substitute(re, (PCRE2_SPTR)text, (PCRE2_SIZE)text_len,
                                  (PCRE2_SIZE)m.start, opts, NULL, NULL,
                                  (PCRE2_SPTR)repl_template, PCRE2_ZERO_TERMINATED,
                                  (PCRE2_UCHAR *)out, &outlen);
        }
    }
    pcre2_code_free(re);
    if (rc < 1) { // 0 = pattern no longer matches here; <0 = bad template etc.
        free(out);
        return NULL;
    }
    return out;
}

void search_session_recompute(SearchSession *s, const char *text, size_t text_len,
                               const char *query, size_t query_len,
                               size_t range_start, size_t range_end) {
    s->match_count = 0;
    s->regex_error[0] = '\0';
    if (query_len == 0 || text_len == 0) return;
    if (range_start > text_len) range_start = text_len;
    if (range_end > text_len) range_end = text_len;
    if (range_end < range_start) range_end = range_start;

    if (s->use_regex) {
        recompute_regex(s, text, text_len, query, query_len,
                        range_start, range_end);
    } else {
        const char *p   = text + range_start;
        const char *end = text + range_end;
        while (p < end) {
            const char *hit = s->case_sensitive
                ? memmem(p, (size_t)(end - p), query, query_len)
                : find_case_insensitive(p, (size_t)(end - p), query, query_len);
            if (!hit) break;

            size_t hit_start = (size_t)(hit - text);
            if (s->match_whole_words &&
                !whole_word_ok(text, text_len, hit_start, hit_start + query_len)) {
                p = hit + 1;
                continue;
            }

            if (!append_match(s, hit_start, hit_start + query_len)) break;
            p = hit + 1;
        }
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
