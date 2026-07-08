#include <SDL3/SDL_render.h>
#include <chibi/sexp.h>
#include <stdio.h>
#include <string.h>

#include "decoration.h"
#include "icon.h"
#include "major_mode_info.h"
#include "message.h"
#include "mode_icon.h"
#include "theme.h"
#include "tooltip.h"
#include "../command/keymap.h"
#include "../command/scheme_internal.h"
#include "../state.h"
#include "../text/buffer.h"
#include "../text/buffer_type.h"
#include "../text/line.h"

#define BAR_STRINGS_MAX 256
static char *bar_strings[BAR_STRINGS_MAX];
static int bar_strings_count = 0;

void bar_free_strings(void) {
    for (int i = 0; i < bar_strings_count; i++) {
        free(bar_strings[i]);
    }
    bar_strings_count = 0;
}

void bar_strings_push(char *p) {
    if (bar_strings_count < BAR_STRINGS_MAX) {
        bar_strings[bar_strings_count++] = p;
    }
}

void ModePill(AppState *state, Buffer *buf) {
    CachedRoles roles = state->ui.roles;

    bool is_welcome = (state->input.current_focus == FOCUS_WELCOME);

    const char* modeStr = "Normal";
    if (buf && !is_welcome) {
        sexp mode_symbol = sexp_intern(state->chibi.ctx, "mode-name", -1);
        sexp mode_val = vartable_get(buffer_get_locals(buf), mode_symbol, SEXP_FALSE);
        modeStr = sexp_to_cstring(state->chibi.ctx, mode_val, "ERROR");
    }

    Clay_String modeName = {
        .chars = modeStr,
        .length = strlen(modeStr),
        .isStaticallyAllocated = true
    };

    ModeIconEntry *icon = (buf && !is_welcome) ? mode_icon_for_current_buffer() : mode_icon_get("vim-normal-mode");
    Clay_Color mode_bg = icon
        ? ui_resolve_color(state, icon->role_mode_bg)
        : ui_resolve_color(state, roles.mode_normal);
    Clay_Color label_color = icon
        ? ui_resolve_color(state, icon->role_label)
        : (Clay_Color){255, 0, 255, 255};

    CLAY(CLAY_ID("Mode Name"), {
        .layout = {
            .sizing = {
                .height = CLAY_SIZING_FIXED(16.0 * state->ui.scale_factor)
            },
            .padding = {
                .left = 8.0 * state->ui.scale_factor,
                .right = 14.0 * state->ui.scale_factor
            },
            .childGap = 3.0 * state->ui.scale_factor,
            .childAlignment = {
                .y = CLAY_ALIGN_Y_CENTER
            },
        },
        .cornerRadius = {
            .topRight = 8.0 * state->ui.scale_factor,
            .bottomRight = 8.0 * state->ui.scale_factor
         },
        .backgroundColor = mode_bg
    }){
        if (icon) {
            SDL_Texture *tex = icon_get(icon->icon_name, state, 14, 14);
            CLAY(CLAY_ID("Mode Icon"), {
                .layout = {
                    .sizing = {
                        .width = 14.0 * state->ui.scale_factor,
                        .height = 14.0 * state->ui.scale_factor
                    },
                },
                .image = tex
            }) {}
        }
        CLAY_TEXT(modeName, CLAY_TEXT_CONFIG({
            .fontId = FONT_UI_BOLD,
            .fontSize = 12.0 * state->ui.scale_factor,
            .textColor = label_color,
        }));
    }
}

void MacroIndicator(AppState *state) {
    CachedRoles roles = state->ui.roles;
    float scale = state->ui.scale_factor;
    bool recording = state->macro_recording;
    if (recording) {
        float radius = 3.0 * state->ui.scale_factor;
        CLAY(CLAY_ID("Macro indicator"), {
            .layout = {
                .sizing = { .height = CLAY_SIZING_GROW(0) },
                .padding = { .left = 10.0 * scale, .right = 10.0 * scale },
                .childAlignment = {
                    .y = CLAY_ALIGN_Y_CENTER
                },
                .childGap = 4.0 * state->ui.scale_factor
            },
        }){
            CLAY_AUTO_ID({
                .layout = {
                    .sizing = { .height = CLAY_SIZING_GROW(0) },
                    .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
                    .padding = { .top = (radius / 2) },
                }
            }) {
                CLAY_AUTO_ID({
                    .layout = {
                        .sizing = {
                            .height = CLAY_SIZING_FIXED(2.0 * radius),
                            .width = CLAY_SIZING_FIXED(2.0 * radius)
                        }
                    },
                    .cornerRadius = CLAY_CORNER_RADIUS(radius),
                    .backgroundColor = ui_resolve_color(state, roles.macro_indicator)
                }) {}
            }
            CLAY_TEXT(CLAY_STRING("REC"), CLAY_TEXT_CONFIG({
                .fontId = FONT_UI_BOLD,
                .fontSize = 10.0 * state->ui.scale_factor,
                .textColor = ui_resolve_color(state, roles.text_primary),
            }));
        }
    }
}

