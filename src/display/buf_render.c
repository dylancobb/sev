#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "buf_render.h"
#include "cursor.h"
#include "pane.h"
#include "tab.h"
#include "theme.h"
#include "vline.h"
#include "text_surface.h"
#include "../clay/renderer.h"
#include "../text/buffer.h"
#include "../text/buffer_type.h"
#include "../text/diff.h"
#include "../text/line.h"

// --- Shared rendering context ---
//
// Populated at the start of BufferContentRender and passed by pointer to all
// helper functions, avoiding large parameter lists.

typedef struct {
    AppState        *state;
    ContentPane     *cp;
    Tab             *tab;
    int32_t          index;
    Buffer          *buf;
    char            *chars;
    size_t           point;
    const LineTable *lt;
    VLineCache      *cache;
    uint16_t         font_id;
    uint16_t         font_size;
    float            padding;
    TTF_Font        *font;
    int              line_num_type;     // 0=off 1=abs 2=rel 3=visual
    int              tab_width;
    bool             wrap_lines;        // false = nowrap + hscroll
    float            scroll_x;         // horizontal scroll offset (0 in wrap mode)
    float            gutter_width;
    float            diff_marker_w;        // fixed px width of the change-slot stripe
    const uint8_t   *diff_markers;         // per-logical-line flags (LineChangeKind), or NULL
    int              diff_markers_count;
    size_t           cursor_logical_line;
    char            *lnum_strs;         // slab for formatted line number strings
    Clay_BoundingBox box;
    float            text_height;
    int              line_height;
    size_t           first_vline;
    size_t           end_vline;
    Clay_ElementId   outer_id;
    Clay_ElementId   track_id;
    Clay_ElementId   content_pane_id;
    int32_t          run_id;            // monotonically increasing Clay segment ID
    size_t           bracket_hl_a;     // cursor bracket byte pos ((size_t)-1 if none)
    size_t           bracket_hl_b;     // match bracket byte pos  ((size_t)-1 if none)
} BufRenderCtx;



// --- File-local state ---

static Clay_Sizing tab_layout_expand = {
    .width  = CLAY_SIZING_GROW(0),
    .height = CLAY_SIZING_GROW(0)
};

static ScissoredRectData sel_pool[SCISSORED_RECT_POOL_SIZE];
static int sel_pool_idx = 0;

static ScissoredRectData match_range_pool[SCISSORED_RECT_POOL_SIZE];
static int match_range_pool_idx = 0;

static TriangleRenderData tri_pool[TRIANGLE_POOL_SIZE];
static int tri_pool_idx = 0;

#define SEARCH_RECT_POOL_SIZE 2048
static ScissoredRectData srch_pool[SEARCH_RECT_POOL_SIZE];
static int srch_pool_idx = 0;

#define LNUM_STR_LEN 12

void buf_render_reset(void) {
    sel_pool_idx  = 0;
    tri_pool_idx  = 0;
    srch_pool_idx = 0;
    match_range_pool_idx = 0;
}

// --- Static helpers ---

static sexp hl_kind_to_role(AppState *state, uint16_t kind) {
    switch ((HLKind)kind) {
    case HL_KEYWORD:  return state->ui.roles.hl_keyword;
    case HL_STRING:   return state->ui.roles.hl_string;
    case HL_COMMENT:  return state->ui.roles.hl_comment;
    case HL_NUMBER:   return state->ui.roles.hl_number;
    case HL_CONSTANT: return state->ui.roles.hl_constant;
    case HL_FUNCTION: return state->ui.roles.hl_function;
    case HL_BUILTIN:  return state->ui.roles.hl_builtin;
    case HL_OPERATOR: return state->ui.roles.hl_operator;
    case HL_BRACKET:   return state->ui.roles.hl_bracket;
    case HL_PROPERTY:  return state->ui.roles.hl_property;
    default:           return state->ui.roles.text_primary;
    }
}



// --- Component functions ---

// Returns the fractional pixel sub-line offset for smooth scrolling.
// Computed before the outer CLAY() container so it can be passed to .clip.
static float BufRender_ComputeSubOffset(BufRenderCtx *ctx) {
    float sub_offset = 0.0f;
    int lh = vline_get_line_height(&ctx->state->rendererData,
                                   ctx->font_id, ctx->font_size);
    VLineCache *vc = ctx->cache;
    if (lh > 0 && vc->count > 0) {
        size_t first = (size_t)(vc->scroll_offset / lh);
        sub_offset = vc->scroll_offset - (float)first * (float)lh;
    }
    return sub_offset;
}

