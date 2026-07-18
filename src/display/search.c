#include "search.h"

#include <stdlib.h>
#include <string.h>

#include "cursor.h"
#include "icon.h"
#include "pane.h"
#include "text_surface.h"
#include "theme.h"
#include "tooltip.h"
#include "vline.h"
#include "../command/keymap.h"
#include "../command/mode.h"
#include "../command/scheme_internal.h"
#include "../text/buffer.h"
#include "../text/buffer_type.h"
#include "../text/change.h"
#include "../text/search.h"

extern KeyEvent last_event;  // defined in command/keymap.c

static void search_jump_to_active(Pane *pane);
static void search_recompute_current(Pane *pane);

static void HandleSearchPrev(Clay_ElementId id, Clay_PointerData ptr, void *ud) {
    if (ptr.state != CLAY_POINTER_DATA_PRESSED_THIS_FRAME) return;
    Pane *pane = (Pane *)ud;
    if (!pane) return;
    SearchSession *s = &pane->content.search;
    if (!s->active || s->match_count == 0) return;
    search_session_prev_match(s);
    search_jump_to_active(pane);
}

static void HandleSearchNext(Clay_ElementId id, Clay_PointerData ptr, void *ud) {
    if (ptr.state != CLAY_POINTER_DATA_PRESSED_THIS_FRAME) return;
    Pane *pane = (Pane *)ud;
    if (!pane) return;
    SearchSession *s = &pane->content.search;
    if (!s->active || s->match_count == 0) return;
    search_session_next_match(s);
    search_jump_to_active(pane);
}

static void HandleToggleMatchWholeWords(Clay_ElementId id, Clay_PointerData ptr, void *ud) {
    if (ptr.state != CLAY_POINTER_DATA_PRESSED_THIS_FRAME) return;
    Pane *pane = (Pane *)ud;
    if (!pane) return;
    SearchSession *s = &pane->content.search;
    s->match_whole_words = !s->match_whole_words;
    search_recompute_current(pane);
}

static void HandleToggleCaseSensitive(Clay_ElementId id, Clay_PointerData ptr, void *ud) {
    if (ptr.state != CLAY_POINTER_DATA_PRESSED_THIS_FRAME) return;
    Pane *pane = (Pane *)ud;
    if (!pane) return;
    SearchSession *s = &pane->content.search;
    s->case_sensitive = !s->case_sensitive;
    search_recompute_current(pane);
}

static void HandleToggleUseRegex(Clay_ElementId id, Clay_PointerData ptr, void *ud) {
    if (ptr.state != CLAY_POINTER_DATA_PRESSED_THIS_FRAME) return;
    Pane *pane = (Pane *)ud;
    if (!pane) return;
    SearchSession *s = &pane->content.search;
    s->use_regex = !s->use_regex;
    search_recompute_current(pane);
}

static void HandleToggleMatchInSelection(Clay_ElementId id, Clay_PointerData ptr, void *ud) {
    if (ptr.state != CLAY_POINTER_DATA_PRESSED_THIS_FRAME) return;
    Pane *pane = (Pane *)ud;
    if (!pane) return;
    SearchSession *s = &pane->content.search;
    s->match_in_selection = !s->match_in_selection;
    search_recompute_current(pane);
}

static void HandleCloseSearch(Clay_ElementId id, Clay_PointerData ptr, void *ud) {
    if (ptr.state != CLAY_POINTER_DATA_PRESSED_THIS_FRAME) return;
    Pane *pane = (Pane *)ud;
    if (!pane) return;
    SearchSession *s = &pane->content.search;
    s->bar_open      = false;
    s->active        = false;
    s->match_count   = 0;
    s->has_match_range = false;
    s->case_sensitive     = false;
    s->match_whole_words  = false;
    s->match_in_selection = false;
    s->use_regex          = true; // regex is the default; re-arm for next open
    s->regex_error[0]     = '\0';
    s->replace_open  = false;
    s->replace_sel_active = false;
    G->input.current_focus = FOCUS_PANE;
}

static void call_interactively_checked(const char *name) {
    sexp ctx = G->chibi.ctx;
    sexp sym = sexp_intern(ctx, name, -1);
    sexp result = sexp_apply(ctx, G->chibi.call_interactively, sexp_list1(ctx, sym));
    if (sexp_exceptionp(result))
        sexp_print_exception(ctx, result, sexp_current_error_port(ctx));
}

static void HandleToggleReplace(Clay_ElementId id, Clay_PointerData ptr, void *ud) {
    if (ptr.state != CLAY_POINTER_DATA_PRESSED_THIS_FRAME) return;
    Pane *pane = (Pane *)ud;
    if (!pane) return;
    call_interactively_checked("search-toggle-replace");
}

static void HandleReplaceNext(Clay_ElementId id, Clay_PointerData ptr, void *ud) {
    if (ptr.state != CLAY_POINTER_DATA_PRESSED_THIS_FRAME) return;
    Pane *pane = (Pane *)ud;
    if (!pane || pane->content.search.match_count == 0) return;
    call_interactively_checked("search-replace-next");
}

static void HandleReplaceAll(Clay_ElementId id, Clay_PointerData ptr, void *ud) {
    if (ptr.state != CLAY_POINTER_DATA_PRESSED_THIS_FRAME) return;
    Pane *pane = (Pane *)ud;
    if (!pane || pane->content.search.match_count == 0) return;
    call_interactively_checked("search-replace-all");
}