static void HandleMajorModeClick(Clay_ElementId elementId,
                                 Clay_PointerData pointerInfo,
                                 void *userData) {
    (void)elementId; (void)userData;
    if (pointerInfo.state != CLAY_POINTER_DATA_PRESSED_THIS_FRAME) return;
    sexp ctx = G->chibi.ctx;
    sexp sym = sexp_intern(ctx, "set-buffer-mode", -1);
    sexp result = sexp_apply(ctx, G->chibi.call_interactively, sexp_list1(ctx, sym));
    if (sexp_exceptionp(result))
        sexp_print_exception(ctx, result, sexp_current_error_port(ctx));
}

void MajorModeIndicator(AppState *state, Buffer *buf, Clay_Color text_color) {
    if (state->input.current_focus != FOCUS_PANE &&
        state->input.current_focus != FOCUS_SEARCH &&
        state->input.current_focus != FOCUS_REPLACE &&
        !(state->input.current_focus == FOCUS_MINIBUFFER &&
          state->minibuf.prev_focus == FOCUS_PANE)) return;
    float scale = state->ui.scale_factor;
    Mode *major = buffer_get_major_mode(buf);
    if (!major) return;

    MajorModeInfoEntry *info = major_mode_info_get(major->name);
    if (!info) return;

    Clay_String display_name = {
        .chars = info->display_name,
        .length = strlen(info->display_name),
        .isStaticallyAllocated = true
    };
    Clay_Color hovered_bg = ui_resolve_color(state, state->ui.roles.message_hover);
    CLAY(CLAY_ID("Major Mode"), {
        .layout = {
            .sizing = {
                .height = CLAY_SIZING_FIXED(16 * scale),
            },
            .padding = { .left = 10.0 * scale, .right = 10.0 * scale },
            .childGap = 3.0 * scale,
            .childAlignment = {
                .y = CLAY_ALIGN_Y_CENTER
            },
        },
        .backgroundColor = Clay_Hovered() ? hovered_bg : (Clay_Color){0}
    }) {
        Clay_OnHover(HandleMajorModeClick, NULL);
        bool hovered = Clay_Hovered();
        char binding[64] = {0};
        keymap_where_is_first(state, "set-buffer-mode", binding, sizeof(binding));
        TextTooltipWithBinding(state, hovered, 0xBADE, "Select Language", binding);
        if (hovered) state->input.desired_cursor = SDL_SYSTEM_CURSOR_POINTER;
        if (info->icon_name[0]) {
            SDL_Texture *tex = icon_get(info->icon_name, state, 12, 12);
            CLAY(CLAY_ID("Major Mode Icon"), {
                .layout = {
                    .sizing = {
                        .width = 12.0 * scale,
                        .height = 12.0 * scale
                    },
                },
                .image = tex
            }) {}
        }
        CLAY_TEXT(display_name, CLAY_TEXT_CONFIG({
            .fontId = FONT_UI_NORMAL,
            .fontSize = 10.0 * scale,
            .textColor = text_color,
        }));
    }
    BarDivider(state);
}