// Populates all geometry fields of ctx from Clay element data.
// Returns false (and sets needs_extra_frame) if layout data isn't ready yet.
static bool BufRender_SetupGeometry(BufRenderCtx *ctx, Clay_ElementId id) {
    Clay_ElementData data = Clay_GetElementData(id);
    if (!data.found || data.boundingBox.width <= 0) {
        ctx->state->needs_extra_frame = true;
        return false;
    }
    Clay_BoundingBox box = data.boundingBox;
    float text_width  = box.width - (2 * ctx->padding);
    float text_height = box.height;
    ctx->box         = box;
    ctx->text_height = text_height;

    TTF_Font *font = SDL_Clay_GetRenderFont(&ctx->state->rendererData,
                                            ctx->font_id, (float)ctx->font_size);
    ctx->font = font;

    // Determine line-number display mode from buffer-local variable.
    sexp lnum_sym = sexp_intern(ctx->state->chibi.ctx, "display-line-numbers-type", -1);
    sexp lnum_val = vartable_get(buffer_get_locals(ctx->buf), lnum_sym, SEXP_FALSE);
    int line_num_type = 0;
    if (lnum_val == SEXP_TRUE) {
        line_num_type = 1;
    } else if (sexp_symbolp(lnum_val)) {
        sexp c = ctx->state->chibi.ctx;
        if      (lnum_val == sexp_intern(c, "relative", -1)) line_num_type = 2;
        else if (lnum_val == sexp_intern(c, "visual",   -1)) line_num_type = 3;
    }
    ctx->line_num_type = line_num_type;

    // Measure gutter width for line numbers.
    float gutter_width = 0;
    if (line_num_type) {
        char num_buf[20];
        int ndigits = snprintf(num_buf, sizeof(num_buf), "%zu", ctx->lt->count);
        if (ndigits < 3) ndigits = 3;
        memset(num_buf, '0', ndigits);
        num_buf[ndigits] = '\0';
        int w = 0, h = 0;
        TTF_GetStringSize(font, num_buf, ndigits, &w, &h);
        gutter_width = (float)w + ctx->padding;
    }
    ctx->gutter_width = gutter_width;

    // Fixed slot along the gutter's left edge for diff change markers.
    // Width is carved out of ctx->padding, so total gutter-col width is
    // unchanged — digits stay put regardless of whether the slot is
    // drawing a colour or is transparent.
    float marker_w = 5.0f * ctx->state->ui.scale_factor;
    if (marker_w < 2.0f) marker_w = 2.0f;
    if (marker_w > ctx->padding) marker_w = ctx->padding;
    ctx->diff_marker_w = marker_w;

    // Read tab-width from buffer-local variable.
    sexp tabw_sym = sexp_intern(ctx->state->chibi.ctx, "tab-width", -1);
    sexp tabw_val = vartable_get(buffer_get_locals(ctx->buf), tabw_sym, SEXP_FALSE);
    int tab_width = (sexp_fixnump(tabw_val) && sexp_unbox_fixnum(tabw_val) > 0)
                    ? (int)sexp_unbox_fixnum(tabw_val) : 4;
    ctx->tab_width = tab_width;

    // Read wrap-lines from buffer-local variable (default: true).
    sexp wl_sym = sexp_intern(ctx->state->chibi.ctx, "wrap-lines", -1);
    sexp wl_val = vartable_get(buffer_get_locals(ctx->buf), wl_sym, SEXP_TRUE);
    bool wrap_lines = (wl_val != SEXP_FALSE);
    ctx->wrap_lines = wrap_lines;

    VLineCache *cache = ctx->cache;
    vline_rebuild(cache, ctx->buf, &ctx->state->rendererData,
                  text_width - gutter_width, ctx->font_id, ctx->font_size,
                  tab_width, wrap_lines, ctx->state->render_gen);

    int line_height  = vline_get_line_height(&ctx->state->rendererData,
                                             ctx->font_id, ctx->font_size);
    ctx->line_height = line_height;

    // Store geometry in ContentPane for mouse hit-testing.
    ContentPane *cp      = ctx->cp;
    cp->text_origin_x    = box.x + ctx->padding + gutter_width;
    cp->text_origin_y    = box.y + 2;
    cp->text_origin_w    = text_width - gutter_width;
    cp->text_origin_h    = text_height;
    cp->gutter_width_px  = gutter_width;
    cp->line_height_px   = line_height;
    cp->render_font_id   = ctx->font_id;
    cp->render_font_size = ctx->font_size;

    Clay_ElementData outer_data = Clay_GetElementData(ctx->outer_id);
    if (outer_data.found) {
        cp->pane_left  = outer_data.boundingBox.x;
        cp->pane_right = outer_data.boundingBox.x + outer_data.boundingBox.width;
    }
    Clay_ElementData cpane_data = Clay_GetElementData(ctx->content_pane_id);
    if (cpane_data.found) {
        cp->pane_top    = cpane_data.boundingBox.y;
        cp->pane_bottom = cpane_data.boundingBox.y + cpane_data.boundingBox.height;
    }
    Clay_ElementData track_data = Clay_GetElementData(ctx->track_id);
    if (track_data.found)
        cp->scrollbar_x = track_data.boundingBox.x;
    cp->scrollbar_y       = box.y;
    cp->scrollbar_track_h = text_height;

    // Scroll to keep cursor visible when this pane is active.
    size_t point = ctx->point;
    if (cp->active && point != cache->last_scroll_point) {
        cache->last_scroll_point = point;
        float old_scroll_offset = cache->scroll_offset;
        if (line_height > 0)
            vline_scroll_to_cursor_pixels(cache, point, text_height, line_height, 3);
        if (cache->scroll_offset != old_scroll_offset)
            ctx->state->needs_extra_frame = true;

        // Horizontal scroll-to-cursor in nowrap mode.
        if (!wrap_lines) {
            size_t cursor_vline = vline_for_byte_pos(cache, point);
            if (cursor_vline < cache->count) {
                const VisualLine *cvl = &cache->lines[cursor_vline];
                size_t head_len = point > cvl->byte_start ? point - cvl->byte_start : 0;
                float cursor_x = 0.0f;
                if (head_len > 0)
                    cursor_x += text_measure_tab_aware(&ctx->state->rendererData, ctx->font_id, ctx->font_size,
                                                  ctx->chars + cvl->byte_start,
                                                  head_len, tab_width);
                float textcol_w = box.width - (ctx->padding + gutter_width);
                float old_scroll_x = cache->scroll_x;
                vline_scroll_x_to_cursor_pixels(cache, cursor_x, textcol_w);
                if (cache->scroll_x != old_scroll_x)
                    ctx->state->needs_extra_frame = true;
            }
        }
    }

    float max_scroll = (cache->count > 1)
        ? (float)(cache->count - 1) * line_height : 0.0f;
    cache->scroll_offset = fmaxf(0.0f, fminf(cache->scroll_offset, max_scroll));

    size_t first_vline = 0;
    if (line_height > 0)
        first_vline = (size_t)(cache->scroll_offset / line_height);
    ctx->first_vline = first_vline;

    size_t visible_count = line_height > 0
        ? (size_t)(text_height / line_height) + 2 : 1;
    size_t end_vline = first_vline + visible_count;
    if (end_vline > cache->count) end_vline = cache->count;
    ctx->end_vline = end_vline;

    ctx->cursor_logical_line = 0;
    if (line_num_type)
        ctx->cursor_logical_line = line_index_at(ctx->lt, point);

    ctx->lnum_strs = NULL;
    if (line_num_type) {
        ctx->lnum_strs = malloc((end_vline - first_vline) * LNUM_STR_LEN);
        tab_register_string(ctx->lnum_strs);
    }

    return true;
}

// Centered "Loading..." placeholder shown while Clay layout data isn't ready.
static void BufRender_LoadingPlaceholder(BufRenderCtx *ctx) {
    CLAY_AUTO_ID({
        .layout = {
            .sizing = tab_layout_expand,
            .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }
        }
    }) {
        CLAY_TEXT(CLAY_STRING("Loading..."),
            CLAY_TEXT_CONFIG({
                .fontId        = FONT_BUF_NORMAL,
                .fontSize      = 16,
                .textColor     = ui_resolve_color(ctx->state,
                                     ctx->state->ui.roles.text_faded),
                .textAlignment = CLAY_TEXT_ALIGN_CENTER
            }));
    }
}

