#include <math.h>
#include <unistd.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_mouse.h>
#include <chibi/eval.h>

#include "state.h"
#ifdef __APPLE__
#include "platform/macos.h"
#endif
#ifndef __EMSCRIPTEN__
#include "platform/dpi.h"
#endif
#include "cursor_flash.h"
#include "command/keyboard.h"
#include "command/message.h"
#include "command/minibuf.h"
#include "display/pane.h"
#include "display/scale.h"
#include "display/tooltip.h"
#include "display/vline.h"
#include "file_scanner.h"
#include "state_io.h"
#include "text/buffer.h"

#define CURSOR_FLASH_EVENT 1

static Uint32 cursor_flash_cb(void *ud, SDL_TimerID id, Uint32 interval) {
    (void)id; (void)interval;
    AppState *state = ud;
    SDL_Event ev = {0};
    ev.type = SDL_EVENT_USER;
    ev.user.code = CURSOR_FLASH_EVENT;
    ev.user.data1 = (void *)(uintptr_t)state->cursor_flash_gen;
    SDL_PushEvent(&ev);
    return 0; // one-shot
}

static bool cursor_blink_enabled(AppState *state) {
    sexp sym = sexp_intern(state->chibi.ctx, "cursor-blink", -1);
    sexp val = sexp_env_ref(state->chibi.ctx, state->chibi.env, sym, SEXP_TRUE);
    return val != SEXP_FALSE;
}

void cursor_flash_reset(AppState *state) {
    SDL_RemoveTimer(state->cursor_flash_timer);
    state->cursor_flash_timer = 0;
    state->cursor_flash_gen++;
    state->cursor_visible = true;
    if (cursor_blink_enabled(state))
        state->cursor_flash_timer = SDL_AddTimer(500, cursor_flash_cb, state);
}

// Invoke (mouse-click-cb button buf-pos clicks) on the Scheme side.
static void invoke_mouse_click_cb(AppState *state, int button,
                                  size_t pos, int clicks) {
    sexp cb = state->input.mouse_click_cb;
    if (cb == SEXP_FALSE) return;

    sexp ctx = state->chibi.ctx;
    sexp_gc_var1(args);
    sexp_gc_preserve1(ctx, args);
    args = sexp_list3(ctx,
        sexp_make_fixnum(button),
        sexp_make_fixnum((sexp_sint_t)pos),
        sexp_make_fixnum(clicks));
    sexp result = sexp_apply(ctx, cb, args);
    if (sexp_exceptionp(result))
        sexp_print_exception(ctx, result, sexp_current_error_port(ctx));
    sexp_gc_release1(ctx);
}

// Invoke (mouse-drag-cb current-pos start-pos) on the Scheme side.
static void invoke_mouse_drag_cb(AppState *state, size_t pos, size_t start_pos) {
    sexp cb = state->input.mouse_drag_cb;
    if (cb == SEXP_FALSE) return;

    sexp ctx = state->chibi.ctx;
    sexp_gc_var1(args);
    sexp_gc_preserve1(ctx, args);
    args = sexp_list2(ctx,
        sexp_make_fixnum((sexp_sint_t)pos),
        sexp_make_fixnum((sexp_sint_t)start_pos));
    sexp result = sexp_apply(ctx, cb, args);
    if (sexp_exceptionp(result))
        sexp_print_exception(ctx, result, sexp_current_error_port(ctx));
    sexp_gc_release1(ctx);
}


