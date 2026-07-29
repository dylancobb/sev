#include <stdlib.h>
#include <string.h>

#include <chibi/eval.h>

#include "message.h"
#include "minibuf.h"
#include "../state_io.h"
#include "mode.h"
#include "scheme_internal.h"
#include "../text/buffer.h"
#include "../display/major_mode_info.h"
#include "../file_scanner.h"

// ---- Shared provider utility ------------------------------------------------

// Clamp selected and item_scroll after item_count changes.
static void clamp_selected(AppState *state) {
    if (state->minibuf.item_count == 0)
        state->minibuf.selected = 0;
    else if (state->minibuf.selected >= state->minibuf.item_count)
        state->minibuf.selected = state->minibuf.item_count - 1;
    int max_scroll = state->minibuf.item_count - MINIBUF_VISIBLE_ITEMS;
    if (max_scroll < 0) max_scroll = 0;
    if (state->minibuf.item_scroll > max_scroll) state->minibuf.item_scroll = max_scroll;
    if (state->minibuf.item_scroll < 0) state->minibuf.item_scroll = 0;
}

// Scroll the visible window just enough to keep selected in view.
static void scroll_to_visible(AppState *state) {
    int sel = state->minibuf.selected;
    if (sel < state->minibuf.item_scroll)
        state->minibuf.item_scroll = sel;
    else if (sel >= state->minibuf.item_scroll + MINIBUF_VISIBLE_ITEMS)
        state->minibuf.item_scroll = sel - MINIBUF_VISIBLE_ITEMS + 1;
}

// ---- Preview ----------------------------------------------------------------

// Preview sym_name, unless it is already what's previewed. Hover, arrow keys,
// typing and submit all funnel through here: previews are expensive (a theme
// preview rebuilds the whole palette) and mouse motion fires far more often
// than the item under the cursor actually changes.
static void preview_sym(AppState *state, const char *sym_name) {
    if (!state->minibuf.preview_action) return;
    if (strcmp(sym_name, state->minibuf.previewed_sym) == 0) return;
    strncpy(state->minibuf.previewed_sym, sym_name, MINIBUF_LABEL_MAX - 1);
    state->minibuf.previewed_sym[MINIBUF_LABEL_MAX - 1] = '\0';
    sexp ctx = state->chibi.ctx;
    state->minibuf.preview_action(ctx, sexp_intern(ctx, sym_name, -1));
}

// Preview the keyboard selection: the item up/down and typing move between,
// which hover deliberately leaves alone.
static void preview_selected(AppState *state) {
    if (state->minibuf.item_count == 0) return;
    int sel = state->minibuf.selected;
    if (sel < 0 || sel >= state->minibuf.item_count) return;
    preview_sym(state, state->minibuf.items[sel].sym_name);
}

// ---- Shared sort comparator -------------------------------------------------

static int item_label_cmp(const void *a, const void *b) {
    return strcmp(((const MinibufItem *)a)->label,
                  ((const MinibufItem *)b)->label);
}

// Filter all_items[] into items[] based on input, then clamp selection.
static void filter_items(AppState *state, const char *input) {
    bool filter = input && input[0] != '\0';
    state->minibuf.item_count = 0;
    for (int i = 0; i < state->minibuf.all_item_count; i++) {
        if (state->minibuf.item_count >= MINIBUF_ITEMS_MAX) break;
        if (filter && strstr(state->minibuf.all_items[i].label, input) == NULL) continue;
        state->minibuf.items[state->minibuf.item_count++] = state->minibuf.all_items[i];
    }
    clamp_selected(state);
}

// ---- Command provider -------------------------------------------------------