// Renders the line-number gutter cell for visual line i.
static bool vline_in_selection(BufRenderCtx *ctx, VisualLine *vl) {
    if (ctx->buf->select_mode == SELECT_NONE || !ctx->cp->active) return false;
    size_t sel_a   = ctx->buf->select_start.pos;
    size_t sel_b   = ctx->point;
    size_t sel_min = sel_a < sel_b ? sel_a : sel_b;
    size_t sel_max = sel_a > sel_b ? sel_a + 1 : sel_b + 1;
    switch (ctx->buf->select_mode) {
    case SELECT_REGULAR:
        return vl->byte_start < sel_max && vl->byte_end >= sel_min;
    case SELECT_LINE: {
        size_t lmin = line_index_at(ctx->lt, sel_min);
        size_t lmax = line_index_at(ctx->lt, sel_max > 0 ? sel_max - 1 : 0);
        size_t vll  = line_index_at(ctx->lt, vl->byte_start);
        return vll >= lmin && vll <= lmax;
    }
    case SELECT_RECTANGLE:
    case SELECT_RECTANGLE_RAGGED: {
        size_t la  = line_index_at(ctx->lt, sel_a);
        size_t lb  = line_index_at(ctx->lt, sel_b);
        size_t vll = line_index_at(ctx->lt, vl->byte_start);
        return vll >= (la < lb ? la : lb) && vll <= (la > lb ? la : lb);
    }
    default: return false;
    }
}

static void BufRender_GutterCell(BufRenderCtx *ctx, size_t i, VisualLine *vl) {
    bool cursor_on_line = (ctx->point >= vl->byte_start) && (ctx->point < vl->byte_end)
                          && !ctx->state->minibuf.active;
    bool is_last = (i + 1 == ctx->cache->count) ||
                   (ctx->cache->lines[i + 1].line_id != vl->line_id);
    if (!cursor_on_line && ctx->point == vl->byte_end && is_last && !ctx->state->minibuf.active) {
        if (i + 1 == ctx->cache->count || ctx->cache->lines[i + 1].byte_start != ctx->point)
            cursor_on_line = true;
    }
    Clay_Color bg = ui_resolve_color(ctx->state, ctx->state->ui.roles.line_bg);

    size_t logical = line_index_at(ctx->lt, vl->byte_start);

    size_t str_idx    = i - ctx->first_vline;
    char  *str        = ctx->lnum_strs ? ctx->lnum_strs + str_idx * LNUM_STR_LEN : NULL;
    size_t slen       = 0;
    bool   show_number = ctx->line_num_type && str;

    if (show_number) {
        if (ctx->line_num_type == 3) {
            slen = snprintf(str, LNUM_STR_LEN, "%zu", i - ctx->first_vline + 1);
        } else if (vl->visual_index > 0) {
            show_number = false;
        } else {
            if (ctx->line_num_type == 1) {
                slen = snprintf(str, LNUM_STR_LEN, "%zu", logical + 1);
            } else {
                size_t rel = (logical > ctx->cursor_logical_line)
                    ? logical - ctx->cursor_logical_line
                    : ctx->cursor_logical_line - logical;
                slen = snprintf(str, LNUM_STR_LEN, "%zu",
                                rel == 0 ? logical + 1 : rel);
            }
        }
    }

    // Diff flags for this logical line (bitfield of LineChangeKind).
    uint8_t diff_flags = 0;
    if (ctx->diff_markers && (int)logical < ctx->diff_markers_count)
        diff_flags = ctx->diff_markers[logical];

    // Change-slot colour: transparent unless the line is added/modified.
    Clay_Color slot_bg = (Clay_Color){0};
    if (diff_flags & LINE_CHANGE_ADDED)
        slot_bg = ui_resolve_color(ctx->state, ctx->state->ui.roles.diff_added);
    else if (diff_flags & LINE_CHANGE_MODIFIED)
        slot_bg = ui_resolve_color(ctx->state, ctx->state->ui.roles.diff_modified);

    // Deletion squares only sit on the first visual row of a logical line,
    // so wrapped lines don't get duplicate squares.
    bool draw_del_above = (diff_flags & LINE_CHANGE_DEL_ABOVE) && vl->visual_index == 0;
    bool draw_del_below = (diff_flags & LINE_CHANGE_DEL_BELOW) && is_last;

    float marker_w    = ctx->diff_marker_w;
    float left_pad    = ctx->padding - marker_w;
    if (left_pad < 0) left_pad = 0;

    CLAY(CLAY_IDI_LOCAL("LineNum", (int32_t)i), {
        .layout = {
            .sizing = {
                .width  = CLAY_SIZING_GROW(0),
                .height = CLAY_SIZING_FIXED(ctx->line_height)
            },
            .layoutDirection = CLAY_LEFT_TO_RIGHT,
        },
        .backgroundColor = (cursor_on_line && ctx->buf->select_mode == SELECT_NONE)
            ? (Clay_Color){ bg.r, bg.g, bg.b, 128 } : (Clay_Color){0},
    }) {
        // Change-slot: fixed-width strip at the left edge. Always laid out
        // (so digit alignment is stable), coloured only when there is a
        // change on this line.
        CLAY(CLAY_IDI_LOCAL("DiffSlot", (int32_t)i), {
            .layout = {
                .sizing = { .width  = CLAY_SIZING_FIXED(marker_w),
                            .height = CLAY_SIZING_GROW(0) },
            },
            .backgroundColor = slot_bg,
        }) {
            if (draw_del_above && tri_pool_idx < TRIANGLE_POOL_SIZE) {
                TriangleRenderData *t = &tri_pool[tri_pool_idx++];
                t->type  = CUSTOM_TYPE_TRIANGLE;
                t->half  = TRIANGLE_HALF_TL;
                t->color = ui_resolve_color(ctx->state,
                    ctx->state->ui.roles.diff_deleted);
                CLAY(CLAY_IDI_LOCAL("DelAbove", (int32_t)i), {
                    .floating = {
                        .attachTo = CLAY_ATTACH_TO_PARENT,
                        .attachPoints = {
                            .element = CLAY_ATTACH_POINT_LEFT_TOP,
                            .parent  = CLAY_ATTACH_POINT_LEFT_TOP,
                        },
                        .zIndex = 5,
                    },
                    .layout = {
                        .sizing = { .width  = CLAY_SIZING_FIXED(marker_w),
                                    .height = CLAY_SIZING_FIXED(marker_w) },
                    },
                    .custom = { .customData = t },
                }) {}
            }
            if (draw_del_below && tri_pool_idx < TRIANGLE_POOL_SIZE) {
                TriangleRenderData *t = &tri_pool[tri_pool_idx++];
                t->type  = CUSTOM_TYPE_TRIANGLE;
                t->half  = TRIANGLE_HALF_BL;
                t->color = ui_resolve_color(ctx->state,
                    ctx->state->ui.roles.diff_deleted);
                CLAY(CLAY_IDI_LOCAL("DelBelow", (int32_t)i), {
                    .floating = {
                        .attachTo = CLAY_ATTACH_TO_PARENT,
                        .attachPoints = {
                            .element = CLAY_ATTACH_POINT_LEFT_BOTTOM,
                            .parent  = CLAY_ATTACH_POINT_LEFT_BOTTOM,
                        },
                        .offset = { .y = 1.0 },
                        .zIndex = 5,
                    },
                    .layout = {
                        .sizing = { .width  = CLAY_SIZING_FIXED(marker_w),
                                    .height = CLAY_SIZING_FIXED(marker_w) },
                    },
                    .custom = { .customData = t },
                }) {}
            }
        }
        // Number column: remaining gutter width, right-aligned digits.
        CLAY(CLAY_IDI_LOCAL("LineNumText", (int32_t)i), {
            .layout = {
                .sizing = {
                    .width  = CLAY_SIZING_GROW(0),
                    .height = CLAY_SIZING_GROW(0),
                },
                .padding        = ctx->line_num_type
                    ? (Clay_Padding){ .left = (uint16_t)left_pad,
                                      .right = (uint16_t)ctx->padding }
                    : (Clay_Padding){0},
                .childAlignment = { .x = CLAY_ALIGN_X_RIGHT,
                                    .y = CLAY_ALIGN_Y_CENTER },
            },
        }) {
            if (show_number && slen > 0) {
                bool is_current = (ctx->line_num_type != 3) &&
                    (logical == ctx->cursor_logical_line);
                bool in_sel = vline_in_selection(ctx, vl);
                Clay_String numStr = { .chars = str, .length = slen };
                CLAY_TEXT(numStr, CLAY_TEXT_CONFIG({
                    .fontId    = ctx->font_id,
                    .fontSize  = ctx->font_size,
                    .textColor = ui_resolve_color(ctx->state,
                        (is_current || in_sel) && ctx->cp->active
                            ? ctx->state->ui.roles.text_linenum
                            : ctx->state->ui.roles.text_faded),
                }));
            }
        }
    }
}