static void SearchBarRow(AppState *state, Pane *pane, int32_t index) {
    SearchSession *s = &pane->content.search;

    float sf = state->ui.scale_factor;
    uint16_t font_sz = (uint16_t)(12 * sf);
    int line_h = vline_get_line_height(&state->rendererData, FONT_BUF_NORMAL, font_sz);

    Buffer *saved = buffer_get_current();
    buffer_set_current(s->query_buf);

    const char *query_text = buffer_text_cached(s->query_buf);
    size_t text_len = query_text ? strlen(query_text) : 0;
    size_t point_pos = point_get(s->query_buf).pos;
    if (point_pos > text_len) point_pos = text_len;

    float cursor_x = (text_len > 0 && point_pos > 0)
        ? text_measure_tab_aware(&state->rendererData, FONT_BUF_NORMAL, font_sz,
                                 query_text, point_pos, 4)
        : 0.0f;

    bool  has_sel = s->sel_active && text_len > 0;
    float sel_x = 0.0f, sel_w = 0.0f;
    if (has_sel) {
        size_t anch    = s->sel_anchor < text_len ? s->sel_anchor : text_len;
        size_t sel_min = anch < point_pos ? anch : point_pos;
        size_t sel_max = anch > point_pos ? anch : point_pos;
        sel_x = sel_min > 0
            ? text_measure_tab_aware(&state->rendererData, FONT_BUF_NORMAL, font_sz,
                                     query_text, sel_min, 4)
            : 0.0f;
        sel_w = sel_max > sel_min
            ? text_measure_tab_aware(&state->rendererData, FONT_BUF_NORMAL, font_sz,
                                     query_text + sel_min, sel_max - sel_min, 4)
            : 0.0f;
        if (sel_w == 0.0f) has_sel = false;
    }

    CLAY(CLAY_IDI_LOCAL("SearchBarRow", index), {
        .layout = {
            .sizing = { .width = CLAY_SIZING_GROW(0) },
            .layoutDirection = CLAY_LEFT_TO_RIGHT,
            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
            .childGap = (uint16_t)(6 * sf),
        },
    }) {
        Clay_String qstr = {
            .chars               = query_text,
            .length              = (int32_t)text_len,
            .isStaticallyAllocated = true,
        };
        CLAY(CLAY_IDI("SearchQueryBox", index), {
                    .layout = {
                        .sizing = { .width = CLAY_SIZING_GROW(0) },
                        .padding = { .top = 4 * sf, .bottom = 4 * sf, .left = 8 * sf, .right = 8 * sf },
                        .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
                        .childGap = 4 * sf
                    },
                    .border = {
                        .color = s->use_regex && s->regex_error[0]
                            ? ui_resolve_color(state, state->ui.roles.search_error)
                            : ui_resolve_color(state, state->ui.roles.border_inactive),
                        .width = CLAY_BORDER_OUTSIDE(2)
                    },
                    .cornerRadius = CLAY_CORNER_RADIUS(5*sf)
                }) {
            if (has_sel) {
                CLAY_AUTO_ID({
                    .floating = {
                        .attachTo = CLAY_ATTACH_TO_PARENT,
                        .offset   = { .x = sel_x + 8 * sf, .y = 4.0f * sf },
                        .zIndex   = 5,
                    },
                    .layout = {
                        .sizing = {
                            .width  = CLAY_SIZING_FIXED(sel_w),
                            .height = CLAY_SIZING_FIXED((float)line_h),
                        }
                    },
                    .backgroundColor = ui_resolve_color(state, state->ui.roles.selection),
                }) {}
            }
            CLAY_TEXT(qstr.length ? qstr : CLAY_STRING("Search..."), CLAY_TEXT_CONFIG({
                .fontId    = FONT_BUF_NORMAL,
                .fontSize  = font_sz,
                .textColor = !qstr.length
                ? ui_resolve_color(state, state->ui.roles.text_faded)
                : (s->match_count == 0
                    ? ui_resolve_color(state, state->ui.roles.search_error)
                    : ui_resolve_color(state, state->ui.roles.text_primary)),
            }));
            if (state->input.current_focus == FOCUS_SEARCH && state->cursor_visible)
                Cursor(state, 0, cursor_x + 8 * sf, 4.0f * sf, line_h,
                       0.0f, 0.0f, 65535.0f, 65535.0f,
                       FONT_BUF_NORMAL, font_sz, 10);

            CLAY_AUTO_ID({
                .layout = { .sizing = { .width = CLAY_SIZING_GROW() } }
            }) {}

            // Case sensitivity toggle
            bool case_hovered = false;
            CLAY(CLAY_IDI_LOCAL("SearchCaseToggle", index), {
                .layout = {
                    .sizing = { .width = CLAY_SIZING_FIXED(15 * sf), .height = CLAY_SIZING_FIXED(15 * sf) },
                    .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
                },
                .cornerRadius    = CLAY_CORNER_RADIUS(4 * sf),
                .backgroundColor = Clay_Hovered()
                    ? ui_resolve_color(state, state->ui.roles.tab_close)
                    : (Clay_Color){0}
            }) {
                SDL_Texture *case_tex = icon_get(
                    s->case_sensitive ? "case-icon-active" : "case-icon", state, 12, 12);
                CLAY_AUTO_ID({
                    .layout = { .sizing = { .width = 12.0f * sf, .height = 12.0f * sf } },
                    .image  = { .imageData = case_tex },
                }) {}
                Clay_OnHover(HandleToggleCaseSensitive, pane);
                case_hovered = Clay_Hovered();
                if (case_hovered)
                    state->input.desired_cursor = SDL_SYSTEM_CURSOR_POINTER;
            }
            char case_binding[64] = {0};
            keymap_where_is_first(state, "search-toggle-case", case_binding, sizeof(case_binding));
            TextTooltipWithBinding(state, case_hovered, index + 1027,
                                   "Match Case Sensitivity", case_binding[0] ? case_binding : NULL);

            // Match whole words toggle
            bool word_hovered = false;
            CLAY(CLAY_IDI_LOCAL("SearchWordToggle", index), {
                .layout = {
                    .sizing = { .width = CLAY_SIZING_FIXED(15 * sf), .height = CLAY_SIZING_FIXED(15 * sf) },
                    .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
                },
                .cornerRadius    = CLAY_CORNER_RADIUS(4 * sf),
                .backgroundColor = Clay_Hovered()
                    ? ui_resolve_color(state, state->ui.roles.tab_close)
                    : (Clay_Color){0}
            }) {
                SDL_Texture *word_tex = icon_get(
                    s->match_whole_words ? "word-icon-active" : "word-icon", state, 12, 12);
                CLAY_AUTO_ID({
                    .layout = { .sizing = { .width = 12.0f * sf, .height = 12.0f * sf } },
                    .image  = { .imageData = word_tex },
                }) {}
                Clay_OnHover(HandleToggleMatchWholeWords, pane);
                word_hovered = Clay_Hovered();
                if (word_hovered)
                    state->input.desired_cursor = SDL_SYSTEM_CURSOR_POINTER;
            }
            char word_binding[64] = {0};
            keymap_where_is_first(state, "search-toggle-whole-words", word_binding, sizeof(word_binding));
            TextTooltipWithBinding(state, word_hovered, index + 1028,
                                   "Match Whole Words", word_binding[0] ? word_binding : NULL);

            // Regular expressions toggle
            bool regex_hovered = false;
            CLAY(CLAY_IDI_LOCAL("SearchRegexToggle", index), {
                .layout = {
                    .sizing = { .width = CLAY_SIZING_FIXED(15 * sf), .height = CLAY_SIZING_FIXED(15 * sf) },
                    .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
                },
                .cornerRadius    = CLAY_CORNER_RADIUS(4 * sf),
                .backgroundColor = Clay_Hovered()
                    ? ui_resolve_color(state, state->ui.roles.tab_close)
                    : (Clay_Color){0}
            }) {
                SDL_Texture *regex_tex = icon_get(
                    s->use_regex ? "regex-icon-active" : "regex-icon", state, 12, 12);
                CLAY_AUTO_ID({
                    .layout = { .sizing = { .width = 12.0f * sf, .height = 12.0f * sf } },
                    .image  = { .imageData = regex_tex },
                }) {}
                Clay_OnHover(HandleToggleUseRegex, pane);
                regex_hovered = Clay_Hovered();
                if (regex_hovered)
                    state->input.desired_cursor = SDL_SYSTEM_CURSOR_POINTER;
            }
            char regex_binding[64] = {0};
            keymap_where_is_first(state, "search-toggle-regex", regex_binding, sizeof(regex_binding));
            TextTooltipWithBinding(state, regex_hovered, index + 1033,
                                   "Use Regular Expressions", regex_binding[0] ? regex_binding : NULL);

        }

        // Toggle to open/close the match replacement UI.
        bool replace_hovered = false;
        CLAY(CLAY_IDI_LOCAL("SearchReplaceToggle", index), {
            .layout = {
                .sizing = { .width = CLAY_SIZING_FIXED(15 * sf), .height = CLAY_SIZING_FIXED(15 * sf) },
                .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
            },
            .cornerRadius    = CLAY_CORNER_RADIUS(4 * sf),
            .backgroundColor = Clay_Hovered()
                ? ui_resolve_color(state, state->ui.roles.tab_close)
                : (Clay_Color){0}
        }) {
            SDL_Texture *replace_tex = icon_get(
                s->replace_open ? "match-replace-icon-active" : "match-replace-icon", state, 12, 12);
            CLAY_AUTO_ID({
                .layout = { .sizing = { .width = 12.0f * sf, .height = 12.0f * sf } },
                .image  = { .imageData = replace_tex },
            }) {}
            Clay_OnHover(HandleToggleReplace, pane);
            replace_hovered = Clay_Hovered();
            if (replace_hovered)
                state->input.desired_cursor = SDL_SYSTEM_CURSOR_POINTER;
        }
        char replace_binding[64] = {0};
        keymap_where_is_first(state, "search-toggle-replace", replace_binding, sizeof(replace_binding));
        TextTooltipWithBinding(state, replace_hovered, index + 1029,
                               "Toggle Replace", replace_binding[0] ? replace_binding : NULL);

        // Toggle for "match in selection", which restricts the search range to
        // the user's active selection when the search UI was opened.
        bool match_in_selection_hovered = false;
        CLAY(CLAY_IDI_LOCAL("SearchMatchInSelectionToggle", index), {
            .layout = {
                .sizing = { .width = CLAY_SIZING_FIXED(15 * sf), .height = CLAY_SIZING_FIXED(15 * sf) },
                .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
            },
            .cornerRadius    = CLAY_CORNER_RADIUS(4 * sf),
            .backgroundColor = Clay_Hovered()
                ? ui_resolve_color(state, state->ui.roles.tab_close)
                : (Clay_Color){0}
        }) {
            SDL_Texture *match_in_selection_tex = icon_get(
                s->match_in_selection ? "match-in-selection-icon-active" : "match-in-selection-icon", state, 12, 12);
            CLAY_AUTO_ID({
                .layout = { .sizing = { .width = 12.0f * sf, .height = 12.0f * sf } },
                .image  = { .imageData = match_in_selection_tex },
            }) {}
            Clay_OnHover(HandleToggleMatchInSelection, pane);
            match_in_selection_hovered = Clay_Hovered();
            if (match_in_selection_hovered)
                state->input.desired_cursor = SDL_SYSTEM_CURSOR_POINTER;
        }
        char match_in_selection_binding[64] = {0};
        keymap_where_is_first(state, "search-toggle-match-in-selection", match_in_selection_binding, sizeof(match_in_selection_binding));
        TextTooltipWithBinding(state, match_in_selection_hovered, index + 1032,
                               "Match In Selection", match_in_selection_binding[0] ? match_in_selection_binding : NULL);

        // Vertical divider
        CLAY_AUTO_ID({
            .layout = {
                .sizing = { .width = CLAY_SIZING_FIXED(2), .height = CLAY_SIZING_FIXED(line_h + 4 * sf) },
            },
            .backgroundColor = ui_resolve_color(state, state->ui.roles.border_inactive),
        }) {}

        bool nav_disabled = !text_len || !s->match_count;

        bool search_prev_hovered = false;
        CLAY_AUTO_ID({
            .layout = {
                .sizing = { .width = CLAY_SIZING_FIXED(15 * sf), .height = CLAY_SIZING_FIXED(15 * sf) },
                .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
            },
            .cornerRadius    = CLAY_CORNER_RADIUS(8 * sf),
            .backgroundColor = (!nav_disabled && Clay_Hovered())
                ? ui_resolve_color(state, state->ui.roles.tab_close)
                : (Clay_Color){0}
        }) {
            SDL_Texture *prev_icon = icon_get(
                nav_disabled ? "caret-left-faded" : "caret-left", state, 10, 10);
            CLAY_AUTO_ID({
                .layout = { .sizing = { .width = 10.0f * sf, .height = 10.0f * sf } },
                .image  = { .imageData = prev_icon },
            }) {}
            if (!nav_disabled) Clay_OnHover(HandleSearchPrev, pane);
            search_prev_hovered = Clay_Hovered();
            if (search_prev_hovered)
                state->input.desired_cursor = nav_disabled
                    ? SDL_SYSTEM_CURSOR_NOT_ALLOWED
                    : SDL_SYSTEM_CURSOR_POINTER;
        }
        char prev_binding[64] = {0};
        keymap_where_is_first(state, "vim-search-prev", prev_binding, sizeof(prev_binding));
        TextTooltipWithBinding(state, search_prev_hovered, index + 1025,
                               "Select Previous Match", prev_binding[0] ? prev_binding : NULL);

        bool search_next_hovered = false;
        CLAY_AUTO_ID({
            .layout = {
                .sizing = { .width = CLAY_SIZING_FIXED(15 * sf), .height = CLAY_SIZING_FIXED(15 * sf) },
                .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
            },
            .cornerRadius    = CLAY_CORNER_RADIUS(8 * sf),
            .backgroundColor = (!nav_disabled && Clay_Hovered())
                ? ui_resolve_color(state, state->ui.roles.tab_close)
                : (Clay_Color){0}
        }) {
            SDL_Texture *next_icon = icon_get(
                nav_disabled ? "caret-right-faded" : "caret-right", state, 10, 10);
            CLAY_AUTO_ID({
                .layout = { .sizing = { .width = 10.0f * sf, .height = 10.0f * sf } },
                .image  = { .imageData = next_icon },
            }) {}
            if (!nav_disabled) Clay_OnHover(HandleSearchNext, pane);
            search_next_hovered = Clay_Hovered();
            if (search_next_hovered)
                state->input.desired_cursor = nav_disabled
                    ? SDL_SYSTEM_CURSOR_NOT_ALLOWED
                    : SDL_SYSTEM_CURSOR_POINTER;
        }
        char next_binding[64] = {0};
        keymap_where_is_first(state, "vim-search-next", next_binding, sizeof(next_binding));
        TextTooltipWithBinding(state, search_next_hovered, index + 1026,
                               "Select Next Match", next_binding[0] ? next_binding : NULL);

        // count_str is pre-formatted and stored in the SearchSession (stable per-pane memory).
        const char *count_chars = text_len == 0 ? "0/0" : s->count_str;
        Clay_String cstr = {
            .chars               = count_chars,
            .length              = (int32_t)strlen(count_chars),
            .isStaticallyAllocated = true,
        };
        CLAY_TEXT(cstr, CLAY_TEXT_CONFIG({
            .fontId    = FONT_UI_NORMAL,
            .fontSize  = (uint16_t)(10 * sf),
            .textColor = text_len && s->match_count
              ? ui_resolve_color(state, state->ui.roles.text_primary)
              : ui_resolve_color(state, state->ui.roles.text_faded),
        }));

        CLAY_AUTO_ID({
            .layout = { .sizing = { .width = CLAY_SIZING_FIXED(30 * sf) } }
        }) {}

        bool search_close_hovered = false;
        CLAY_AUTO_ID({
            .layout = {
                .sizing = { .width = CLAY_SIZING_FIXED(15 * sf), .height = CLAY_SIZING_FIXED(15 * sf) },
                .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
            },
            .cornerRadius    = CLAY_CORNER_RADIUS(8 * sf),
            .backgroundColor = Clay_Hovered()
                ? ui_resolve_color(state, state->ui.roles.tab_close)
                : (Clay_Color){0}
        }) {
            SDL_Texture *icon = icon_get("tab-close", state, 7, 7);
            CLAY_AUTO_ID({
                .layout = { .sizing = { .width = 7.0f * sf, .height = 7.0f * sf } },
                .image  = { .imageData = icon },
            }) {}
            Clay_OnHover(HandleCloseSearch, pane);
            search_close_hovered = Clay_Hovered();
        }
        char binding[64] = {0};
        keymap_where_is_first(state, "search-cancel", binding, sizeof(binding));
        TextTooltipWithBinding(state, search_close_hovered, index + 1024,
                               "Close Search Bar", binding[0] ? binding : NULL);
    }

    buffer_set_current(saved);
}