static void commands_provider(AppState *state, const char *input) {
    if (state->minibuf.all_item_count == 0) {
        sexp ctx = state->chibi.ctx;
        sexp fn = sexp_env_ref(ctx, state->chibi.env,
                               sexp_intern(ctx, "list-commands", -1), SEXP_FALSE);
        if (fn == SEXP_FALSE) return;
        sexp list = sexp_apply(ctx, fn, SEXP_NULL);
        if (sexp_exceptionp(list)) return;

        sexp summary_fn = sexp_env_ref(ctx, state->chibi.env,
                                       sexp_intern(ctx, "doc-summary", -1), SEXP_FALSE);
        sexp first_binding_fn = sexp_env_ref(ctx, state->chibi.env,
                                             sexp_intern(ctx, "command-display-binding", -1),
                                             SEXP_FALSE);

        for (sexp p = list; sexp_pairp(p); p = sexp_cdr(p)) {
            if (state->minibuf.all_item_count >= MINIBUF_ITEMS_MAX) break;
            sexp sym = sexp_car(p);
            if (!sexp_symbolp(sym)) continue;
            sexp str = sexp_symbol_to_string(ctx, sym);
            const char *name = sexp_string_data(str);
            const char *label = name;
            sexp summary = (summary_fn != SEXP_FALSE)
                ? sexp_apply(ctx, summary_fn, sexp_list1(ctx, sym)) : SEXP_FALSE;
            if (sexp_stringp(summary)) label = sexp_string_data(summary);
            MinibufItem *item = &state->minibuf.all_items[state->minibuf.all_item_count];
            memset(item, 0, sizeof(*item));
            strncpy(item->label,    label, MINIBUF_LABEL_MAX - 1);
            strncpy(item->sym_name, name,  MINIBUF_LABEL_MAX - 1);
            if (first_binding_fn != SEXP_FALSE) {
                sexp kb = sexp_apply(ctx, first_binding_fn, sexp_list1(ctx, sym));
                if (sexp_stringp(kb))
                    strncpy(item->keybinding, sexp_string_data(kb), sizeof(item->keybinding) - 1);
            }
            state->minibuf.all_item_count++;
        }
        qsort(state->minibuf.all_items, state->minibuf.all_item_count,
              sizeof(MinibufItem), item_label_cmp);
    }
    filter_items(state, input);
}

// ---- Function provider ------------------------------------------------------

static void functions_provider(AppState *state, const char *input) {
    if (state->minibuf.all_item_count == 0) {
        sexp ctx = state->chibi.ctx;
        sexp fn = sexp_env_ref(ctx, state->chibi.env,
                               sexp_intern(ctx, "list-functions", -1), SEXP_FALSE);
        if (fn == SEXP_FALSE) return;
        sexp list = sexp_apply(ctx, fn, SEXP_NULL);
        if (sexp_exceptionp(list)) return;

        for (sexp p = list; sexp_pairp(p); p = sexp_cdr(p)) {
            if (state->minibuf.all_item_count >= MINIBUF_ITEMS_MAX) break;
            sexp sym = sexp_car(p);
            if (!sexp_symbolp(sym)) continue;
            sexp str = sexp_symbol_to_string(ctx, sym);
            const char *name = sexp_string_data(str);
            MinibufItem *item = &state->minibuf.all_items[state->minibuf.all_item_count];
            memset(item, 0, sizeof(*item));
            strncpy(item->label,    name, MINIBUF_LABEL_MAX - 1);
            strncpy(item->sym_name, name, MINIBUF_LABEL_MAX - 1);
            state->minibuf.all_item_count++;
        }
        qsort(state->minibuf.all_items, state->minibuf.all_item_count,
              sizeof(MinibufItem), item_label_cmp);
    }
    filter_items(state, input);
}

// ---- Variable provider ------------------------------------------------------

static void variables_provider(AppState *state, const char *input) {
    if (state->minibuf.all_item_count == 0) {
        sexp ctx = state->chibi.ctx;
        sexp fn = sexp_env_ref(ctx, state->chibi.env,
                               sexp_intern(ctx, "list-variables", -1), SEXP_FALSE);
        if (fn == SEXP_FALSE) return;
        sexp list = sexp_apply(ctx, fn, SEXP_NULL);
        if (sexp_exceptionp(list)) return;

        for (sexp p = list; sexp_pairp(p); p = sexp_cdr(p)) {
            if (state->minibuf.all_item_count >= MINIBUF_ITEMS_MAX) break;
            sexp sym = sexp_car(p);
            if (!sexp_symbolp(sym)) continue;
            sexp str = sexp_symbol_to_string(ctx, sym);
            const char *name = sexp_string_data(str);
            MinibufItem *item = &state->minibuf.all_items[state->minibuf.all_item_count];
            memset(item, 0, sizeof(*item));
            strncpy(item->label,    name, MINIBUF_LABEL_MAX - 1);
            strncpy(item->sym_name, name, MINIBUF_LABEL_MAX - 1);
            state->minibuf.all_item_count++;
        }
        qsort(state->minibuf.all_items, state->minibuf.all_item_count,
              sizeof(MinibufItem), item_label_cmp);
    }
    filter_items(state, input);
}

// ---- Theme provider ---------------------------------------------------------