// Hit-test the screen point (x, y) against the pane tree, then compute the
// buffer byte position.  Returns true and fills *pos_out on success.
static bool pane_hit_to_buf_pos(AppState *state, Pane *pane,
                                 float x, float y, size_t *pos_out) {
    if (!pane || pane->type != PANE_CONTENT || !pane->content.active_tab) return false;
    Buffer *buf = pane->content.active_tab->content.buffer.buffer;
    if (!buf) return false;

    float rel_x = x - pane->content.text_origin_x;
    float rel_y = y - pane->content.text_origin_y;
    // Clamp rel_y so dragging slightly outside still lands on nearest line.
    if (rel_y < 0.0f) rel_y = 0.0f;
    if (rel_y > pane->content.text_origin_h)
        rel_y = pane->content.text_origin_h;

    char *chars = buffer_text(buf);
    *pos_out = vline_byte_pos_at_xy(
        &pane->content.active_tab->content.buffer.vline_cache, chars,
        rel_x, rel_y,
        pane->content.line_height_px,
        &state->rendererData,
        pane->content.render_font_id,
        pane->content.render_font_size);
    free(chars);
    return true;
}

/* This function runs when a new event (mouse input, keypresses, etc) occurs. */
SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    AppState *state = (AppState*) appstate;

    // Convert mouse/touch coordinates from window space to render (pixel) space
    SDL_ConvertEventToRenderCoordinates(state->rendererData.renderer, event);

    switch (event->type) {
    case SDL_EVENT_USER:
        if (event->user.code == CURSOR_FLASH_EVENT) {
            uint32_t gen = (uint32_t)(uintptr_t)event->user.data1;
            if (gen == state->cursor_flash_gen) {
                if (cursor_blink_enabled(state)) {
                    state->cursor_visible = !state->cursor_visible;
                    state->cursor_flash_gen++;
                    state->cursor_flash_timer = SDL_AddTimer(500, cursor_flash_cb, state);
                } else {
                    state->cursor_flash_timer = 0;
                    state->cursor_visible = true;
                }
            }
        } else if (event->user.code == TOOLTIP_SHOW_EVENT) {
            tooltip_handle_show(state);
        } else if (event->user.code == FOLDER_DIALOG_EVENT) {
            if (state->pending_folder_dialog[0]) {
                if (chdir(state->pending_folder_dialog) == 0) {
                    state_io_update_recent_project(state, state->pending_folder_dialog);
                    scanner_restart(&state->scanner);
                    char msg[PATH_MAX + 32];
                    snprintf(msg, sizeof(msg), "Opened project %s", state->pending_folder_dialog);
                    message_echo(msg);
                } else {
                    char msg[PATH_MAX + 48];
                    snprintf(msg, sizeof(msg), "Cannot open project: %s", state->pending_folder_dialog);
                    message_echo(msg);
                }
                state->pending_folder_dialog[0] = '\0';
            }
        }
        break;

    case SDL_EVENT_TEXT_INPUT:
        handle_text_input(state, &event->text);
        break;

    case SDL_EVENT_KEY_DOWN:
        handle_key_down(state, &event->key);
        break;

    case SDL_EVENT_MOUSE_MOTION: {
        float x = event->motion.x;
        float y = event->motion.y;
        state->input.mouse_x = x;
        state->input.mouse_y = y;
        bool button_held = (event->motion.state & SDL_BUTTON_LMASK) != 0;
        Clay_SetPointerState((Clay_Vector2){x, y}, button_held);

        // The palette owns the mouse while it is open, same as for clicks.
        if (state->minibuf.active) {
            minibuf_hover_update(state, x, y);
            break;
        }

        // Split divider drag.
        if (state->input.split_drag_pane && state->input.mouse_button_down) {
            pane_split_drag_update(state->input.split_drag_pane, x, y);
            break;
        }

        if (state->input.scrollbar_drag_pane) {
            Pane *sp = state->input.scrollbar_drag_pane;
            VLineCache *sc = &sp->content.active_tab->content.buffer.vline_cache;
            int slh = sp->content.line_height_px;
            float track_top = sp->content.scrollbar_y;
            float thumb_h   = sp->content.scrollbar_thumb_h;
            float travel    = sp->content.scrollbar_track_h - thumb_h;
            float max_sc    = (sc->count > 1) ? (float)(sc->count - 1) * slh : 0.0f;
            if (travel > 0 && max_sc > 0) {
                float frac = (y - state->input.scrollbar_drag_offset - track_top) / travel;
                if (frac < 0) frac = 0;
                if (frac > 1) frac = 1;
                sc->scroll_offset = frac * max_sc;
                sc->target_scroll = sc->scroll_offset;
            }
            break;
        }

        if (state->input.hscrollbar_drag_pane) {
            Pane *sp = state->input.hscrollbar_drag_pane;
            VLineCache *sc = &sp->content.active_tab->content.buffer.vline_cache;
            float thumb_w  = sp->content.hscrollbar_thumb_w;
            float track_w  = sp->content.hscrollbar_track_w;
            float track_x  = sp->content.hscrollbar_track_x;
            float travel   = track_w - thumb_w;
            float content_w = sp->content.text_origin_w;
            float max_sc   = sc->max_line_width > content_w
                             ? sc->max_line_width - content_w : 0.0f;
            if (travel > 0 && max_sc > 0) {
                float frac = (x - state->input.hscrollbar_drag_offset - track_x) / travel;
                if (frac < 0) frac = 0;
                if (frac > 1) frac = 1;
                sc->scroll_x = frac * max_sc;
                sc->target_scroll_x = sc->scroll_x;
            }
            break;
        }

        if (state->input.mouse_button_down && state->input.mouse_down_pane) {
            float dx = x - state->input.mouse_down_x;
            float dy = y - state->input.mouse_down_y;
            if (!state->input.mouse_drag_active && (dx*dx + dy*dy) > 9.0f)
                state->input.mouse_drag_active = true;

            if (state->input.mouse_drag_active) {
                Pane *p = state->input.mouse_down_pane;
                size_t pos = 0;
                if (pane_hit_to_buf_pos(state, p, x, y, &pos))
                    invoke_mouse_drag_cb(state, pos, state->input.mouse_down_buf_pos);
            }
        }
        break;
    }

    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        cursor_flash_reset(state);
        float x = event->button.x;
        float y = event->button.y;

        // Handle minibuf click first, before setting any Clay or input state,
        // to prevent click-through to elements rendered under the backdrop.
        if (state->minibuf.active) {
            if (!minibuf_point_in_palette(state, x, y)) {
                scm_minibuffer_cancel(state->chibi.ctx, SEXP_FALSE, SEXP_FALSE);
            } else {
                int i = minibuf_item_at(state, x, y);
                if (i >= 0) {
                    state->minibuf.selected = i;
                    scm_minibuffer_submit(state->chibi.ctx, SEXP_FALSE, SEXP_FALSE);
                }
            }
            break; // consume the click regardless
        }

        Clay_SetPointerState((Clay_Vector2){x, y}, true);

        state->input.mouse_button_down = true;
        state->input.mouse_down_x = x;
        state->input.mouse_down_y = y;
        state->input.mouse_drag_active = false;
        if (event->button.button == SDL_BUTTON_MIDDLE)
            state->input.middle_pressed_this_frame = true;
        if (event->button.button == SDL_BUTTON_LEFT && event->button.clicks == 2)
            state->input.left_double_click_this_frame = true;

        // Check split divider before scrollbar/buffer.
        Pane *split_hit = pane_split_at_coords(pane_get_root(), x, y);
        if (split_hit) {
            if (event->button.clicks == 2) {
                if (split_hit->type == PANE_V_SPLIT) split_hit->v_split.left_width = 0.5f;
                else                                 split_hit->h_split.top_height  = 0.5f;
            } else {
                state->input.split_drag_pane    = split_hit;
                state->input.split_drag_start_x = x;
                state->input.split_drag_start_y = y;
            }
            break;
        }

        Pane *hit = pane_at_coords(pane_get_root(), x, y);
        state->input.mouse_down_pane = hit;

        // Check vertical scrollbar hit before buffer hit.
        if (hit && hit->type == PANE_CONTENT && hit->content.active_tab
            && hit->content.has_scrollbar
            && x >= hit->content.scrollbar_x
            && y >= hit->content.scrollbar_y
            && y <= hit->content.scrollbar_y + hit->content.scrollbar_track_h) {

            float thumb_y = hit->content.scrollbar_thumb_y;
            float thumb_h = hit->content.scrollbar_thumb_h;
            bool  on_thumb = (y >= thumb_y && y < thumb_y + thumb_h);
            state->input.scrollbar_drag_offset = on_thumb
                ? (y - thumb_y)
                : (thumb_h / 2.0f);

            VLineCache *sc = &hit->content.active_tab->content.buffer.vline_cache;
            int slh = hit->content.line_height_px;
            float travel = hit->content.scrollbar_track_h - thumb_h;
            float max_sc  = (sc->count > 1) ? (float)(sc->count - 1) * slh : 0.0f;
            if (travel > 0 && max_sc > 0) {
                float frac = (y - state->input.scrollbar_drag_offset - hit->content.scrollbar_y) / travel;
                if (frac < 0) frac = 0;
                if (frac > 1) frac = 1;
                sc->scroll_offset = frac * max_sc;
                sc->target_scroll = sc->scroll_offset;
            }

            state->input.scrollbar_drag_pane = hit;
            break;
        }

        // Check horizontal scrollbar hit.
        if (hit && hit->type == PANE_CONTENT && hit->content.active_tab
            && hit->content.has_hscrollbar
            && y >= hit->content.hscrollbar_y
            && x >= hit->content.hscrollbar_track_x
            && x <= hit->content.hscrollbar_track_x + hit->content.hscrollbar_track_w) {

            float thumb_x = hit->content.hscrollbar_thumb_x;
            float thumb_w = hit->content.hscrollbar_thumb_w;
            bool  on_thumb = (x >= thumb_x && x < thumb_x + thumb_w);
            state->input.hscrollbar_drag_offset = on_thumb
                ? (x - thumb_x)
                : (thumb_w / 2.0f);

            VLineCache *sc = &hit->content.active_tab->content.buffer.vline_cache;
            float track_w   = hit->content.hscrollbar_track_w;
            float content_w = hit->content.text_origin_w;
            float travel    = track_w - thumb_w;
            float max_sc    = sc->max_line_width > content_w
                              ? sc->max_line_width - content_w : 0.0f;
            if (travel > 0 && max_sc > 0) {
                float frac = (x - state->input.hscrollbar_drag_offset
                              - hit->content.hscrollbar_track_x) / travel;
                if (frac < 0) frac = 0;
                if (frac > 1) frac = 1;
                sc->scroll_x = frac * max_sc;
                sc->target_scroll_x = sc->scroll_x;
            }

            state->input.hscrollbar_drag_pane = hit;
            break;
        }

        if (hit && hit->type == PANE_CONTENT && hit->content.active_tab) {
            // Switch active pane so that Scheme point-set! targets the right buffer.
            if (!hit->content.active)
                pane_set_active(hit);

            size_t pos = 0;
            if (pane_hit_to_buf_pos(state, hit, x, y, &pos)) {
                state->input.mouse_down_buf_pos = pos;
                invoke_mouse_click_cb(state, event->button.button, pos,
                                      event->button.clicks);
            }
        }
        break;
    }

    case SDL_EVENT_MOUSE_BUTTON_UP:
        Clay_SetPointerState((Clay_Vector2){event->button.x, event->button.y}, false);
        state->input.mouse_button_down    = false;
        state->input.mouse_drag_active    = false;
        state->input.mouse_down_pane      = NULL;
        state->input.scrollbar_drag_pane  = NULL;
        state->input.hscrollbar_drag_pane = NULL;
        state->input.split_drag_pane      = NULL;
        break;

    case SDL_EVENT_MOUSE_WHEEL:
        cursor_flash_reset(state);
        float deltaTime = ((float) SDL_GetTicksNS() - state->last_frame_ns) / 1e9;
        Clay_UpdateScrollContainers(true, (Clay_Vector2) { event->wheel.x, event->wheel.y }, deltaTime);

        // Scroll the minibuffer completion list when wheel is over the palette.
        if (state->minibuf.active && state->minibuf.item_count > MINIBUF_VISIBLE_ITEMS) {
            float mx = event->wheel.mouse_x, my = event->wheel.mouse_y;
            if (minibuf_point_in_palette(state, mx, my)) {
                int delta = -(int)event->wheel.y;
                int max_scroll = state->minibuf.item_count - MINIBUF_VISIBLE_ITEMS;
                state->minibuf.item_scroll += delta;
                if (state->minibuf.item_scroll < 0) state->minibuf.item_scroll = 0;
                if (state->minibuf.item_scroll > max_scroll) state->minibuf.item_scroll = max_scroll;
                // Scrolling slides a different item under a stationary cursor.
                minibuf_hover_update(state, mx, my);
                break;
            }
        }

        // Scroll the buffer pane under the mouse cursor.
        Pane *scroll_hit = pane_at_coords(pane_get_root(),
                                          event->wheel.mouse_x, event->wheel.mouse_y);
        if (scroll_hit && scroll_hit->type == PANE_CONTENT && scroll_hit->content.active_tab) {
            VLineCache *cache = &scroll_hit->content.active_tab->content.buffer.vline_cache;
            float px_per_line = (float)scroll_hit->content.line_height_px;
            if (px_per_line <= 0) px_per_line = 20.0f;

            // Vertical scroll.
            if (cache->count > 0 && event->wheel.y != 0) {
                float delta_px = -event->wheel.y * px_per_line * 3.0f;

                cache->scroll_offset += delta_px;
                float max_scroll = (cache->count > 1)
                    ? (float)(cache->count - 1) * px_per_line : 0.0f;
                if (cache->scroll_offset < 0.0f) cache->scroll_offset = 0.0f;
                if (cache->scroll_offset > max_scroll) cache->scroll_offset = max_scroll;
                cache->target_scroll = cache->scroll_offset;
            }

            // Horizontal scroll (nowrap mode only).
            if (!cache->wrap_lines && event->wheel.x != 0) {
                float dx = event->wheel.x * px_per_line * 3.0f;
                float text_w = scroll_hit->content.text_origin_w;
                float max_scroll_x = cache->max_line_width > text_w
                                     ? cache->max_line_width - text_w : 0.0f;
                cache->scroll_x = fmaxf(0.0f, fminf(cache->scroll_x + dx, max_scroll_x));
                cache->target_scroll_x = cache->scroll_x;
            }
        }
        break;

#ifdef __APPLE__
    case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
        macos_reapply_titlebar(state->window);
        break;
#endif

    case SDL_EVENT_WINDOW_RESIZED: {
        int width, height;
        SDL_GetWindowSizeInPixels(state->window, &width, &height);
        Clay_SetLayoutDimensions((Clay_Dimensions) {(float) width, (float) height});
        break;
    }

    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED: {
        float os_scale = SDL_GetWindowDisplayScale(state->window);
        if (os_scale <= 0.0f) os_scale = 1.0f;
#ifndef __EMSCRIPTEN__
        if (os_scale <= 1.0f) {
            float ppi = get_display_ppi(state->window);
            os_scale = (ppi > 0.0f) ? fmaxf(ppi / 96.0f, 1.0f) : 1.0f;
        }
#endif
        state->ui.dpi_scale = os_scale;
        ui_recompute_scale(state);
        int width, height;
        SDL_GetWindowSizeInPixels(state->window, &width, &height);
        Clay_SetLayoutDimensions((Clay_Dimensions) {(float) width, (float) height});
        break;
    }

    case SDL_EVENT_QUIT:
        return SDL_APP_SUCCESS;
    }

    return SDL_APP_CONTINUE;
}