// Shown beneath the search row while the regex pattern fails to compile.
// regex_error is stable per-pane memory in the SearchSession, like count_str.
static void SearchErrorRow(AppState *state, Pane *pane, int32_t index) {
    SearchSession *s = &pane->content.search;
    float sf = state->ui.scale_factor;

    CLAY(CLAY_IDI_LOCAL("SearchErrorRow", index), {
        .layout = {
            .sizing = { .width = CLAY_SIZING_GROW(0) },
            .layoutDirection = CLAY_LEFT_TO_RIGHT,
            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
            .padding = {
                .left  = (uint16_t)(8 * sf),
                .right = (uint16_t)(8 * sf),
                .top   = (uint16_t)(4 * sf),
            },
        },
    }) {
        Clay_String estr = {
            .chars               = s->regex_error,
            .length              = (int32_t)strlen(s->regex_error),
            .isStaticallyAllocated = true,
        };
        CLAY_TEXT(estr, CLAY_TEXT_CONFIG({
            .fontId    = FONT_UI_NORMAL,
            .fontSize  = (uint16_t)(10 * sf),
            .textColor = ui_resolve_color(state, state->ui.roles.search_error),
        }));
    }
}

static void ReplaceBarRow(AppState *state, Pane *pane, int32_t index) {
    SearchSession *s = &pane->content.search;

    float sf = state->ui.scale_factor;
    uint16_t font_sz = (uint16_t)(12 * sf);
    int line_h = vline_get_line_height(&state->rendererData, FONT_BUF_NORMAL, font_sz);

    Buffer *saved = buffer_get_current();
    buffer_set_current(s->replace_buf);

    const char *replace_text = buffer_text_cached(s->replace_buf);
    size_t text_len = replace_text ? strlen(replace_text) : 0;
    size_t point_pos = point_get(s->replace_buf).pos;
    if (point_pos > text_len) point_pos = text_len;

    float cursor_x = (text_len > 0 && point_pos > 0)
        ? text_measure_tab_aware(&state->rendererData, FONT_BUF_NORMAL, font_sz,
                                 replace_text, point_pos, 4)
        : 0.0f;

    bool  has_sel = s->replace_sel_active && text_len > 0;
    float sel_x = 0.0f, sel_w = 0.0f;
    if (has_sel) {
        size_t anch    = s->replace_sel_anchor < text_len ? s->replace_sel_anchor : text_len;
        size_t sel_min = anch < point_pos ? anch : point_pos;
        size_t sel_max = anch > point_pos ? anch : point_pos;
        sel_x = sel_min > 0
            ? text_measure_tab_aware(&state->rendererData, FONT_BUF_NORMAL, font_sz,
                                     replace_text, sel_min, 4)
            : 0.0f;
        sel_w = sel_max > sel_min
            ? text_measure_tab_aware(&state->rendererData, FONT_BUF_NORMAL, font_sz,
                                     replace_text + sel_min, sel_max - sel_min, 4)
            : 0.0f;
        if (sel_w == 0.0f) has_sel = false;
    }

    // Tighter gap when the regex error row sits directly above.
    bool error_above = s->use_regex && s->regex_error[0];

    CLAY(CLAY_IDI_LOCAL("ReplaceBarRow", index), {
        .layout = {
            .sizing = { .width = CLAY_SIZING_GROW(0) },
            .layoutDirection = CLAY_LEFT_TO_RIGHT,
            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
            .childGap = (uint16_t)(6 * sf),
            .padding = { .top = (uint16_t)((error_above ? 4 : 8) * sf) },
        },
    }) {
        Clay_String rstr = {
            .chars               = replace_text,
            .length              = (int32_t)text_len,
            .isStaticallyAllocated = true,
        };
        // Match the query box's width so both boxes' right edges line up.
        // Falls back to GROW on the frame before the query box's layout is
        // first available (mirrors BufRender_SetupGeometry's pattern).
        Clay_ElementData qbox_data = Clay_GetElementData(CLAY_IDI("SearchQueryBox", index));
        Clay_SizingAxis replace_box_width = qbox_data.found
            ? CLAY_SIZING_FIXED(qbox_data.boundingBox.width)
            : CLAY_SIZING_GROW(0);
        if (!qbox_data.found) state->needs_extra_frame = true;
        CLAY_AUTO_ID({
                    .layout = {
                        .sizing = { .width = replace_box_width },
                        .padding = { .top = 4 * sf, .bottom = 4 * sf, .left = 8 * sf, .right = 8 * sf },
                        .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
                        .childGap = 4 * sf
                    },
                    .border = {
                        .color = ui_resolve_color(state, state->ui.roles.border_inactive),
                        .width = CLAY_BORDER_OUTSIDE(2)
                    },
                    .cornerRadius = CLAY_CORNER_RADIUS(5*sf)
                }) {
            if (has_sel) {
                CLAY_AUTO_ID({
                    .floating = {
                        .attachTo = CLAY_ATTACH_TO_PARENT,
                        .offset   = { .x = sel_x + 8 * sf, .y = 4.0f * sf },
                        .zIndex   = 5,
                    },
                    .layout = {
                        .sizing = {
                            .width  = CLAY_SIZING_FIXED(sel_w),
                            .height = CLAY_SIZING_FIXED((float)line_h),
                        }
                    },
                    .backgroundColor = ui_resolve_color(state, state->ui.roles.selection),
                }) {}
            }
            CLAY_TEXT(rstr.length ? rstr : CLAY_STRING("Replace..."), CLAY_TEXT_CONFIG({
                .fontId    = FONT_BUF_NORMAL,
                .fontSize  = font_sz,
                .textColor = !rstr.length
                ? ui_resolve_color(state, state->ui.roles.text_faded)
                : ui_resolve_color(state, state->ui.roles.text_primary),
            }));
            if (state->input.current_focus == FOCUS_REPLACE && state->cursor_visible)
                Cursor(state, 0, cursor_x + 8 * sf, 4.0f * sf, line_h,
                       0.0f, 0.0f, 65535.0f, 65535.0f,
                       FONT_BUF_NORMAL, font_sz, 10);

            CLAY_AUTO_ID({
                .layout = { .sizing = { .width = CLAY_SIZING_GROW() } }
            }) {}
        }

        bool replace_disabled = s->match_count == 0;

        bool replace_next_hovered = false;
        CLAY_AUTO_ID({
            .layout = {
                .sizing = { .width = CLAY_SIZING_FIXED(15 * sf), .height = CLAY_SIZING_FIXED(15 * sf) },
                .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
            },
            .cornerRadius    = CLAY_CORNER_RADIUS(8 * sf),
            .backgroundColor = (!replace_disabled && Clay_Hovered())
                ? ui_resolve_color(state, state->ui.roles.tab_close)
                : (Clay_Color){0}
        }) {
            SDL_Texture *next_icon = icon_get(
                replace_disabled ? "replace-next-icon-faded" : "replace-next-icon", state, 12, 12);
            CLAY_AUTO_ID({
                .layout = { .sizing = { .width = 12.0f * sf, .height = 12.0f * sf } },
                .image  = { .imageData = next_icon },
            }) {}
            if (!replace_disabled) Clay_OnHover(HandleReplaceNext, pane);
            replace_next_hovered = Clay_Hovered();
            if (replace_next_hovered)
                state->input.desired_cursor = replace_disabled
                    ? SDL_SYSTEM_CURSOR_NOT_ALLOWED
                    : SDL_SYSTEM_CURSOR_POINTER;
        }
        char replace_next_binding[64] = {0};
        keymap_where_is_first(state, "search-replace-next", replace_next_binding, sizeof(replace_next_binding));
        TextTooltipWithBinding(state, replace_next_hovered, index + 1030,
                               "Replace Next Match", replace_next_binding[0] ? replace_next_binding : NULL);

        bool replace_all_hovered = false;
        CLAY_AUTO_ID({
            .layout = {
                .sizing = { .width = CLAY_SIZING_FIXED(15 * sf), .height = CLAY_SIZING_FIXED(15 * sf) },
                .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
            },
            .cornerRadius    = CLAY_CORNER_RADIUS(8 * sf),
            .backgroundColor = (!replace_disabled && Clay_Hovered())
                ? ui_resolve_color(state, state->ui.roles.tab_close)
                : (Clay_Color){0}
        }) {
            SDL_Texture *all_icon = icon_get(
                replace_disabled ? "replace-all-icon-faded" : "replace-all-icon", state, 12, 12);
            CLAY_AUTO_ID({
                .layout = { .sizing = { .width = 12.0f * sf, .height = 12.0f * sf } },
                .image  = { .imageData = all_icon },
            }) {}
            if (!replace_disabled) Clay_OnHover(HandleReplaceAll, pane);
            replace_all_hovered = Clay_Hovered();
            if (replace_all_hovered)
                state->input.desired_cursor = replace_disabled
                    ? SDL_SYSTEM_CURSOR_NOT_ALLOWED
                    : SDL_SYSTEM_CURSOR_POINTER;
        }
        char replace_all_binding[64] = {0};
        keymap_where_is_first(state, "search-replace-all", replace_all_binding, sizeof(replace_all_binding));
        TextTooltipWithBinding(state, replace_all_hovered, index + 1031,
                               "Replace All Matches", replace_all_binding[0] ? replace_all_binding : NULL);
    }

    buffer_set_current(saved);
}