static void themes_provider(AppState *state, const char *input) {
    if (state->minibuf.all_item_count == 0) {
        sexp ctx = state->chibi.ctx;
        sexp fn = sexp_env_ref(ctx, state->chibi.env,
                               sexp_intern(ctx, "list-themes", -1), SEXP_FALSE);
        if (fn == SEXP_FALSE) return;
        sexp list = sexp_apply(ctx, fn, SEXP_NULL);
        if (sexp_exceptionp(list)) return;

        for (sexp p = list; sexp_pairp(p); p = sexp_cdr(p)) {
            if (state->minibuf.all_item_count >= MINIBUF_ITEMS_MAX) break;
            sexp pair = sexp_car(p);
            if (!sexp_pairp(pair)) continue;
            sexp sym  = sexp_car(pair);
            sexp dstr = sexp_cdr(pair);
            if (!sexp_symbolp(sym) || !sexp_stringp(dstr)) continue;
            MinibufItem *item = &state->minibuf.all_items[state->minibuf.all_item_count];
            memset(item, 0, sizeof(*item));
            strncpy(item->label, sexp_string_data(dstr), MINIBUF_LABEL_MAX - 1);
            sexp sym_str = sexp_symbol_to_string(ctx, sym);
            strncpy(item->sym_name, sexp_string_data(sym_str), MINIBUF_LABEL_MAX - 1);
            state->minibuf.all_item_count++;
        }
        qsort(state->minibuf.all_items, state->minibuf.all_item_count,
              sizeof(MinibufItem), item_label_cmp);
    }
    filter_items(state, input);
}

static void theme_apply(sexp ctx, sexp sym) {
    sexp activate = sexp_env_ref(ctx, G->chibi.env,
                                  sexp_intern(ctx, "activate-theme", -1),
                                  SEXP_FALSE);
    if (activate == SEXP_FALSE) return;
    sexp result = sexp_apply(ctx, activate, sexp_list1(ctx, sym));
    if (sexp_exceptionp(result))
        sexp_print_exception(ctx, result, sexp_current_error_port(ctx));
}

static void describe_symbol_action(sexp ctx, sexp sym) {
    sexp fn = sexp_env_ref(ctx, G->chibi.env,
                           sexp_intern(ctx, "describe-symbol", -1), SEXP_FALSE);
    if (fn == SEXP_FALSE) return;
    sexp result = sexp_apply(ctx, fn, sexp_list1(ctx, sym));
    if (sexp_exceptionp(result))
        sexp_print_exception(ctx, result, sexp_current_error_port(ctx));
}

// ---- Major mode provider ----------------------------------------------------

static void major_modes_provider(AppState *state, const char *input) {
    if (state->minibuf.all_item_count == 0) {
        sexp ctx = state->chibi.ctx;
        sexp fn = sexp_env_ref(ctx, state->chibi.env,
                               sexp_intern(ctx, "list-writable-major-modes", -1), SEXP_FALSE);
        if (fn == SEXP_FALSE) return;

        // GC-protect list: sexp_symbol_to_string in the loop allocates and can
        // trigger GC, which would collect the unprotected list sitting on the C stack.
        sexp_gc_var1(list);
        sexp_gc_preserve1(ctx, list);
        list = sexp_apply(ctx, fn, SEXP_NULL);

        if (!sexp_exceptionp(list)) {
            for (sexp p = list; sexp_pairp(p); p = sexp_cdr(p)) {
                if (state->minibuf.all_item_count >= MINIBUF_ITEMS_MAX) break;
                sexp pair = sexp_car(p);
                if (!sexp_pairp(pair)) continue;
                sexp sym  = sexp_car(pair);
                sexp dstr = sexp_cdr(pair);
                if (!sexp_symbolp(sym) || !sexp_stringp(dstr)) continue;
                MinibufItem *item = &state->minibuf.all_items[state->minibuf.all_item_count];
                memset(item, 0, sizeof(*item));
                strncpy(item->label, sexp_string_data(dstr), MINIBUF_LABEL_MAX - 1);
                sexp sym_str = sexp_symbol_to_string(ctx, sym);
                strncpy(item->sym_name, sexp_string_data(sym_str), MINIBUF_LABEL_MAX - 1);
                MajorModeInfoEntry *minfo = major_mode_info_get(item->sym_name);
                if (minfo && minfo->icon_name[0])
                    strncpy(item->icon_name, minfo->icon_name, sizeof(item->icon_name) - 1);
                state->minibuf.all_item_count++;
            }
            qsort(state->minibuf.all_items, state->minibuf.all_item_count,
                  sizeof(MinibufItem), item_label_cmp);
        }

        sexp_gc_release1(ctx);
    }
    filter_items(state, input);
}

static void major_mode_apply(sexp ctx, sexp sym) {
    // Prefer calling the mode's own command procedure (e.g. scheme-mode),
    // which runs mode-specific setup (TS enable/disable, etc.) before
    // switching the mode. Fall back to set-major-mode! for modes that
    // don't define a dedicated command.
    sexp mode_fn = sexp_env_ref(ctx, G->chibi.env, sym, SEXP_FALSE);
    if (sexp_procedurep(mode_fn)) {
        sexp result = sexp_apply(ctx, mode_fn, SEXP_NULL);
        if (sexp_exceptionp(result))
            sexp_print_exception(ctx, result, sexp_current_error_port(ctx));
        return;
    }
    sexp fn = sexp_env_ref(ctx, G->chibi.env,
                           sexp_intern(ctx, "set-major-mode!", -1), SEXP_FALSE);
    if (fn == SEXP_FALSE) return;
    sexp result = sexp_apply(ctx, fn, sexp_list1(ctx, sym));
    if (sexp_exceptionp(result))
        sexp_print_exception(ctx, result, sexp_current_error_port(ctx));
}