// Renders the cursor overlay for the current line.
// Determines the correct font variant from hl_spans before calling Cursor().
static void BufRender_CursorCell(BufRenderCtx *ctx, size_t i, float cursor_offset) {
    uint16_t cursor_font_id = ctx->font_id;
    size_t pt_line_idx      = line_index_at(ctx->lt, ctx->point);
    const Line *pt_line     = &ctx->lt->lines[pt_line_idx];
    for (uint32_t si = 0; si < pt_line->hl_span_count; si++) {
        const HLSpan *sp = &pt_line->hl_spans[si];
        if ((uint32_t)ctx->point >= sp->start_byte &&
            (uint32_t)ctx->point <  sp->end_byte) {
            TextStyle ts = ui_resolve_text_style(
                ctx->state, hl_kind_to_role(ctx->state, sp->style),
                ctx->font_id, ctx->font_size);
            cursor_font_id = ts.font_id;
            break;
        }
    }
    Cursor(ctx->state, (int32_t)i, cursor_offset, 0.0f,
           ctx->line_height,
           ctx->box.x + ctx->padding + ctx->gutter_width, ctx->box.y,
           ctx->box.width - ctx->padding - ctx->gutter_width, ctx->text_height,
           cursor_font_id, ctx->font_size, 0);
}

// Renders the frozen "match in selection" range highlight, independent of
// the live vim-selection state — it stays visible after the user leaves
// visual mode, until the search bar is fully closed. Reuses roles.selection
// so it visually stacks (brighter) where it overlaps the live selection.
static void BufRender_MatchRangeCell(BufRenderCtx *ctx, size_t i, VisualLine *vl) {
    SearchSession *s = &ctx->cp->search;
    if (!s->active || !s->match_in_selection || !s->has_match_range) return;

    size_t line_start = vl->byte_start;
    size_t line_end   = vl->byte_end;
    size_t rng_min     = s->match_range_start;
    size_t rng_max     = s->match_range_end;
    if (rng_max <= rng_min) return;
    if (line_start >= rng_max || line_end <= rng_min) return;

    size_t hl_start = rng_min > line_start ? rng_min : line_start;
    size_t hl_end   = rng_max < line_end   ? rng_max : line_end;
    if (hl_end <= hl_start) return;

    float mr_x = 0.0f;
    if (hl_start > line_start) {
        mr_x = text_measure_tab_aware(&ctx->state->rendererData, ctx->font_id, ctx->font_size,
                                      ctx->chars + line_start,
                                      hl_start - line_start, ctx->tab_width);
    }
    float mr_w = text_measure_tab_aware(&ctx->state->rendererData, ctx->font_id, ctx->font_size,
                                        ctx->chars + hl_start,
                                        hl_end - hl_start, ctx->tab_width);
    if (mr_w <= 0 || match_range_pool_idx >= SCISSORED_RECT_POOL_SIZE) return;

    ScissoredRectData *mr = &match_range_pool[match_range_pool_idx++];
    mr->type   = CUSTOM_TYPE_SCISSORED_RECT;
    mr->clip_x = ctx->box.x + ctx->padding + ctx->gutter_width;
    mr->clip_y = ctx->box.y;
    mr->clip_w = ctx->box.width - ctx->padding - ctx->gutter_width;
    mr->clip_h = ctx->text_height;
    mr->color  = ui_resolve_color(ctx->state, ctx->state->ui.roles.selection);
    CLAY(CLAY_IDI_LOCAL("MatchRange", (int32_t)i), {
        .floating = {
            .attachTo = CLAY_ATTACH_TO_PARENT,
            .offset   = { .x = mr_x, .y = 0 }
        },
        .layout = {
            .sizing = {
                .width  = CLAY_SIZING_FIXED(mr_w),
                .height = CLAY_SIZING_FIXED(ctx->line_height)
            }
        },
        .custom = { .customData = mr },
    }) {}
}