void SearchBar(AppState *state, Pane *pane, int32_t index) {
    SearchSession *s = &pane->content.search;
    if (!s->bar_open || !s->query_buf) return;

    float sf = state->ui.scale_factor;
    bool error_visible   = s->use_regex && s->regex_error[0];
    bool replace_visible = s->replace_open && s->replace_buf;

    // Rows supply their own top padding instead of a uniform childGap so the
    // error row can sit tighter (4 * sf) than the normal row spacing (8 * sf).
    CLAY(CLAY_IDI_LOCAL("SearchBar", index), {
        .layout = {
            .sizing = { .width = CLAY_SIZING_GROW(0) },
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
            .padding = {
                .left   = (uint16_t)(8 * sf),
                .right  = (uint16_t)(8 * sf),
                .top    = (uint16_t)(8 * sf),
                .bottom = (uint16_t)((error_visible && !replace_visible ? 4 : 8) * sf),
            },
        },
        .backgroundColor = ui_resolve_color(state, state->ui.roles.pane_bg),
        .border = {
            .width = { .bottom = 2 },
            .color = ui_resolve_color(state, state->ui.roles.border_active),
        },
    }) {
        SearchBarRow(state, pane, index);
        if (error_visible)
            SearchErrorRow(state, pane, index);
        if (replace_visible)
            ReplaceBarRow(state, pane, index);
    }
}