sexp scm_minibuffer_activate_major_modes(sexp ctx, sexp self, sexp n) {
    G->minibuf.provider       = major_modes_provider;
    G->minibuf.preview_action = NULL;
    G->minibuf.submit_action  = major_mode_apply;
    G->minibuf.all_item_count = 0;
    G->minibuf.item_count     = 0;
    G->minibuf.selected       = 0;
    G->minibuf.item_scroll    = 0;

    sexp prompt = sexp_c_string(ctx, "Select a language...", -1);
    sexp ret = scm_minibuffer_activate(ctx, self, n, prompt, SEXP_FALSE, SEXP_FALSE);
    major_modes_provider(G, "");
    return ret;
}

static void theme_confirm(sexp ctx, sexp sym) {
    sexp fn = sexp_env_ref(ctx, G->chibi.env,
                            sexp_intern(ctx, "theme-display-name", -1), SEXP_FALSE);
    sexp dname = (fn != SEXP_FALSE) ? sexp_apply(ctx, fn, sexp_list1(ctx, sym)) : SEXP_FALSE;
    const char *display_name = sexp_stringp(dname)
        ? sexp_string_data(dname)
        : sexp_string_data(sexp_symbol_to_string(ctx, sym));
    char buf[MINIBUF_LABEL_MAX + 32];
    snprintf(buf, sizeof(buf), "Theme changed to %s", display_name);
    message_send(buf);
}

// Call a named command via the cached call-interactively.
// Used to push/pop vim mode on minibuffer focus transitions.
static void minibuf_invoke_command(sexp ctx, const char *name) {
    if (G->chibi.call_interactively == SEXP_FALSE) return;
    sexp sym = sexp_intern(ctx, name, -1);
    sexp result = sexp_apply(ctx, G->chibi.call_interactively,
                             sexp_list1(ctx, sym));
    if (sexp_exceptionp(result))
        sexp_print_exception(ctx, result, sexp_current_error_port(ctx));
}

bool minibuf_init(AppState *state) {
    state->minibuf.on_submit      = SEXP_FALSE;
    state->minibuf.on_cancel      = SEXP_FALSE;
    state->minibuf.active         = false;
    state->minibuf.stack_depth    = 0;
    state->minibuf.provider       = NULL;
    state->minibuf.preview_action = NULL;
    state->minibuf.submit_action  = NULL;
    state->minibuf.saved_sym      = SEXP_FALSE;
    state->minibuf.all_item_count = 0;
    state->minibuf.item_scroll    = 0;
    state->minibuf.previewed_sym[0] = '\0';

    Buffer *buf = buffer_create("*minibuffer*");
    if (!buf) return false;
    state->minibuf.buf = buf;

    // Enable minibuffer-mode (provides the isolated keymap + allows_input)
    Mode *mode = mode_lookup("minibuffer-mode", MODE_MINOR);
    if (mode)
        buffer_enable_minor_mode(buf, mode);

    // buffer_create() auto-enables vim-normal-mode on every new buffer;
    // remove it so the minibuf is fully isolated from vim keybindings.
    Mode *vim = mode_lookup("vim-normal-mode", MODE_MINOR);
    if (vim)
        buffer_disable_minor_mode(buf, vim);

    return true;
}

// Restore the top stack frame into the live minibuf state.
// Caller must have already saved/zeroed on_submit/on_cancel before calling.
// Requires minibuf.buf to be the current buffer (safe during submit/cancel).
static void minibuf_pop(sexp ctx) {
    G->minibuf.stack_depth--;
    MinibufFrame *frame = &G->minibuf.stack[G->minibuf.stack_depth];

    strncpy(G->minibuf.prompt, frame->prompt, MINIBUF_PROMPT_MAX);

    // Restore callbacks (still GC-preserved from when they were pushed)
    G->minibuf.on_submit = frame->on_submit;
    G->minibuf.on_cancel = frame->on_cancel;

    // Restore buffer content
    buffer_clear(G->minibuf.buf);
    if (frame->saved_text) {
        insert_string(G->minibuf.buf, frame->saved_text);
        free(frame->saved_text);
        frame->saved_text = NULL;
    }

    // Restore point (minibuf.buf must be the current buffer)
    Location loc = { frame->saved_point };
    point_set(loc);

    G->minibuf.text_scroll = 0.0f;
}