// Renders the selection highlight overlay for the current line.
// Handles SELECT_REGULAR, SELECT_LINE, and SELECT_RECTANGLE modes.
static void BufRender_SelectionCell(BufRenderCtx *ctx, size_t i, VisualLine *vl) {
    if (ctx->buf->select_mode == SELECT_NONE || !ctx->cp->active) return;

    size_t line_start = vl->byte_start;
    size_t line_end   = vl->byte_end;
    size_t sel_a      = ctx->buf->select_start.pos;
    size_t sel_b      = ctx->point;
    size_t sel_min    = sel_a < sel_b ? sel_a : sel_b;
    size_t sel_max    = sel_a > sel_b ? sel_a + 1 : sel_b + 1;

    size_t hl_start    = 0, hl_end = 0;
    bool   do_highlight = false;

    switch (ctx->buf->select_mode) {
    case SELECT_REGULAR: {
        if (line_start < sel_max && line_end >= sel_min) {
            hl_start     = sel_min > line_start ? sel_min : line_start;
            hl_end       = sel_max < line_end   ? sel_max : line_end;
            do_highlight = true;
        }
        break;
    }
    case SELECT_LINE: {
        size_t line_min   = line_index_at(ctx->lt, sel_min);
        size_t line_max   = line_index_at(ctx->lt, sel_max > 0 ? sel_max - 1 : 0);
        size_t vl_logical = line_index_at(ctx->lt, vl->byte_start);
        if (vl_logical >= line_min && vl_logical <= line_max) {
            hl_start     = line_start;
            hl_end       = line_end;
            do_highlight = true;
        }
        break;
    }
    case SELECT_RECTANGLE_RAGGED:
    case SELECT_RECTANGLE: {
        size_t line_a  = line_index_at(ctx->lt, sel_a);
        size_t line_b  = line_index_at(ctx->lt, sel_b);
        size_t row_min = line_a < line_b ? line_a : line_b;
        size_t row_max = line_a > line_b ? line_a : line_b;
        size_t col_a   = sel_a - ctx->lt->lines[line_a].start;
        size_t col_b   = sel_b - ctx->lt->lines[line_b].start;
        size_t col_min = col_a < col_b ? col_a : col_b;
        size_t col_max = col_a > col_b ? col_a : col_b;

        size_t vl_logical = line_index_at(ctx->lt, vl->byte_start);
        if (vl_logical >= row_min && vl_logical <= row_max) {
            size_t log_start = ctx->lt->lines[vl_logical].start;
            size_t log_end   = ctx->lt->lines[vl_logical].end;
            size_t hs        = log_start + col_min;
            size_t he;
            if (ctx->buf->select_mode == SELECT_RECTANGLE_RAGGED) {
                he = log_end;
                if (he > log_start && ctx->chars[he - 1] == '\n') he--;
            } else {
                he = log_start + col_max + 1;
            }
            if (he > log_end)    he = log_end;
            if (hs > log_end)    hs = log_end;
            if (hs < line_start) hs = line_start;
            if (he > line_end)   he = line_end;
            if (he > hs) {
                hl_start     = hs;
                hl_end       = he;
                do_highlight = true;
            }
        }
        break;
    }
    default: break;
    }

    if (!do_highlight || hl_end <= hl_start) return;

    float sel_x = 0.0f;
    if (hl_start > line_start) {
        sel_x += text_measure_tab_aware(&ctx->state->rendererData, ctx->font_id, ctx->font_size,
                                   ctx->chars + line_start,
                                   hl_start - line_start, ctx->tab_width);
    }
    float sel_w = text_measure_tab_aware(&ctx->state->rendererData, ctx->font_id, ctx->font_size,
                                    ctx->chars + hl_start,
                                    hl_end - hl_start, ctx->tab_width);
    if (sel_w > 0 && sel_pool_idx < SCISSORED_RECT_POOL_SIZE) {
        ScissoredRectData *s = &sel_pool[sel_pool_idx++];
        s->type   = CUSTOM_TYPE_SCISSORED_RECT;
        s->clip_x = ctx->box.x + ctx->padding + ctx->gutter_width;
        s->clip_y = ctx->box.y;
        s->clip_w = ctx->box.width - ctx->padding - ctx->gutter_width;
        s->clip_h = ctx->text_height;
        s->color  = ui_resolve_color(ctx->state, ctx->state->ui.roles.selection);
        CLAY(CLAY_IDI_LOCAL("Sel", (int32_t)i), {
            .floating = {
                .attachTo = CLAY_ATTACH_TO_PARENT,
                .offset   = { .x = sel_x, .y = 0 }
            },
            .layout = {
                .sizing = {
                    .width  = CLAY_SIZING_FIXED(sel_w),
                    .height = CLAY_SIZING_FIXED(ctx->line_height)
                }
            },
            .custom = { .customData = s },
        }) {}
    }
}