static void search_selection_delete(SearchSession *s) {
    if (!s->sel_active || !s->query_buf) return;
    Buffer *saved = buffer_get_current();
    buffer_set_current(s->query_buf);
    size_t point_pos = point_get(s->query_buf).pos;
    size_t anch      = s->sel_anchor;
    size_t sel_min   = anch < point_pos ? anch : point_pos;
    size_t sel_max   = anch > point_pos ? anch : point_pos;
    if (sel_max > sel_min) {
        Location loc = { .pos = sel_max };
        point_set(loc);
        delete_chars(s->query_buf, (int)(sel_max - sel_min));
    }
    s->sel_active = false;
    buffer_set_current(saved);
}

static void replace_selection_delete(SearchSession *s) {
    if (!s->replace_sel_active || !s->replace_buf) return;
    Buffer *saved = buffer_get_current();
    buffer_set_current(s->replace_buf);
    size_t point_pos = point_get(s->replace_buf).pos;
    size_t anch      = s->replace_sel_anchor;
    size_t sel_min   = anch < point_pos ? anch : point_pos;
    size_t sel_max   = anch > point_pos ? anch : point_pos;
    if (sel_max > sel_min) {
        Location loc = { .pos = sel_max };
        point_set(loc);
        delete_chars(s->replace_buf, (int)(sel_max - sel_min));
    }
    s->replace_sel_active = false;
    buffer_set_current(saved);
}