sexp scm_minibuffer_activate(sexp ctx, sexp self, sexp n,
                              sexp sprompt, sexp on_submit, sexp on_cancel) {
    if (!sexp_stringp(sprompt))
        return sexp_user_exception(ctx, self, "prompt must be a string", sprompt);

    if (G->minibuf.active) {
        // Push current state onto stack
        if (G->minibuf.stack_depth >= MINIBUF_STACK_MAX)
            return sexp_user_exception(ctx, self, "minibuffer stack overflow", SEXP_FALSE);
        MinibufFrame *frame = &G->minibuf.stack[G->minibuf.stack_depth];
        strncpy(frame->prompt, G->minibuf.prompt, MINIBUF_PROMPT_MAX);
        frame->on_submit   = G->minibuf.on_submit;   // already preserved
        frame->on_cancel   = G->minibuf.on_cancel;   // already preserved (or SEXP_FALSE)
        frame->saved_text  = buffer_text(G->minibuf.buf); // may be NULL if empty
        frame->saved_point = point_get(G->minibuf.buf).pos;
        G->minibuf.stack_depth++;
        buffer_clear(G->minibuf.buf);
        G->minibuf.text_scroll = 0.0f;
    } else {
        // Release any stale callbacks
        if (G->minibuf.on_submit != SEXP_FALSE)
            sexp_release_object(ctx, G->minibuf.on_submit);
        if (G->minibuf.on_cancel != SEXP_FALSE)
            sexp_release_object(ctx, G->minibuf.on_cancel);
        Buffer *cur = buffer_get_current();
        G->minibuf.prev_buf   = (cur != G->minibuf.buf) ? cur : NULL;
        G->minibuf.prev_focus = G->input.current_focus;
        G->input.current_focus = FOCUS_MINIBUFFER;
        buffer_clear(G->minibuf.buf);
        G->minibuf.text_scroll = 0.0f;
    }

    const char *prompt = sexp_string_data(sprompt);
    strncpy(G->minibuf.prompt, prompt, MINIBUF_PROMPT_MAX - 1);
    G->minibuf.prompt[MINIBUF_PROMPT_MAX - 1] = '\0';

    G->minibuf.on_submit = on_submit;
    G->minibuf.on_cancel = sexp_booleanp(on_cancel) ? SEXP_FALSE : on_cancel;
    if (G->minibuf.on_submit != SEXP_FALSE)
        sexp_preserve_object(ctx, G->minibuf.on_submit);
    if (G->minibuf.on_cancel != SEXP_FALSE)
        sexp_preserve_object(ctx, G->minibuf.on_cancel);

    G->minibuf.active = true;

    return SEXP_VOID;
}