void SelectionIndicator(AppState *state, Buffer *buf, Clay_Color text_color) {
    if (state->input.current_focus != FOCUS_PANE &&
        state->input.current_focus != FOCUS_SEARCH &&
        state->input.current_focus != FOCUS_REPLACE &&
        !(state->input.current_focus == FOCUS_MINIBUFFER &&
          state->minibuf.prev_focus == FOCUS_PANE)) return;
    if (buf->select_mode != SELECT_NONE) {
        float scale = state->ui.scale_factor;

        size_t sel_a    = buf->select_start.pos;
        size_t sel_b    = buf->point.pos;
        size_t sel_min  = sel_a < sel_b ? sel_a : sel_b;
        size_t sel_max  = sel_a > sel_b ? sel_a : sel_b;
        size_t line_a   = line_index_at(&buf->lt, sel_a);
        size_t line_b   = line_index_at(&buf->lt, sel_b);
        size_t row_min  = line_a < line_b ? line_a : line_b;
        size_t row_max  = line_a > line_b ? line_a : line_b;
        size_t num_lines = row_max - row_min + 1;

        char *sel_str = malloc(64 * sizeof(char));
        if (buf->select_mode == SELECT_LINE) {
            snprintf(sel_str, 64, "(%zu %s)",
                num_lines, num_lines == 1 ? "line" : "lines");
        } else if (buf->select_mode == SELECT_RECTANGLE ||
                   buf->select_mode == SELECT_RECTANGLE_RAGGED) {
            size_t col_a    = sel_a - buf->lt.lines[line_a].start;
            size_t col_b    = sel_b - buf->lt.lines[line_b].start;
            size_t col_min  = col_a < col_b ? col_a : col_b;
            size_t col_max  = col_a > col_b ? col_a : col_b;
            size_t num_cols = col_max - col_min + 1;
            snprintf(sel_str, 64, "(%zu %s, %zu %s)",
                num_lines, num_lines == 1 ? "line" : "lines",
                num_cols,  num_cols  == 1 ? "column" : "columns");
        } else {
            size_t num_chars = sel_max - sel_min + 1;
            if (num_lines > 1) {
                snprintf(sel_str, 64, "(%zu %s, %zu %s)",
                    num_lines, num_lines == 1 ? "line" : "lines",
                    num_chars, num_chars == 1 ? "character" : "characters");
            } else {
                snprintf(sel_str, 64, "(%zu %s)",
                    num_chars, num_chars == 1 ? "character" : "characters");
            }
        }
        bar_strings_push(sel_str);
        Clay_String selCount = { .chars = sel_str, .length = strlen(sel_str) };
        CLAY(CLAY_ID("Selection Indicator"), {
            .layout = {
                .sizing = {
                    .height = CLAY_SIZING_FIXED(16 * scale),
                },
                .padding = { .left = 10.0 * scale, .right = 10.0 * scale },
                .childAlignment = {
                    .y = CLAY_ALIGN_Y_CENTER
                },
            },
        }) {
            CLAY_TEXT(selCount, CLAY_TEXT_CONFIG({
                .fontId = FONT_UI_NORMAL,
                .fontSize = 10.0 * state->ui.scale_factor,
                .textColor = text_color,
            }));
        }
        BarDivider(state);
    }
}

void CursorPosition(AppState* state, Buffer *buf, Clay_Color text_color) {
    if (state->input.current_focus != FOCUS_PANE &&
        state->input.current_focus != FOCUS_SEARCH &&
        state->input.current_focus != FOCUS_REPLACE &&
        !(state->input.current_focus == FOCUS_MINIBUFFER &&
          state->minibuf.prev_focus == FOCUS_PANE)) return;
    float scale = state->ui.scale_factor;
    char *pos = malloc(32 * sizeof(char));
    snprintf(pos, 32, "%zu:%d", buf_get_line(buf), get_column(buf));
    bar_strings_push(pos);
    Clay_String cursor_position = {
         .chars = pos,
         .length = strlen(pos),
    };
    CLAY(CLAY_ID("Cursor Position"), {
        .layout = {
            .sizing = {
                .height = CLAY_SIZING_FIXED(16 * scale),
            },
            .padding = { .left = 10.0 * scale, .right = 10.0 * scale },
            .childAlignment = {
                .y = CLAY_ALIGN_Y_CENTER
            },
        },
    }) {
        CLAY_TEXT(cursor_position, CLAY_TEXT_CONFIG({
            .fontId = FONT_UI_NORMAL,
            .fontSize = 10.0 * state->ui.scale_factor,
            .textColor = text_color,
        }));
    }
}

void StatusBar(AppState *state) {
    Buffer *buf = buffer_get_current();
    if (!buf && state->input.current_focus != FOCUS_WELCOME) return;

    CachedRoles roles = state->ui.roles;

    float scale = state->ui.scale_factor;
    Clay_Color text_color = ui_resolve_color(state, roles.text_primary);

    CLAY(CLAY_ID("Status Bar"), {
        .layout = {
            .sizing = {
                .width = CLAY_SIZING_GROW(0),
            },
            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }
        },
        .border = {
            .width = {
                .top = 2,
            },
            .color = ui_resolve_color(state, roles.border_inactive)
        },
        .backgroundColor = ui_resolve_color(state, roles.bar_bg),
        .clip = {
            .horizontal = true
        }
    }) {
        // Left hand side of status bar
        ModePill(state, buf);
        MacroIndicator(state);

        XSpacer();

        // Right hand side of status bar
        MessageArea(state);
        MajorModeIndicator(state, buf, text_color);
        SelectionIndicator(state, buf, text_color);
        CursorPosition(state, buf, text_color);
    }
}