// Deletes [m.start, m.end) in buf and inserts repl in its place. Point ends
// after the inserted text, mirroring insert_string's contract.
static void search_replace_match(Buffer *buf, SearchMatch m, const char *repl) {
    point_set((Location){ .pos = m.start });
    update_line(buf);
    size_t del_count = m.end - m.start;
    for (size_t i = 0; i < del_count; i++) delete_chars(buf, -1);
    insert_string(buf, repl);
}

static void search_recompute_current(Pane *pane) {
    if (!pane || !pane->content.active_tab) return;
    Buffer *buf = pane->content.active_tab->content.buffer.buffer;
    if (!buf) return;
    SearchSession *s = &pane->content.search;
    if (!s->query_buf) return;
    const char *query = buffer_text_cached(s->query_buf);
    size_t query_len = query ? strlen(query) : 0;
    const char *text = buffer_text_cached(buf);
    size_t text_len = strlen(text);

    size_t range_start = 0, range_end = text_len;
    if (s->match_in_selection && s->has_match_range) {
        range_start = s->match_range_start;
        range_end   = s->match_range_end;
        if (range_start > text_len) range_start = text_len;
        if (range_end   > text_len) range_end   = text_len;
    }
    search_session_recompute(s, text, text_len, query, query_len, range_start, range_end);
}

static void search_jump_to_active(Pane *pane) {
    if (!pane) return;
    SearchSession *s = &pane->content.search;
    if (s->match_count == 0) return;
    Buffer *buf = buffer_get_current();
    if (!buf) return;
    Location loc = { .pos = s->matches[s->active_match_index].start };
    point_set(loc);
    update_line(buf);
    save_current_column(buf);
}

void search_handle_key(AppState *state, const KeyEvent *ev) {
    Pane *pane = pane_get_active();
    if (!pane) { state->input.current_focus = FOCUS_PANE; return; }
    SearchSession *s = &pane->content.search;
    if (!s->query_buf) return;

    if (ev->type == KEYEVENT_SPECIAL) {
        switch (ev->keycode) {
        case KEY_BACKSPACE:
            delete_chars(s->query_buf, 1);
            search_recompute_current(pane);
            break;
        case KEY_RETURN:
            state->input.current_focus = FOCUS_PANE;
            search_jump_to_active(pane);
            break;
        case KEY_ESC:
            s->bar_open    = false;
            s->active      = false;
            s->match_count = 0;
            s->sel_active  = false;
            s->has_match_range = false;
            s->case_sensitive     = false;
            s->match_whole_words  = false;
            s->match_in_selection = false;
            s->use_regex          = true; // regex is the default; re-arm for next open
            s->regex_error[0]     = '\0';
            buffer_clear(s->query_buf);
            s->text_scroll = 0.0f;
            state->input.current_focus = FOCUS_PANE;
            break;
        default:
            break;
        }
        return;
    }

    if (ev->type == KEYEVENT_CHAR && ev->mods == MOD_NONE) {
        insert_codepoint(s->query_buf, ev->codepoint);
        search_recompute_current(pane);
    }
}

// --- Scheme bindings ---

static void search_open_impl(Pane *pane, bool backward) {
    SearchSession *s = &pane->content.search;

    if (!s->query_buf) {
        s->query_buf = buffer_create("*search-query*");
        if (!s->query_buf) return;
        Mode *vim = mode_lookup("vim-normal-mode", MODE_MINOR);
        if (vim) buffer_disable_minor_mode(s->query_buf, vim);
        Mode *sm = mode_lookup("search-mode", MODE_MINOR);
        if (sm) buffer_enable_minor_mode(s->query_buf, sm);
        s->use_regex = true; // regex matching is the default for a fresh session
    }

    Buffer *buf = buffer_get_current();
    s->point    = buf ? point_get(buf).pos : 0;
    s->active   = true;
    s->bar_open = true;
    s->backward = backward;

    // Snapshot the active visual selection (if any) as the frozen restrict
    // range for "match in selection". Only set, never clear here: if the
    // user re-opens search with no live selection (e.g. after leaving
    // visual mode but before fully closing the search bar), the range from
    // the original selection should keep restricting matches.
    if (buf && buf->select_mode != SELECT_NONE) {
        size_t a = buf->select_start.pos, b = point_get(buf).pos;
        s->match_range_start = a < b ? a : b;
        s->match_range_end   = a > b ? a : b;
        s->has_match_range   = true;
    }

    const char *q = buffer_text_cached(s->query_buf);
    size_t qlen = q ? strlen(q) : 0;
    if (qlen > 0) {
        buffer_set_current(s->query_buf);
        Location end = { .pos = qlen };
        point_set(end);
        if (buf) buffer_set_current(buf);
        s->sel_anchor = 0;
        s->sel_active = true;
    } else {
        s->sel_active = false;
    }

    G->input.current_focus = FOCUS_SEARCH;
    search_recompute_current(pane);
}

sexp scm_search_open(sexp ctx, sexp self, sexp n) {
    Pane *pane = pane_get_active();
    if (!pane) return SEXP_VOID;
    search_open_impl(pane, false);
    return SEXP_VOID;
}

sexp scm_search_open_backward(sexp ctx, sexp self, sexp n) {
    Pane *pane = pane_get_active();
    if (!pane) return SEXP_VOID;
    search_open_impl(pane, true);
    return SEXP_VOID;
}

sexp scm_search_self_insert(sexp ctx, sexp self, sexp n) {
    Pane *pane = pane_get_active();
    if (!pane) return SEXP_VOID;
    SearchSession *s = &pane->content.search;
    if (!s->query_buf || last_event.type != KEYEVENT_CHAR) return SEXP_VOID;
    if (s->sel_active) search_selection_delete(s);
    insert_codepoint(s->query_buf, last_event.codepoint);
    search_recompute_current(pane);
    return SEXP_VOID;
}