sexp scm_minibuffer_submit(sexp ctx, sexp self, sexp n) {
    if (!G->minibuf.active)
        return SEXP_VOID;

    // Provider branch: ignore raw text, execute selected item.
    if (G->minibuf.provider && G->minibuf.item_count > 0) {
        int sel = G->minibuf.selected;
        if (sel < 0) sel = 0;
        if (sel >= G->minibuf.item_count) sel = G->minibuf.item_count - 1;

        // A click selects and submits in one go, so the item may never have
        // been previewed; make sure what we accept is what got applied.
        preview_sym(G, G->minibuf.items[sel].sym_name);

        sexp sym = sexp_intern(ctx, G->minibuf.items[sel].sym_name, -1);

        void (*action)(sexp, sexp) = G->minibuf.submit_action;
        G->minibuf.provider       = NULL;
        G->minibuf.preview_action = NULL;
        G->minibuf.submit_action  = NULL;
        G->minibuf.item_count     = 0;
        G->minibuf.selected       = 0;
        G->minibuf.item_scroll    = 0;
        G->minibuf.previewed_sym[0] = '\0';
        if (G->minibuf.saved_sym != SEXP_FALSE) {
            sexp_release_object(ctx, G->minibuf.saved_sym);
            G->minibuf.saved_sym = SEXP_FALSE;
        }

        // Release any stale callbacks before dismissing.
        sexp cb_submit = G->minibuf.on_submit;
        sexp cb_cancel = G->minibuf.on_cancel;
        G->minibuf.on_submit = SEXP_FALSE;
        G->minibuf.on_cancel = SEXP_FALSE;

        if (G->minibuf.stack_depth > 0) {
            minibuf_pop(ctx);
        } else {
            G->minibuf.active = false;
            G->input.current_focus = G->minibuf.prev_focus;
            buffer_set_current(G->minibuf.prev_buf);
            if (G->minibuf.prev_buf)
                minibuf_invoke_command(ctx, "vim-normal");
        }

        if (cb_submit != SEXP_FALSE) sexp_release_object(ctx, cb_submit);
        if (cb_cancel != SEXP_FALSE) sexp_release_object(ctx, cb_cancel);

        if (action) {
            action(ctx, sym);
        } else if (G->chibi.call_interactively != SEXP_FALSE) {
            sexp sym_str = sexp_symbol_to_string(ctx, sym);
            state_io_record_command(G, sexp_string_data(sym_str));
            sexp result = sexp_apply(ctx, G->chibi.call_interactively,
                                     sexp_list1(ctx, sym));
            if (sexp_exceptionp(result))
                sexp_print_exception(ctx, result, sexp_current_error_port(ctx));
        }
        return SEXP_VOID;
    }

    char *raw = buffer_text(G->minibuf.buf);
    const char *text = raw ? raw : "";

    sexp cb_submit = G->minibuf.on_submit;
    sexp cb_cancel = G->minibuf.on_cancel;
    G->minibuf.on_submit = SEXP_FALSE;
    G->minibuf.on_cancel = SEXP_FALSE;

    if (G->minibuf.stack_depth > 0) {
        minibuf_pop(ctx);
        // active remains true; previous frame is restored
    } else {
        G->minibuf.active = false;
        G->input.current_focus = G->minibuf.prev_focus;
        buffer_set_current(G->minibuf.prev_buf);
        // Full dismiss: return the calling buffer to normal-mode.
        if (G->minibuf.prev_buf)
            minibuf_invoke_command(ctx, "vim-normal");
    }

    if (cb_submit != SEXP_FALSE) {
        sexp_gc_var1(str);
        sexp_gc_preserve1(ctx, str);
        str = sexp_c_string(ctx, text, -1);
        free(raw);
        raw = NULL;
        sexp result = sexp_apply(ctx, cb_submit, sexp_list1(ctx, str));
        if (sexp_exceptionp(result))
            sexp_print_exception(ctx, result, sexp_current_error_port(ctx));
        sexp_gc_release1(ctx);
        sexp_release_object(ctx, cb_submit);
    }

    if (cb_cancel != SEXP_FALSE)
        sexp_release_object(ctx, cb_cancel);

    free(raw);
    return SEXP_VOID;
}

sexp scm_minibuffer_cancel(sexp ctx, sexp self, sexp n) {
    if (!G->minibuf.active)
        return SEXP_VOID;

    sexp cb_cancel = G->minibuf.on_cancel;
    sexp cb_submit = G->minibuf.on_submit;
    G->minibuf.on_cancel = SEXP_FALSE;
    G->minibuf.on_submit = SEXP_FALSE;

    if (G->minibuf.stack_depth > 0) {
        minibuf_pop(ctx);
        // active remains true; previous frame is restored
    } else {
        // Restore previewed state before dismissing (e.g. theme picker reverts theme).
        if (G->minibuf.preview_action && G->minibuf.saved_sym != SEXP_FALSE)
            G->minibuf.preview_action(ctx, G->minibuf.saved_sym);

        G->minibuf.active         = false;
        G->minibuf.provider       = NULL;
        G->minibuf.preview_action = NULL;
        G->minibuf.submit_action  = NULL;
        G->minibuf.item_count     = 0;
        G->minibuf.selected       = 0;
        G->minibuf.item_scroll    = 0;
        G->minibuf.previewed_sym[0] = '\0';
        if (G->minibuf.saved_sym != SEXP_FALSE) {
            sexp_release_object(ctx, G->minibuf.saved_sym);
            G->minibuf.saved_sym = SEXP_FALSE;
        }
        G->input.current_focus = G->minibuf.prev_focus;
        buffer_set_current(G->minibuf.prev_buf);
        // Full dismiss: return the calling buffer to normal-mode.
        if (G->minibuf.prev_buf)
            minibuf_invoke_command(ctx, "vim-normal");
    }

    if (cb_cancel != SEXP_FALSE) {
        sexp result = sexp_apply(ctx, cb_cancel, SEXP_NULL);
        if (sexp_exceptionp(result))
            sexp_print_exception(ctx, result, sexp_current_error_port(ctx));
        sexp_release_object(ctx, cb_cancel);
    }

    if (cb_submit != SEXP_FALSE)
        sexp_release_object(ctx, cb_submit);

    return SEXP_VOID;
}

sexp scm_minibuffer_activep(sexp ctx, sexp self, sexp n) {
    return sexp_make_boolean(G->minibuf.active);
}