static void BufRender_SearchCell(BufRenderCtx *ctx, size_t i, VisualLine *vl) {
    SearchSession *s = &ctx->cp->search;
    if (!s->active || s->match_count == 0) return;

    size_t line_start = vl->byte_start;
    size_t line_end   = vl->byte_end;

    for (size_t m = 0; m < s->match_count; m++) {
        size_t ms = s->matches[m].start;
        size_t me = s->matches[m].end;

        if (me <= line_start || ms >= line_end) continue;

        size_t hl_start = ms > line_start ? ms : line_start;
        size_t hl_end   = me < line_end   ? me : line_end;
        if (hl_end <= hl_start) continue;

        float srch_x = 0.0f;
        if (hl_start > line_start) {
            srch_x = text_measure_tab_aware(&ctx->state->rendererData, ctx->font_id, ctx->font_size,
                                            ctx->chars + line_start,
                                            hl_start - line_start, ctx->tab_width);
        }
        float srch_w = text_measure_tab_aware(&ctx->state->rendererData, ctx->font_id, ctx->font_size,
                                              ctx->chars + hl_start,
                                              hl_end - hl_start, ctx->tab_width);
        if (srch_w <= 0 || srch_pool_idx >= SEARCH_RECT_POOL_SIZE) continue;

        bool is_active = (m == s->active_match_index);
        sexp role = is_active ? ctx->state->ui.roles.hl_search_active
                              : ctx->state->ui.roles.hl_search;

        ScissoredRectData *sr = &srch_pool[srch_pool_idx++];
        sr->type   = CUSTOM_TYPE_SCISSORED_RECT;
        sr->clip_x = ctx->box.x + ctx->padding + ctx->gutter_width;
        sr->clip_y = ctx->box.y;
        sr->clip_w = ctx->box.width - ctx->padding - ctx->gutter_width;
        sr->clip_h = ctx->text_height;
        sr->color  = ui_resolve_color(ctx->state, role);
        CLAY(CLAY_IDI_LOCAL("Srch", (int32_t)(i * 128 + (int32_t)m)), {
            .floating = {
                .attachTo = CLAY_ATTACH_TO_PARENT,
                .offset   = { .x = srch_x, .y = 0 }
            },
            .layout = {
                .sizing = {
                    .width  = CLAY_SIZING_FIXED(srch_w),
                    .height = CLAY_SIZING_FIXED(ctx->line_height)
                }
            },
            .custom = { .customData = sr },
        }) {}
    }
}

// Emit a segment [seg_s, seg_e) that may contain tab characters, emitting text and tab blocks.
#define EMIT_SEGMENT(seg_s, seg_e, role_expr) do { \
    TextStyle _er_style = ui_resolve_text_style(ctx->state, (role_expr), \
                                                FONT_BUF_NORMAL, 15); \
    text_emit_tab_aware(&ctx->state->rendererData, &ctx->run_id, &x_accum, \
                        _er_style.font_id, ctx->font_size, ctx->tab_width, ctx->line_height, \
                        _er_style.color, _er_style.bg_color, \
                        ctx->chars + (seg_s), (seg_e) - (seg_s)); \
} while(0)

// Like EMIT_SEGMENT but overrides the 1-byte bracket positions bk_a/bk_b with bk_role.
// Requires locals bk_a, bk_b (size_t), bk_role (sexp), x_accum (float).
#define EMIT_SEGMENT_MAYBE_BK(s, e, base_role) do { \
    size_t _ms = (s), _me = (e), _cur = _ms; \
    size_t _bklo = (bk_a != (size_t)-1 && (bk_b == (size_t)-1 || bk_a <= bk_b)) ? bk_a : bk_b; \
    size_t _bkhi = (bk_a != (size_t)-1 && bk_b != (size_t)-1) \
                   ? (bk_a <= bk_b ? bk_b : bk_a) : (size_t)-1; \
    if (_bklo != (size_t)-1 && _bklo >= _cur && _bklo < _me) { \
        if (_bklo > _cur) EMIT_SEGMENT(_cur, _bklo, (base_role)); \
        size_t _be = (_bklo + 1 < _me) ? _bklo + 1 : _me; \
        EMIT_SEGMENT(_bklo, _be, bk_role); \
        _cur = _be; \
    } \
    if (_bkhi != (size_t)-1 && _bkhi >= _cur && _bkhi < _me) { \
        if (_bkhi > _cur) EMIT_SEGMENT(_cur, _bkhi, (base_role)); \
        size_t _be = (_bkhi + 1 < _me) ? _bkhi + 1 : _me; \
        EMIT_SEGMENT(_bkhi, _be, bk_role); \
        _cur = _be; \
    } \
    if (_cur < _me) EMIT_SEGMENT(_cur, _me, (base_role)); \
} while(0)

// Renders syntax-highlighted text segments for one visual line.
// Tabs are rendered as variable-width spacers aligned to tab stops.
static void BufRender_TextCell(BufRenderCtx *ctx, VisualLine *vl) {
    size_t line_start = vl->byte_start;
    size_t line_end   = vl->byte_end;
    if (line_end == line_start) return;

    // x_accum tracks pixel offset from the left edge of the visual line.
    float x_accum = 0.0f;

    // Bracket highlight override positions (may be (size_t)-1 if not active).
    size_t bk_a    = ctx->bracket_hl_a;
    size_t bk_b    = ctx->bracket_hl_b;
    sexp   bk_role = ctx->state->ui.roles.hl_bracket_match;

    size_t log_idx       = line_index_at(ctx->lt, vl->byte_start);
    const Line *log_line = &ctx->lt->lines[log_idx];
    size_t pos           = line_start;

    for (uint32_t si = 0; si < log_line->hl_span_count; si++) {
        const HLSpan *span = &log_line->hl_spans[si];
        if ((size_t)span->end_byte   <= line_start) continue;
        if ((size_t)span->start_byte >= line_end)   break;
        size_t seg_s = (size_t)span->start_byte > line_start
                     ? (size_t)span->start_byte : line_start;
        size_t seg_e = (size_t)span->end_byte < line_end
                     ? (size_t)span->end_byte  : line_end;
        if (pos < seg_s)
            EMIT_SEGMENT_MAYBE_BK(pos, seg_s, ctx->state->ui.roles.text_primary);
        EMIT_SEGMENT_MAYBE_BK(seg_s, seg_e, hl_kind_to_role(ctx->state, span->style));
        pos = seg_e;
    }
    if (pos < line_end)
        EMIT_SEGMENT_MAYBE_BK(pos, line_end, ctx->state->ui.roles.text_primary);
}