sexp scm_search_backspace(sexp ctx, sexp self, sexp n) {
    Pane *pane = pane_get_active();
    if (!pane) return SEXP_VOID;
    SearchSession *s = &pane->content.search;
    if (!s->query_buf) return SEXP_VOID;
    if (s->sel_active) {
        search_selection_delete(s);
    } else {
        delete_chars(s->query_buf, 1);
    }
    search_recompute_current(pane);
    return SEXP_VOID;
}

sexp scm_search_forward_char(sexp ctx, sexp self, sexp n) {
    Pane *pane = pane_get_active();
    if (!pane) return SEXP_VOID;
    SearchSession *s = &pane->content.search;
    if (!s->query_buf) return SEXP_VOID;
    s->sel_active = false;
    Buffer *saved = buffer_get_current();
    buffer_set_current(s->query_buf);
    point_move(1);
    buffer_set_current(saved);
    return SEXP_VOID;
}

sexp scm_search_backward_char(sexp ctx, sexp self, sexp n) {
    Pane *pane = pane_get_active();
    if (!pane) return SEXP_VOID;
    SearchSession *s = &pane->content.search;
    if (!s->query_buf) return SEXP_VOID;
    s->sel_active = false;
    Buffer *saved = buffer_get_current();
    buffer_set_current(s->query_buf);
    point_move(-1);
    buffer_set_current(saved);
    return SEXP_VOID;
}

sexp scm_search_shift_forward_char(sexp ctx, sexp self, sexp n) {
    Pane *pane = pane_get_active();
    if (!pane) return SEXP_VOID;
    SearchSession *s = &pane->content.search;
    if (!s->query_buf) return SEXP_VOID;
    Buffer *saved = buffer_get_current();
    buffer_set_current(s->query_buf);
    if (!s->sel_active) {
        s->sel_anchor = point_get(s->query_buf).pos;
        s->sel_active = true;
    }
    point_move(1);
    buffer_set_current(saved);
    return SEXP_VOID;
}

sexp scm_search_shift_backward_char(sexp ctx, sexp self, sexp n) {
    Pane *pane = pane_get_active();
    if (!pane) return SEXP_VOID;
    SearchSession *s = &pane->content.search;
    if (!s->query_buf) return SEXP_VOID;
    Buffer *saved = buffer_get_current();
    buffer_set_current(s->query_buf);
    if (!s->sel_active) {
        s->sel_anchor = point_get(s->query_buf).pos;
        s->sel_active = true;
    }
    point_move(-1);
    buffer_set_current(saved);
    return SEXP_VOID;
}

sexp scm_search_confirm(sexp ctx, sexp self, sexp n) {
    Pane *pane = pane_get_active();
    if (!pane) return SEXP_VOID;
    SearchSession *s = &pane->content.search;
    s->sel_active = false;
    // Keep bar_open so the counter stays visible; only close on escape.
    G->input.current_focus = FOCUS_PANE;
    search_jump_to_active(pane);
    return SEXP_VOID;
}

sexp scm_search_cancel(sexp ctx, sexp self, sexp n) {
    Pane *pane = pane_get_active();
    if (!pane) return SEXP_VOID;
    SearchSession *s = &pane->content.search;
    s->bar_open      = false;
    s->active        = false;
    s->match_count   = 0;
    s->sel_active    = false;
    s->has_match_range = false;
    s->case_sensitive     = false;
    s->match_whole_words  = false;
    s->match_in_selection = false;
    s->use_regex          = true; // regex is the default; re-arm for next open
    s->regex_error[0]     = '\0';
    s->replace_open  = false;
    s->replace_sel_active = false;
    G->input.current_focus = FOCUS_PANE;
    return SEXP_VOID;
}

sexp scm_search_next(sexp ctx, sexp self, sexp n) {
    Pane *pane = pane_get_active();
    if (!pane) return SEXP_VOID;
    SearchSession *s = &pane->content.search;
    if (!s->active || s->match_count == 0) return SEXP_VOID;
    if (s->backward) search_session_prev_match(s);
    else             search_session_next_match(s);
    search_jump_to_active(pane);
    return SEXP_VOID;
}

sexp scm_search_prev(sexp ctx, sexp self, sexp n) {
    Pane *pane = pane_get_active();
    if (!pane) return SEXP_VOID;
    SearchSession *s = &pane->content.search;
    if (!s->active || s->match_count == 0) return SEXP_VOID;
    if (s->backward) search_session_next_match(s);
    else             search_session_prev_match(s);
    search_jump_to_active(pane);
    return SEXP_VOID;
}

sexp scm_search_bar_open_p(sexp ctx, sexp self, sexp n) {
    Pane *pane = pane_get_active();
    if (!pane) return SEXP_FALSE;
    return pane->content.search.bar_open ? SEXP_TRUE : SEXP_FALSE;
}

sexp scm_search_toggle_case(sexp ctx, sexp self, sexp n) {
    Pane *pane = pane_get_active();
    if (!pane) return SEXP_VOID;
    SearchSession *s = &pane->content.search;
    s->case_sensitive = !s->case_sensitive;
    search_recompute_current(pane);
    return SEXP_VOID;
}

sexp scm_search_toggle_whole_words(sexp ctx, sexp self, sexp n) {
    Pane *pane = pane_get_active();
    if (!pane) return SEXP_VOID;
    SearchSession *s = &pane->content.search;
    s->match_whole_words = !s->match_whole_words;
    search_recompute_current(pane);
    return SEXP_VOID;
}

sexp scm_search_toggle_regex(sexp ctx, sexp self, sexp n) {
    Pane *pane = pane_get_active();
    if (!pane) return SEXP_VOID;
    SearchSession *s = &pane->content.search;
    s->use_regex = !s->use_regex;
    search_recompute_current(pane);
    return SEXP_VOID;
}

sexp scm_search_toggle_match_in_selection(sexp ctx, sexp self, sexp n) {
    Pane *pane = pane_get_active();
    if (!pane) return SEXP_VOID;
    SearchSession *s = &pane->content.search;
    s->match_in_selection = !s->match_in_selection;
    search_recompute_current(pane);
    return SEXP_VOID;
}

sexp scm_search_toggle_replace(sexp ctx, sexp self, sexp n) {
    Pane *pane = pane_get_active();
    if (!pane) return SEXP_VOID;
    SearchSession *s = &pane->content.search;
    if (!s->bar_open) return SEXP_VOID;

    if (s->replace_open) {
        s->replace_open = false;
        s->replace_sel_active = false;
        G->input.current_focus = FOCUS_SEARCH;
        return SEXP_VOID;
    }

    if (!s->replace_buf) {
        s->replace_buf = buffer_create("*search-replace*");
        if (!s->replace_buf) return SEXP_VOID;
        Mode *vim = mode_lookup("vim-normal-mode", MODE_MINOR);
        if (vim) buffer_disable_minor_mode(s->replace_buf, vim);
        Mode *rm = mode_lookup("replace-mode", MODE_MINOR);
        if (rm) buffer_enable_minor_mode(s->replace_buf, rm);
    }

    s->replace_open = true;
    const char *r = buffer_text_cached(s->replace_buf);
    size_t rlen = r ? strlen(r) : 0;
    if (rlen > 0) {
        Buffer *saved = buffer_get_current();
        buffer_set_current(s->replace_buf);
        Location end = { .pos = rlen };
        point_set(end);
        if (saved) buffer_set_current(saved);
        s->replace_sel_anchor = 0;
        s->replace_sel_active = true;
    } else {
        s->replace_sel_active = false;
    }

    G->input.current_focus = FOCUS_REPLACE;
    return SEXP_VOID;
}