sexp scm_minibuffer_activate_describe_function(sexp ctx, sexp self, sexp n) {
    G->minibuf.provider       = functions_provider;
    G->minibuf.submit_action  = describe_symbol_action;
    G->minibuf.all_item_count = 0;
    G->minibuf.item_count     = 0;
    G->minibuf.selected       = 0;
    G->minibuf.item_scroll    = 0;
    sexp prompt = sexp_c_string(ctx, "Describe function...", -1);
    return scm_minibuffer_activate(ctx, self, n, prompt, SEXP_FALSE, SEXP_FALSE);
}

sexp scm_minibuffer_activate_describe_command(sexp ctx, sexp self, sexp n) {
    G->minibuf.provider       = commands_provider;
    G->minibuf.submit_action  = describe_symbol_action;
    G->minibuf.all_item_count = 0;
    G->minibuf.item_count     = 0;
    G->minibuf.selected       = 0;
    G->minibuf.item_scroll    = 0;
    sexp prompt = sexp_c_string(ctx, "Describe command...", -1);
    return scm_minibuffer_activate(ctx, self, n, prompt, SEXP_FALSE, SEXP_FALSE);
}

sexp scm_minibuffer_activate_describe_variable(sexp ctx, sexp self, sexp n) {
    G->minibuf.provider       = variables_provider;
    G->minibuf.submit_action  = describe_symbol_action;
    G->minibuf.all_item_count = 0;
    G->minibuf.item_count     = 0;
    G->minibuf.selected       = 0;
    G->minibuf.item_scroll    = 0;
    sexp prompt = sexp_c_string(ctx, "Describe variable...", -1);
    return scm_minibuffer_activate(ctx, self, n, prompt, SEXP_FALSE, SEXP_FALSE);
}

sexp scm_minibuffer_activate_commands(sexp ctx, sexp self, sexp n) {
    G->minibuf.provider       = commands_provider;
    G->minibuf.submit_action  = NULL; // use call-interactively
    G->minibuf.all_item_count = 0;
    G->minibuf.item_count     = 0;
    G->minibuf.selected       = 0;
    G->minibuf.item_scroll    = 0;
    sexp prompt = sexp_c_string(ctx, "Execute command...", -1);
    return scm_minibuffer_activate(ctx, self, n, prompt, SEXP_FALSE, SEXP_FALSE);
}

// ---- File picker provider ---------------------------------------------------

static const char *file_picker_icon_for_ext(const char *basename) {
    const char *ext = strrchr(basename, '.');
    if (!ext) return "icon-unknown-extension";
    ext++;
    if (strcmp(ext, "scm") == 0 || strcmp(ext, "sld") == 0) return "scheme-icon";
    if (strcmp(ext, "json") == 0) return "json-icon";
    if (strcmp(ext, "md")   == 0) return "plaintext-icon";
    if (strcmp(ext, "txt")  == 0) return "plaintext-icon";
    return "icon-unknown-extension";
}

static void file_picker_provider(AppState *state, const char *input) {
    bool filter = input && input[0] != '\0';
    state->minibuf.item_count = 0;
    FileIndex *idx = &state->scanner.index;
    for (size_t i = 0; i < idx->count && state->minibuf.item_count < MINIBUF_ITEMS_MAX; i++) {
        FileEntry *e = &idx->entries[i];
        if (filter && strstr(e->path, input) == NULL) continue;
        MinibufItem *item = &state->minibuf.items[state->minibuf.item_count++];
        memset(item, 0, sizeof(*item));
        strncpy(item->sym_name, e->path, MINIBUF_LABEL_MAX - 1);
        strncpy(item->label, e->basename, MINIBUF_LABEL_MAX - 1);
        size_t dir_len = (size_t)(e->basename - e->path);
        if (dir_len > 0)
            snprintf(item->suffix, MINIBUF_LABEL_MAX, "%.*s", (int)dir_len, e->path);
        strncpy(item->icon_name,
                file_picker_icon_for_ext(e->basename),
                sizeof(item->icon_name) - 1);
    }
    clamp_selected(state);
}

static void file_picker_submit_action(sexp ctx, sexp sym) {
    sexp str = sexp_symbol_to_string(ctx, sym);
    sexp fn = sexp_env_ref(ctx, G->chibi.env,
                           sexp_intern(ctx, "file-picker-open!", -1), SEXP_FALSE);
    if (fn == SEXP_FALSE) return;
    sexp result = sexp_apply(ctx, fn, sexp_list1(ctx, str));
    if (sexp_exceptionp(result))
        sexp_print_exception(ctx, result, sexp_current_error_port(ctx));
}