// Renders a single visual line: gutter cell, cursor overlay, selection overlay,
// and syntax-highlighted text.
// Renders cursor, selection, and text for one visual line inside the text column.
// The gutter is rendered separately in BufRender_GutterCell.
static void BufRender_TextRow(BufRenderCtx *ctx, size_t i) {
    VisualLine *vl    = &ctx->cache->lines[i];
    size_t line_start = vl->byte_start;
    size_t line_end   = vl->byte_end;

    bool cursor_on_line = (ctx->point >= line_start) && (ctx->point < line_end)
                          && !ctx->state->minibuf.active;
    bool is_last = (i + 1 == ctx->cache->count) ||
                   (ctx->cache->lines[i + 1].line_id != vl->line_id);
    if (!cursor_on_line && ctx->point == line_end && is_last && !ctx->state->minibuf.active) {
        if (i + 1 == ctx->cache->count || ctx->cache->lines[i + 1].byte_start != ctx->point)
            cursor_on_line = true;
    }

    float cursor_offset = 0.0f;
    if (cursor_on_line) {
        size_t head_len = ctx->point - line_start;
        if (head_len > 0) {
            cursor_offset += text_measure_tab_aware(&ctx->state->rendererData, ctx->font_id, ctx->font_size,
                                               ctx->chars + line_start, head_len,
                                               ctx->tab_width);
        }
    }

    Clay_Color c = ui_resolve_color(ctx->state, ctx->state->ui.roles.line_bg);
    // In nowrap mode the row is shifted left by scroll_x via the parent clip childOffset.
    // Use a fixed width of (text_col_w + scroll_x) so the background always fills the
    // full visible pane width regardless of how far the user has scrolled horizontally.
    float text_col_w = ctx->box.width - ctx->padding - ctx->gutter_width;
    CLAY(CLAY_IDI_LOCAL("TextRow", (int32_t)i), {
        .layout = {
            .sizing = { .width = ctx->wrap_lines ? CLAY_SIZING_GROW(0) : CLAY_SIZING_FIXED(text_col_w + ctx->scroll_x), .height = CLAY_SIZING_FIXED(ctx->line_height) },
            .layoutDirection = CLAY_LEFT_TO_RIGHT,
        },
        .backgroundColor = (cursor_on_line && ctx->buf->select_mode == SELECT_NONE) ? (Clay_Color){ c.r, c.g, c.b, 128 } : (Clay_Color){0}
    }) {
        if (cursor_on_line && ctx->cp->active && ctx->state->cursor_visible
                && ctx->state->input.current_focus != FOCUS_SEARCH
                && ctx->state->input.current_focus != FOCUS_REPLACE)
            BufRender_CursorCell(ctx, i, cursor_offset);
        BufRender_MatchRangeCell(ctx, i, vl);
        BufRender_SelectionCell(ctx, i, vl);
        BufRender_SearchCell(ctx, i, vl);
        BufRender_TextCell(ctx, vl);
    }
}

// Renders a horizontal scrollbar below the text area (nowrap mode only).
static void BufRender_HScrollbar(BufRenderCtx *ctx) {
    ContentPane *cp = ctx->cp;
    cp->has_hscrollbar = false;
    if (ctx->wrap_lines) return;
    float content_w = cp->text_origin_w;
    float max_w = ctx->cache->max_line_width;
    if (max_w <= content_w) return;

    cp->has_hscrollbar = true;
    float bar_h = 12.0f * ctx->state->ui.scale_factor;
    float scroll_x = ctx->scroll_x;
    float range    = max_w - content_w;

    float gutter_col_w = ctx->padding + ctx->gutter_width;
    float track_w = ctx->box.width - gutter_col_w;
    float thumb_w = fmaxf((content_w / max_w) * track_w,
                          20.0f * ctx->state->ui.scale_factor);
    float travel  = track_w - thumb_w;
    float thumb_x = (range > 0.0f && travel > 0.0f)
                    ? (scroll_x / range) * travel : 0.0f;

    cp->hscrollbar_y       = ctx->box.y + ctx->text_height;
    cp->hscrollbar_track_x = ctx->box.x + gutter_col_w;
    cp->hscrollbar_track_w = track_w;
    cp->hscrollbar_thumb_x = ctx->box.x + gutter_col_w + thumb_x;
    cp->hscrollbar_thumb_w = thumb_w;

    CLAY_AUTO_ID({
        .layout = {
            .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(bar_h) },
            .layoutDirection = CLAY_LEFT_TO_RIGHT,
        }
    }) {
        // Transparent spacer so the bar background doesn't cover the gutter.
        CLAY_AUTO_ID({
            .layout = { .sizing = {
                .width  = CLAY_SIZING_FIXED(gutter_col_w),
                .height = CLAY_SIZING_FIXED(bar_h) } }
        }) {}
        // Track (text column width only): background, border, and thumb.
        CLAY_AUTO_ID({
            .layout = {
                .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(bar_h) },
                .layoutDirection = CLAY_LEFT_TO_RIGHT,
            },
            .backgroundColor = ui_resolve_color(ctx->state, ctx->state->ui.roles.bar_bg),
            .border = {
                .width = { .left = 2, .right = 2 },
                .color = ui_resolve_color(ctx->state, ctx->state->ui.roles.border_inactive)
            }
        }) {
            // Left spacer before thumb.
            if (thumb_x > 0.0f) {
                CLAY_AUTO_ID({
                    .layout = { .sizing = {
                        .width  = CLAY_SIZING_FIXED(thumb_x),
                        .height = CLAY_SIZING_FIXED(bar_h) } }
                }) {}
            }
            // Thumb.
            CLAY_AUTO_ID({
                .layout = { .sizing = {
                    .width  = CLAY_SIZING_FIXED(thumb_w),
                    .height = CLAY_SIZING_FIXED(bar_h) } },
                .backgroundColor = Clay_Hovered() && !ctx->state->input.mouse_button_down
                    ? ui_resolve_color(ctx->state, ctx->state->ui.roles.scrollbar_hover)
                    : ui_resolve_color(ctx->state, ctx->state->ui.roles.scrollbar)
            }) {}
        }
    }
}