sexp scm_replace_self_insert(sexp ctx, sexp self, sexp n) {
    Pane *pane = pane_get_active();
    if (!pane) return SEXP_VOID;
    SearchSession *s = &pane->content.search;
    if (!s->replace_buf || last_event.type != KEYEVENT_CHAR) return SEXP_VOID;
    if (s->replace_sel_active) replace_selection_delete(s);
    insert_codepoint(s->replace_buf, last_event.codepoint);
    return SEXP_VOID;
}

sexp scm_replace_backspace(sexp ctx, sexp self, sexp n) {
    Pane *pane = pane_get_active();
    if (!pane) return SEXP_VOID;
    SearchSession *s = &pane->content.search;
    if (!s->replace_buf) return SEXP_VOID;
    if (s->replace_sel_active) {
        replace_selection_delete(s);
    } else {
        delete_chars(s->replace_buf, 1);
    }
    return SEXP_VOID;
}

sexp scm_replace_forward_char(sexp ctx, sexp self, sexp n) {
    Pane *pane = pane_get_active();
    if (!pane) return SEXP_VOID;
    SearchSession *s = &pane->content.search;
    if (!s->replace_buf) return SEXP_VOID;
    s->replace_sel_active = false;
    Buffer *saved = buffer_get_current();
    buffer_set_current(s->replace_buf);
    point_move(1);
    buffer_set_current(saved);
    return SEXP_VOID;
}

sexp scm_replace_backward_char(sexp ctx, sexp self, sexp n) {
    Pane *pane = pane_get_active();
    if (!pane) return SEXP_VOID;
    SearchSession *s = &pane->content.search;
    if (!s->replace_buf) return SEXP_VOID;
    s->replace_sel_active = false;
    Buffer *saved = buffer_get_current();
    buffer_set_current(s->replace_buf);
    point_move(-1);
    buffer_set_current(saved);
    return SEXP_VOID;
}

sexp scm_replace_shift_forward_char(sexp ctx, sexp self, sexp n) {
    Pane *pane = pane_get_active();
    if (!pane) return SEXP_VOID;
    SearchSession *s = &pane->content.search;
    if (!s->replace_buf) return SEXP_VOID;
    Buffer *saved = buffer_get_current();
    buffer_set_current(s->replace_buf);
    if (!s->replace_sel_active) {
        s->replace_sel_anchor = point_get(s->replace_buf).pos;
        s->replace_sel_active = true;
    }
    point_move(1);
    buffer_set_current(saved);
    return SEXP_VOID;
}

sexp scm_replace_shift_backward_char(sexp ctx, sexp self, sexp n) {
    Pane *pane = pane_get_active();
    if (!pane) return SEXP_VOID;
    SearchSession *s = &pane->content.search;
    if (!s->replace_buf) return SEXP_VOID;
    Buffer *saved = buffer_get_current();
    buffer_set_current(s->replace_buf);
    if (!s->replace_sel_active) {
        s->replace_sel_anchor = point_get(s->replace_buf).pos;
        s->replace_sel_active = true;
    }
    point_move(-1);
    buffer_set_current(saved);
    return SEXP_VOID;
}

sexp scm_search_replace_next(sexp ctx, sexp self, sexp n) {
    Pane *pane = pane_get_active();
    if (!pane) return SEXP_VOID;
    SearchSession *s = &pane->content.search;
    if (!s->active || s->match_count == 0 || !s->replace_buf) return SEXP_VOID;

    Buffer *buf = buffer_get_current();
    if (!buf) return SEXP_VOID;
    size_t cursor_pos = point_get(buf).pos;

    size_t idx = search_session_replace_target(s, cursor_pos);
    if (idx == (size_t)-1) return SEXP_VOID;

    const char *repl = buffer_text_cached(s->replace_buf);
    if (!repl) repl = "";

    char *expanded = NULL;
    if (s->use_regex) {
        const char *text  = buffer_text_cached(buf);
        const char *query = buffer_text_cached(s->query_buf);
        if (text && query)
            expanded = search_regex_expand_replacement(text, strlen(text),
                                                       s->matches[idx], query,
                                                       s->case_sensitive, repl);
    }

    change_begin(buf);
    // Expansion failure (e.g. bad template) falls back to the literal text.
    search_replace_match(buf, s->matches[idx], expanded ? expanded : repl);
    change_end(buf);
    free(expanded);

    s->point = point_get(buf).pos;
    search_recompute_current(pane);
    return SEXP_VOID;
}

sexp scm_search_replace_all(sexp ctx, sexp self, sexp n) {
    Pane *pane = pane_get_active();
    if (!pane) return SEXP_VOID;
    SearchSession *s = &pane->content.search;
    if (!s->active || s->match_count == 0 || !s->replace_buf) return SEXP_VOID;

    Buffer *buf = buffer_get_current();
    if (!buf) return SEXP_VOID;

    const char *repl = buffer_text_cached(s->replace_buf);
    if (!repl) repl = "";

    // Expand capture-group references against the pre-edit text before any
    // mutation; a failed expansion falls back to the literal text.
    char **expanded = NULL;
    if (s->use_regex) {
        const char *text  = buffer_text_cached(buf);
        const char *query = buffer_text_cached(s->query_buf);
        if (text && query) {
            expanded = calloc(s->match_count, sizeof(char *));
            if (expanded) {
                size_t text_len = strlen(text);
                for (size_t i = 0; i < s->match_count; i++)
                    expanded[i] = search_regex_expand_replacement(
                        text, text_len, s->matches[i], query,
                        s->case_sensitive, repl);
            }
        }
    }

    change_begin(buf);
    // Back-to-front: replacement length may differ from match length, so
    // earlier (lower-offset) matches must be processed after later ones to
    // keep their stored offsets valid without a recompute mid-loop.
    for (size_t i = s->match_count; i-- > 0; ) {
        const char *r = (expanded && expanded[i]) ? expanded[i] : repl;
        search_replace_match(buf, s->matches[i], r);
    }
    change_end(buf);

    if (expanded) {
        for (size_t i = 0; i < s->match_count; i++) free(expanded[i]);
        free(expanded);
    }

    s->point = point_get(buf).pos;
    search_recompute_current(pane);
    return SEXP_VOID;
}