sexp scm_minibuffer_activate_file_picker(sexp ctx, sexp self, sexp n) {
    G->minibuf.provider       = file_picker_provider;
    G->minibuf.preview_action = NULL;
    G->minibuf.submit_action  = file_picker_submit_action;
    G->minibuf.all_item_count = 0;
    G->minibuf.item_count     = 0;
    G->minibuf.selected       = 0;
    G->minibuf.item_scroll    = 0;
    sexp prompt = sexp_c_string(ctx, "Find file...", -1);
    return scm_minibuffer_activate(ctx, self, n, prompt, SEXP_FALSE, SEXP_FALSE);
}

sexp scm_minibuffer_activate_themes(sexp ctx, sexp self, sexp n) {
    // Save current theme so cancel can restore it.
    // Call the exported (current-theme) accessor; *current-theme* itself is not exported.
    sexp fn = sexp_env_ref(ctx, G->chibi.env,
                            sexp_intern(ctx, "current-theme", -1), SEXP_FALSE);
    sexp current = (fn != SEXP_FALSE) ? sexp_apply(ctx, fn, SEXP_NULL) : SEXP_FALSE;
    G->minibuf.saved_sym = sexp_symbolp(current) ? current : SEXP_FALSE;
    if (G->minibuf.saved_sym != SEXP_FALSE)
        sexp_preserve_object(ctx, G->minibuf.saved_sym);

    G->minibuf.provider       = themes_provider;
    G->minibuf.preview_action = theme_apply;
    G->minibuf.submit_action  = theme_confirm;
    G->minibuf.all_item_count = 0;
    G->minibuf.item_count     = 0;
    G->minibuf.selected       = 0;
    G->minibuf.item_scroll    = 0;
    G->minibuf.previewed_sym[0] = '\0';
    sexp prompt = sexp_c_string(ctx, "Select a theme...", -1);
    sexp ret = scm_minibuffer_activate(ctx, self, n, prompt, SEXP_FALSE, SEXP_FALSE);

    // Populate items now so the initial selection is immediately previewed.
    themes_provider(G, "");
    preview_selected(G);

    return ret;
}

// Re-filter the provider list against the current minibuffer text and preview
// whatever ends up selected. Called after a key edits the minibuffer, so that
// typing previews the same way arrow keys do. The filter runs again during
// layout; doing it here too is what makes items[] current before previewing.
void minibuf_refresh_after_edit(AppState *state) {
    if (!state->minibuf.active || !state->minibuf.provider) return;

    char *raw = buffer_text(state->minibuf.buf);
    state->minibuf.provider(state, raw ? raw : "");
    free(raw);

    preview_selected(state);
}

// ---- Mouse hit-testing ------------------------------------------------------

bool minibuf_point_in_palette(AppState *state, float x, float y) {
    return state->minibuf.active
        && x >= state->minibuf.palette_x
        && x <  state->minibuf.palette_x + state->minibuf.palette_w
        && y >= state->minibuf.palette_y
        && y <  state->minibuf.palette_y + state->minibuf.palette_h;
}

int minibuf_item_at(AppState *state, float x, float y) {
    if (!minibuf_point_in_palette(state, x, y)) return -1;
    if (!state->minibuf.provider || state->minibuf.item_count == 0) return -1;
    if (state->minibuf.palette_item_h <= 0.0f) return -1;
    if (y < state->minibuf.palette_items_y) return -1;

    int row = (int)((y - state->minibuf.palette_items_y) / state->minibuf.palette_item_h);
    int visible = state->minibuf.item_count < MINIBUF_VISIBLE_ITEMS
                ? state->minibuf.item_count : MINIBUF_VISIBLE_ITEMS;
    if (row < 0 || row >= visible) return -1;
    return state->minibuf.item_scroll + row;
}

// Preview whatever the mouse points at; anywhere that isn't an item row (the
// input line, the padding, outside the palette) falls back to the keyboard
// selection.
void minibuf_hover_update(AppState *state, float x, float y) {
    if (!state->minibuf.active || !state->minibuf.preview_action) return;
    int idx = minibuf_item_at(state, x, y);
    if (idx >= 0)
        preview_sym(state, state->minibuf.items[idx].sym_name);
    else
        preview_selected(state);
}

sexp scm_minibuffer_select_next(sexp ctx, sexp self, sexp n) {
    if (!G->minibuf.active || G->minibuf.item_count == 0) return SEXP_VOID;
    if (G->minibuf.selected < G->minibuf.item_count - 1) {
        G->minibuf.selected++;
        scroll_to_visible(G);
        preview_selected(G);
    }
    return SEXP_VOID;
}

sexp scm_minibuffer_select_prev(sexp ctx, sexp self, sexp n) {
    if (!G->minibuf.active || G->minibuf.item_count == 0) return SEXP_VOID;
    if (G->minibuf.selected > 0) {
        G->minibuf.selected--;
        scroll_to_visible(G);
        preview_selected(G);
    }
    return SEXP_VOID;
}