// Renders the scrollbar track and thumb alongside the buffer text area.
static void BufRender_Scrollbar(BufRenderCtx *ctx) {
    float bar_w     = 12.0f * ctx->state->ui.scale_factor;
    ContentPane *cp = ctx->cp;
    CLAY(ctx->track_id, {
        .layout = {
            .sizing = { .width = CLAY_SIZING_FIXED(bar_w), .height = CLAY_SIZING_GROW(0) },
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
        },
        .backgroundColor = ui_resolve_color(ctx->state, ctx->state->ui.roles.bar_bg)
    }) {
        cp->has_scrollbar = (cp->line_height_px > 0 &&
                             ctx->tab->content.buffer.vline_cache.count > 1);
        if (cp->has_scrollbar) {
            VLineCache *tc = &ctx->tab->content.buffer.vline_cache;
            Clay_ElementData track_bb = Clay_GetElementData(ctx->track_id);
            float th = (track_bb.found && track_bb.boundingBox.height > 0)
                ? track_bb.boundingBox.height : cp->text_origin_h;
            cp->scrollbar_track_h = th;
            float ms       = (float)(tc->count - 1) * cp->line_height_px;
            float total    = th + ms;
            float min_th   = fminf(20.0f * ctx->state->ui.scale_factor, th);
            float thumb_h  = fmaxf((th / total) * th, min_th);
            float spacer_h = (ms > 0)
                ? (tc->scroll_offset / ms) * (th - thumb_h)
                : 0.0f;
            cp->scrollbar_thumb_h = thumb_h;
            cp->scrollbar_thumb_y = cp->scrollbar_y + spacer_h;
            CLAY_AUTO_ID({
                .layout = { .sizing = {
                    .width  = CLAY_SIZING_GROW(0),
                    .height = CLAY_SIZING_FIXED(spacer_h) } }
            }) {}
            CLAY_AUTO_ID({
                .layout = { .sizing = {
                    .width  = CLAY_SIZING_GROW(0),
                    .height = CLAY_SIZING_FIXED(thumb_h) } },
                .backgroundColor = Clay_Hovered() && !ctx->state->input.mouse_button_down
                    ? ui_resolve_color(ctx->state, ctx->state->ui.roles.scrollbar_hover)
                    : ui_resolve_color(ctx->state, ctx->state->ui.roles.scrollbar)
            }) {}
        }
    }
}

// --- Public entry point ---

void BufferContentRender(AppState *state, ContentPane *cp, Tab *tab, int32_t index,
                         Clay_ElementId container_id) {
    if (!tab) return;
    Buffer *buf = tab->content.buffer.buffer;
    if (!buf) return;

    const char *chars = buffer_text_cached(buf);

    // Per-frame diff markers against the last-saved snapshot. NULL when the
    // buffer has never been saved or is currently unmodified.
    int diff_n = 0;
    uint8_t *diff_markers = buffer_get_line_markers(buf, &diff_n);

    // Pre-read horizontal scroll from cache before layout (same two-frame pattern as sub_offset).
    // This value is used for both the clip childOffset and cursor/selection offsets so they
    // are always in sync within the same frame.
    float scroll_x_pre = tab->content.buffer.vline_cache.wrap_lines
                         ? 0.0f
                         : tab->content.buffer.vline_cache.scroll_x;

    BufRenderCtx ctx = {
        .state     = state,
        .cp        = cp,
        .tab       = tab,
        .index     = index,
        .buf       = buf,
        .chars     = chars,
        .point     = point_get(buf).pos,
        .lt        = buffer_get_line_table(buf),
        .cache     = &tab->content.buffer.vline_cache,
        .font_id   = FONT_BUF_NORMAL,
        .font_size = (uint16_t)(14 * state->ui.scale_factor * buffer_get_scale(buf)),
        .padding   = 32.0f * state->ui.scale_factor,
        // scroll_x matches the clip childOffset so cursor/selection float correctly.
        .scroll_x  = scroll_x_pre,
        .diff_markers       = diff_markers,
        .diff_markers_count = diff_n,
    };

    // Compute bracket highlight positions for this frame.
    {
        size_t bk_match = buffer_find_matching_bracket(buf, ctx.point);
        ctx.bracket_hl_a = (bk_match != (size_t)-1) ? ctx.point  : (size_t)-1;
        ctx.bracket_hl_b = (bk_match != (size_t)-1) ? bk_match   : (size_t)-1;
    }

    Clay_ElementId outer_id = CLAY_IDI_LOCAL("BufWrap", index);
    Clay_ElementId track_id = CLAY_IDI_LOCAL("ScrollTrack", index);
    ctx.outer_id        = outer_id;
    ctx.track_id        = track_id;
    ctx.content_pane_id = container_id;

    float sub_offset = BufRender_ComputeSubOffset(&ctx);

    CLAY(outer_id, {
        .layout = { .sizing = tab_layout_expand, .layoutDirection = CLAY_LEFT_TO_RIGHT }
    }) {
        // Inner row: text area + vertical scrollbar.
        Clay_ElementId inner_row_id = CLAY_IDI_LOCAL("BufInner", index);
        CLAY(inner_row_id, {
            .layout = { .sizing = tab_layout_expand, .layoutDirection = CLAY_TOP_TO_BOTTOM }
        }) {
            Clay_ElementId id = CLAY_IDI_LOCAL("Buffer Text", index);
            CLAY(id, {
                .layout = {
                    .sizing          = tab_layout_expand,
                    .padding         = { .top = 2, .bottom = 2 },
                    .layoutDirection = CLAY_LEFT_TO_RIGHT,
                },
                .clip = { .vertical = true, .horizontal = true,
                          .childOffset = { .y = -sub_offset } }
            }) {
                if (BufRender_SetupGeometry(&ctx, id)) {
                    // Gutter column: fixed width, not scrolled horizontally.
                    float gutter_col_w = ctx.padding + ctx.gutter_width;
                    CLAY(CLAY_IDI_LOCAL("GutterCol", index), {
                        .layout = {
                            .sizing = { .width  = CLAY_SIZING_FIXED(gutter_col_w),
                                        .height = CLAY_SIZING_GROW(0) },
                            .layoutDirection = CLAY_TOP_TO_BOTTOM,
                        }
                    }) {
                        for (size_t i = ctx.first_vline; i < ctx.end_vline; i++)
                            BufRender_GutterCell(&ctx, i, &ctx.cache->lines[i]);
                    }
                    // Text column: scrolled horizontally.
                    CLAY(CLAY_IDI_LOCAL("TextCol", index), {
                        .layout = {
                            .sizing = { .width  = CLAY_SIZING_GROW(0),
                                        .height = CLAY_SIZING_GROW(0) },
                            .layoutDirection = CLAY_TOP_TO_BOTTOM,
                        },
                        .clip = { .horizontal = true,
                                  .childOffset = { .x = -scroll_x_pre } }
                    }) {
                        for (size_t i = ctx.first_vline; i < ctx.end_vline; i++)
                            BufRender_TextRow(&ctx, i);
                    }
                } else {
                    BufRender_LoadingPlaceholder(&ctx);
                }
            }
            // Horizontal scrollbar (nowrap mode only, shown when content overflows).
            BufRender_HScrollbar(&ctx);
        }
        BufRender_Scrollbar(&ctx);
    }
}
