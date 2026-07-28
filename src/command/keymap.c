// Keymap binding function implementations.

#include <assert.h>

#include <chibi/eval.h>

#include "keymap.h"
#include "message.h"
#include "minibuf.h"
#include "mode.h"
#include "scheme_internal.h"
#include "../text/buffer.h"
#include "../display/pane.h"
#include "../display/search.h"

KeyEvent last_event;

Keymap *keymap_create(void) {
    Keymap *km = calloc(1, sizeof(Keymap));
    if (km) km->name = NULL;
    return km;
}

bool init_input(AppState *state) {
    Keymap *global = keymap_create();
    if (!global) return false;
    Keymap *pane = keymap_create();
    if (!pane) { free(global); return false; }

    Keymap *search = keymap_create();
    if (!search) { free(global); free(pane); return false; }

    Keymap *replace = keymap_create();
    if (!replace) { free(global); free(pane); free(search); return false; }

    state->input.global_map  = global;
    state->input.pane_map    = pane;
    state->input.search_map  = search;
    state->input.replace_map = replace;
    state->input.current_map = global;
    state->input.key_intercept_cb  = SEXP_FALSE;
    state->input.key_intercept_map = NULL;
    state->input.key_intercept_str[0] = '\0';
    state->input.key_unbound_cb    = SEXP_FALSE;
    state->input.mouse_click_cb    = SEXP_FALSE;
    state->input.mouse_drag_cb     = SEXP_FALSE;
    state->input.mouse_button_down = false;
    state->input.mouse_down_pane   = NULL;
    state->input.mouse_drag_active = false;
    state->input.current_focus     = FOCUS_WELCOME;
    state->input.welcome_map        = NULL;

    state->which_key.enabled = true;
    state->which_key.prefix_str[0] = '\0';

    return true;
}

static void key_event_append_str(char *buf, size_t sz, const KeyEvent *ev) {
    size_t len = strlen(buf);
    if (len > 0 && len + 1 < sz) {
        buf[len++] = ' ';
        buf[len]   = '\0';
    }

    char tmp[64];
    size_t ti = 0;

    if (ev->mods & MOD_CTRL)  { memcpy(tmp + ti, "ctrl-",  5); ti += 5; }
    if (ev->mods & MOD_META)  { memcpy(tmp + ti, "alt-",   4); ti += 4; }
    if (ev->mods & MOD_SHIFT) { memcpy(tmp + ti, "shift-", 6); ti += 6; }

    if (ev->type == KEYEVENT_SPECIAL) {
        const char *name;
        switch (ev->keycode) {
        case KEY_ESC:        name = "escape";    break;
        case KEY_RETURN:     name = "return";    break;
        case KEY_TAB:        name = "tab";       break;
        case KEY_BACKSPACE:  name = "backspace"; break;
        case KEY_DELETE:     name = "delete";    break;
        case KEY_UP:         name = "up";        break;
        case KEY_DOWN:       name = "down";      break;
        case KEY_LEFT:       name = "left";      break;
        case KEY_RIGHT:      name = "right";     break;
        case KEY_HOME:       name = "home";      break;
        case KEY_END:        name = "end";       break;
        case KEY_PAGE_UP:    name = "pageup";    break;
        case KEY_PAGE_DOWN:  name = "pagedown";  break;
        case KEY_INSERT:     name = "insert";    break;
        case KEY_MENU:       name = "menu";      break;
        case KEY_PRINT:      name = "print";     break;
        case KEY_PAUSE:      name = "pause";     break;
        case KEY_F1:  name = "f1";  break;
        case KEY_F2:  name = "f2";  break;
        case KEY_F3:  name = "f3";  break;
        case KEY_F4:  name = "f4";  break;
        case KEY_F5:  name = "f5";  break;
        case KEY_F6:  name = "f6";  break;
        case KEY_F7:  name = "f7";  break;
        case KEY_F8:  name = "f8";  break;
        case KEY_F9:  name = "f9";  break;
        case KEY_F10: name = "f10"; break;
        case KEY_F11: name = "f11"; break;
        case KEY_F12: name = "f12"; break;
        case KEY_F13: name = "f13"; break;
        case KEY_F14: name = "f14"; break;
        case KEY_F15: name = "f15"; break;
        case KEY_F16: name = "f16"; break;
        case KEY_F17: name = "f17"; break;
        case KEY_F18: name = "f18"; break;
        case KEY_F19: name = "f19"; break;
        case KEY_F20: name = "f20"; break;
        case KEY_F21: name = "f21"; break;
        case KEY_F22: name = "f22"; break;
        case KEY_F23: name = "f23"; break;
        case KEY_F24: name = "f24"; break;
        default:             name = "?";         break;
        }
        snprintf(tmp + ti, sizeof(tmp) - ti, "%s", name);
    } else {
        if (ev->codepoint == (uint32_t)' ') {
            snprintf(tmp + ti, sizeof(tmp) - ti, "space");
        } else {
            tmp[ti++] = (char)ev->codepoint;
            tmp[ti]   = '\0';
        }
    }

    strncat(buf, tmp, sz - strlen(buf) - 1);
}

static bool keymap_add(Keymap *km, KeyEvent key, Binding binding) {
    for (size_t i = 0; i < km->count; i++) {
        if (keyevent_equal(&km->entries[i].key, &key)) {
            km->entries[i].binding = binding; // overwrite
            return true;
        }
    }

    if (km->count == km->cap) {
        km->cap = km->cap ? km->cap * 2 : 8;
        KeymapEntry *temp = realloc(km->entries,
                               km->cap * sizeof(KeymapEntry));
        if (temp == NULL) {
            fprintf(stderr, "Error adding binding: memory reallocation failed!\n");
            return false;
        } else {
            km->entries = temp;
        }
    }

    km->entries[km->count++] = (KeymapEntry){
        .key = key,
        .binding = binding,
    };
    return true;
}

// Local-only lookup (no parent chain) — used by keymap_bind_sequence
// to avoid leaking bindings into parent keymaps.
static Binding *keymap_lookup_local(Keymap *km, const KeyEvent *ev) {
    if (!km) return NULL;
    for (size_t i = 0; i < km->count; i++) {
        if (keyevent_equal(&km->entries[i].key, ev))
            return &km->entries[i].binding;
    }
    return NULL;
}

Binding *keymap_lookup(Keymap *km, const KeyEvent *ev) {
    for (Keymap *cur = km; cur; cur = cur->parent) {
        for (size_t i = 0; i < cur->count; i++) {
            if (keyevent_equal(&cur->entries[i].key, ev))
                return &cur->entries[i].binding;
        }
    }
    return NULL;
}

Binding *keymap_lookup_chain(AppState *state, const KeyEvent *ev) {
    Buffer *buf = buffer_get_current();
    Binding *b;

    // 1. Minor mode keymaps (head-first = most-recently-enabled)
    ModeList *minors = buffer_get_minor_modes(buf);
    if (minors) {
        for (ModeListNode *n = minors->head; n; n = n->next) {
            if (n->mode->keymap && (b = keymap_lookup(n->mode->keymap, ev)))
                return b;
        }
    }

    // 2. Buffer-local keymap
    Keymap *local = buffer_get_local_map(buf);
    if (local && (b = keymap_lookup(local, ev)))
        return b;

    // 3. Major mode keymap
    Mode *major = buffer_get_major_mode(buf);
    if (major && major->keymap && (b = keymap_lookup(major->keymap, ev)))
        return b;

    // 4. Pane keymap (pane-dependent commands; parent chain reaches global)
    if (state->input.pane_map && pane_get_root() != NULL)
        return keymap_lookup(state->input.pane_map, ev);
    // 5. Global keymap (base; always-available commands)
    return keymap_lookup(state->input.global_map, ev);
}

static void execute_command(AppState *state, Binding *b) {
    sexp ctx = state->chibi.ctx;

    // Call Scheme: (call-interactively sym)
    sexp result = sexp_apply(
        ctx,
        state->chibi.call_interactively,
        sexp_list1(ctx, b->command_sym)
    );

    if (sexp_exceptionp(result)) {
        sexp_print_exception(ctx, result, sexp_current_error_port(ctx));
    }
}

static void reset_key_state(AppState *state) {
    bool was_mid_prefix = (state->input.current_map != state->input.global_map);
    state->input.current_map = state->input.global_map;
    state->which_key.active = false;
    state->which_key.prefix_str[0] = '\0';
    if (was_mid_prefix) message_echo_clear();
}

static void key_dispatch_inner(AppState *state, const KeyEvent *ev) {
    assert(state->input.current_map);

    Binding *b;

    // If mid-prefix-sequence, continue in current_map only
    if (state->input.current_map != state->input.global_map) {
        b = keymap_lookup(state->input.current_map, ev);
    } else {
        // Start of sequence: use focus-appropriate lookup
        if (state->input.current_focus == FOCUS_MINIBUFFER) {
            // Only search minor mode keymaps (minibuffer-mode); no global
            // fallthrough so commands like command-palette can't nest.
            b = NULL;
            Buffer *buf = buffer_get_current();
            ModeList *minors = buffer_get_minor_modes(buf);
            if (minors) {
                for (ModeListNode *n = minors->head; n; n = n->next) {
                    if (n->mode->keymap && (b = keymap_lookup(n->mode->keymap, ev)))
                        break;
                }
            }
        } else if (state->input.current_focus == FOCUS_WELCOME && state->input.welcome_map) {
            b = keymap_lookup(state->input.welcome_map, ev);
        } else {
            b = keymap_lookup_chain(state, ev);
        }
    }

    if (!b) {
        if (state->input.current_map != state->input.global_map) {
            // In prefix sequence: invalid continuation is an error
            char seq[256];
            strncpy(seq, state->which_key.prefix_str, sizeof(seq) - 1);
            seq[sizeof(seq) - 1] = '\0';
            key_event_append_str(seq, sizeof(seq), ev);
            reset_key_state(state);
            char msg[270];
            snprintf(msg, sizeof(msg), "\"%s\" is undefined", seq);
            message_send(msg);
            return;
        }
        // Top-level unbound key
        if (ev->type == KEYEVENT_CHAR && ev->mods == MOD_NONE
            && state->input.current_focus != FOCUS_WELCOME) {
            Buffer *buf = buffer_get_current();
            ModeList *minors = buffer_get_minor_modes(buf);
            if (minors && minors->head && minors->head->mode->allows_input) {
                static Binding si;
                si.type = BINDING_COMMAND;
                si.command_sym = sexp_intern(state->chibi.ctx, "self-insert", -1);
                execute_command(state, &si);
                reset_key_state(state);
                goto record_macro;
            }
        }
        reset_key_state(state);
        message_echo_clear();
        if (state->input.key_unbound_cb != SEXP_FALSE) {
            sexp ctx = state->chibi.ctx;
            sexp result = sexp_apply(ctx, state->chibi.call_interactively,
                                     sexp_list1(ctx, state->input.key_unbound_cb));
            if (sexp_exceptionp(result))
                sexp_print_exception(ctx, result, sexp_current_error_port(ctx));
        }
        goto record_macro;  // silently ignore, but still record
    }

    if (b->type == BINDING_KEYMAP) {
        state->input.current_map = b->keymap;
        key_event_append_str(state->which_key.prefix_str,
                             sizeof(state->which_key.prefix_str), ev);
        message_echo(state->which_key.prefix_str);
        if (state->which_key.enabled) {
            state->which_key.active = true;
            state->which_key.keymap = b->keymap;
        }
        goto record_macro;  // prefix key: record for macro replay
    }

    execute_command(state, b);
    reset_key_state(state);

record_macro:
    // Record event AFTER dispatch so stop-macro can clear the flag first,
    // preventing the stop 'q' from being recorded.
    if (state->macro_recording) {
        if (state->macro_skip_next) {
            // Don't record the key that triggered start-macro (e.g. 'a' in 'qa')
            state->macro_skip_next = false;
        } else {
            if (state->macro_buf_len >= state->macro_buf_cap) {
                size_t cap = state->macro_buf_cap ? state->macro_buf_cap * 2 : 32;
                KeyEvent *nb = SDL_realloc(state->macro_buf, cap * sizeof(KeyEvent));
                if (nb) { state->macro_buf = nb; state->macro_buf_cap = cap; }
            }
            if (state->macro_buf_len < state->macro_buf_cap)
                state->macro_buf[state->macro_buf_len++] = *ev;
        }
    }
}

void key_dispatch(AppState *state, const KeyEvent *ev) {
    last_event = *ev;

    if (state->input.current_focus == FOCUS_SEARCH) {
        reset_key_state(state);
        Binding *b = state->input.search_map
                     ? keymap_lookup(state->input.search_map, ev) : NULL;
        if (!b && ev->type == KEYEVENT_CHAR && ev->mods == MOD_NONE) {
            static Binding si;
            si.type        = BINDING_COMMAND;
            si.command_sym = sexp_intern(state->chibi.ctx, "search-self-insert", -1);
            b = &si;
        }
        if (b && b->type == BINDING_COMMAND)
            execute_command(state, b);
        return;
    }

    if (state->input.current_focus == FOCUS_REPLACE) {
        reset_key_state(state);
        Binding *b = state->input.replace_map
                     ? keymap_lookup(state->input.replace_map, ev) : NULL;
        if (!b && ev->type == KEYEVENT_CHAR && ev->mods == MOD_NONE) {
            static Binding ri;
            ri.type        = BINDING_COMMAND;
            ri.command_sym = sexp_intern(state->chibi.ctx, "replace-self-insert", -1);
            b = &ri;
        }
        if (b && b->type == BINDING_COMMAND)
            execute_command(state, b);
        return;
    }

    if (state->input.current_focus == FOCUS_MINIBUFFER) {
        Buffer *saved = buffer_get_current();
        buffer_set_current(state->minibuf.buf);
        reset_key_state(state);       // clear any editor prefix state
        key_dispatch_inner(state, ev);
        if (state->minibuf.active) {  // submit/cancel already restored if false
            // Editing the text re-filters the item list, which can change what
            // is selected; preview it, as select-next/prev do for arrow keys.
            minibuf_refresh_after_edit(state);
            buffer_set_current(saved);
        }
        return;
    }

    if (state->input.key_intercept_cb != SEXP_FALSE) {
        key_event_append_str(state->input.key_intercept_str,
                             sizeof(state->input.key_intercept_str), ev);

        Binding *b = (state->input.key_intercept_map == state->input.global_map)
            ? keymap_lookup_chain(state, ev)
            : keymap_lookup(state->input.key_intercept_map, ev);

        if (b && b->type == BINDING_KEYMAP) {
            state->input.key_intercept_map = b->keymap;
            char echo[280];
            snprintf(echo, sizeof(echo), "Describe key: %s", state->input.key_intercept_str);
            message_send(echo);
            return;
        }

        // Reached command or unbound — fire callback
        sexp ctx = state->chibi.ctx;
        sexp cb  = state->input.key_intercept_cb;
        state->input.key_intercept_cb = SEXP_FALSE;
        sexp sym = (b && b->type == BINDING_COMMAND) ? b->command_sym : SEXP_FALSE;
        sexp kstr = sexp_c_string(ctx, state->input.key_intercept_str, -1);
        sexp result = sexp_apply(ctx, cb, sexp_list2(ctx, sym, kstr));
        if (sexp_exceptionp(result))
            sexp_print_exception(ctx, result, sexp_current_error_port(ctx));
        sexp_release_object(ctx, cb);
        return;
    }

    key_dispatch_inner(state, ev);
}

/* Boundary check: key name at s[0..len-1] must be followed by space or NUL. */
#define KEY_WORD(str, len) \
    (strncmp(s, str, len) == 0 && (s[len] == '\0' || s[len] == ' '))

int parse_key_sequence(const char *s, KeyEvent *out) {
    int count = 0;
    uint16_t mods = 0;

    while (*s) {
        if (*s == ' ') { s++; continue; }

        mods = 0;

        /* modifiers: consume full-word prefixes */
        while (true) {
            if      (strncmp(s, "ctrl-",  5) == 0) { mods |= MOD_CTRL;  s += 5; }
            else if (strncmp(s, "alt-",   4) == 0) { mods |= MOD_META;  s += 4; }
            else if (strncmp(s, "shift-", 6) == 0) { mods |= MOD_SHIFT; s += 6; }
            else break;
        }

        /* special key names */
        if      (KEY_WORD("space",     5)) { out[count++] = (KeyEvent){KEYEVENT_CHAR,    mods, {.codepoint = ' '         }}; s += 5; }
        else if (KEY_WORD("return",    6)) { out[count++] = (KeyEvent){KEYEVENT_SPECIAL, mods, {.keycode = KEY_RETURN    }}; s += 6; }
        else if (KEY_WORD("escape",    6)) { out[count++] = (KeyEvent){KEYEVENT_SPECIAL, mods, {.keycode = KEY_ESC       }}; s += 6; }
        else if (KEY_WORD("tab",       3)) { out[count++] = (KeyEvent){KEYEVENT_SPECIAL, mods, {.keycode = KEY_TAB       }}; s += 3; }
        else if (KEY_WORD("backspace", 9)) { out[count++] = (KeyEvent){KEYEVENT_SPECIAL, mods, {.keycode = KEY_BACKSPACE }}; s += 9; }
        else if (KEY_WORD("delete",    6)) { out[count++] = (KeyEvent){KEYEVENT_SPECIAL, mods, {.keycode = KEY_DELETE    }}; s += 6; }
        else if (KEY_WORD("up",        2)) { out[count++] = (KeyEvent){KEYEVENT_SPECIAL, mods, {.keycode = KEY_UP        }}; s += 2; }
        else if (KEY_WORD("down",      4)) { out[count++] = (KeyEvent){KEYEVENT_SPECIAL, mods, {.keycode = KEY_DOWN      }}; s += 4; }
        else if (KEY_WORD("left",      4)) { out[count++] = (KeyEvent){KEYEVENT_SPECIAL, mods, {.keycode = KEY_LEFT      }}; s += 4; }
        else if (KEY_WORD("right",     5)) { out[count++] = (KeyEvent){KEYEVENT_SPECIAL, mods, {.keycode = KEY_RIGHT     }}; s += 5; }
        else if (KEY_WORD("home",      4)) { out[count++] = (KeyEvent){KEYEVENT_SPECIAL, mods, {.keycode = KEY_HOME      }}; s += 4; }
        else if (KEY_WORD("end",       3)) { out[count++] = (KeyEvent){KEYEVENT_SPECIAL, mods, {.keycode = KEY_END       }}; s += 3; }
        else if (KEY_WORD("pageup",    6)) { out[count++] = (KeyEvent){KEYEVENT_SPECIAL, mods, {.keycode = KEY_PAGE_UP   }}; s += 6; }
        else if (KEY_WORD("pagedown",  8)) { out[count++] = (KeyEvent){KEYEVENT_SPECIAL, mods, {.keycode = KEY_PAGE_DOWN }}; s += 8; }
        else if (KEY_WORD("insert",    6)) { out[count++] = (KeyEvent){KEYEVENT_SPECIAL, mods, {.keycode = KEY_INSERT    }}; s += 6; }
        else if (KEY_WORD("menu",      4)) { out[count++] = (KeyEvent){KEYEVENT_SPECIAL, mods, {.keycode = KEY_MENU      }}; s += 4; }
        else if (KEY_WORD("print",     5)) { out[count++] = (KeyEvent){KEYEVENT_SPECIAL, mods, {.keycode = KEY_PRINT     }}; s += 5; }
        else if (KEY_WORD("pause",     5)) { out[count++] = (KeyEvent){KEYEVENT_SPECIAL, mods, {.keycode = KEY_PAUSE     }}; s += 5; }
        else if (s[0] == 'f' && s[1] >= '1' && s[1] <= '9') {
            /* function keys f1–f24 */
            int n = s[1] - '0';
            int skip = 2;
            if (s[2] >= '0' && s[2] <= '9') { n = n * 10 + (s[2] - '0'); skip = 3; }
            if (n >= 1 && n <= 24 && (s[skip] == '\0' || s[skip] == ' ')) {
                out[count++] = (KeyEvent){KEYEVENT_SPECIAL, mods, {.keycode = KEY_F1 + (n - 1)}};
                s += skip;
            } else if (*s) {
                out[count++] = (KeyEvent){KEYEVENT_CHAR, mods, {.codepoint = (uint32_t)*s}};
                s++;
            }
        }
        else if (*s) {
            out[count++] = (KeyEvent){KEYEVENT_CHAR, mods, {.codepoint = (uint32_t)*s}};
            s++;
        }
    }

    return count;
}

#undef KEY_WORD

void keymap_bind_sequence(Keymap *km, KeyEvent *seq, int n, Binding final) {
    for (int i = 0; i < n - 1; i++) {
        Binding *b = keymap_lookup_local(km, &seq[i]);

        if (!b || b->type != BINDING_KEYMAP) {
            Keymap *next = keymap_create();

            Binding nb = {
                .type = BINDING_KEYMAP,
                .keymap = next
            };

            keymap_add(km, seq[i], nb);
            km = next;
        } else {
            km = b->keymap;
        }
    }

    /* final key */
    keymap_add(km, seq[n - 1], final);
}

void keymap_bind_prefix_sequence(Keymap *km, KeyEvent *seq, int n, Keymap *child) {
    // Navigate / create intermediate keymaps for the first n-1 keys
    for (int i = 0; i < n - 1; i++) {
        Binding *b = keymap_lookup_local(km, &seq[i]);

        if (!b || b->type != BINDING_KEYMAP) {
            Keymap *next = keymap_create();

            Binding nb = {
                .type = BINDING_KEYMAP,
                .keymap = next
            };

            keymap_add(km, seq[i], nb);
            km = next;
        } else {
            km = b->keymap;
        }
    }

    // Bind the final key to the supplied child keymap
    Binding final = {
        .type = BINDING_KEYMAP,
        .keymap = child
    };
    keymap_add(km, seq[n - 1], final);
}

// --- Scheme bindings ---

sexp scm_make_keymap(sexp ctx, sexp self, sexp n) {
    Keymap *km = keymap_create();
    if (!km) {
        return SEXP_VOID;
    }
    sexp result = sexp_make_cpointer(ctx,
        SEXP_CPOINTER,
        km,
        SEXP_FALSE,
        0);
    return result;
}

sexp scm_set_key(sexp ctx, sexp self, sexp n,
                 sexp skeymap, sexp skeystr, sexp scommand) {
    Keymap *km = sexp_cpointer_value(skeymap);
    if (!km) {
        return sexp_user_exception(ctx, self, "null keymap pointer", skeymap);
    }

    if (!sexp_symbolp(scommand)) {
        return sexp_user_exception(ctx, self,
            "command must be a symbol", scommand);
    }

    const char *keystr = sexp_string_data(skeystr);
    KeyEvent seq[8];
    int key_count = parse_key_sequence(keystr, seq);
    if (key_count < 1) {
        return sexp_user_exception(ctx, self, "invalid key sequence", skeystr);
    }

    Binding binding = {
        .type = BINDING_COMMAND,
        .command_sym = scommand
    };

    sexp_preserve_object(ctx, binding.command_sym);

    keymap_bind_sequence(km, seq, key_count, binding);

    return SEXP_VOID;
}

// (%set-keymap-parent! keymap parent) -> void
sexp scm_set_keymap_parent(sexp ctx, sexp self, sexp n,
                           sexp skeymap, sexp sparent) {
    Keymap *km = sexp_cpointer_value(skeymap);
    if (!km) return sexp_user_exception(ctx, self, "null keymap", skeymap);
    Keymap *parent = sexp_cpointer_value(sparent);
    km->parent = parent;
    return SEXP_VOID;
}

// (%set-keymap-name! keymap name) -> void
sexp scm_set_keymap_name(sexp ctx, sexp self, sexp n,
                         sexp skeymap, sexp sname) {
    Keymap *km = sexp_cpointer_value(skeymap);
    if (!km) return sexp_user_exception(ctx, self, "null keymap", skeymap);
    if (!sexp_stringp(sname))
        return sexp_user_exception(ctx, self, "name must be a string", sname);
    free(km->name);
    km->name = strdup(sexp_string_data(sname));
    return SEXP_VOID;
}

// (%bind-prefix! parent keystr child) -> void
sexp scm_bind_prefix(sexp ctx, sexp self, sexp n,
                     sexp sparent, sexp skeystr, sexp schild) {
    Keymap *parent = sexp_cpointer_value(sparent);
    if (!parent) return sexp_user_exception(ctx, self, "null parent keymap", sparent);
    Keymap *child = sexp_cpointer_value(schild);
    if (!child) return sexp_user_exception(ctx, self, "null child keymap", schild);
    if (!sexp_stringp(skeystr))
        return sexp_user_exception(ctx, self, "key must be a string", skeystr);

    const char *keystr = sexp_string_data(skeystr);
    KeyEvent seq[8];
    int key_count = parse_key_sequence(keystr, seq);
    if (key_count < 1)
        return sexp_user_exception(ctx, self, "invalid key sequence", skeystr);

    keymap_bind_prefix_sequence(parent, seq, key_count, child);
    return SEXP_VOID;
}

// (%read-key-binding callback) -> void
// Activates key-intercept mode: next key sequence (until a command or unbound)
// calls callback with (sym key-str), where sym is the bound command symbol or #f.
sexp scm_read_key_binding(sexp ctx, sexp self, sexp n, sexp callback) {
    if (G->input.key_intercept_cb != SEXP_FALSE)
        sexp_release_object(ctx, G->input.key_intercept_cb);
    G->input.key_intercept_cb = callback;
    sexp_preserve_object(ctx, G->input.key_intercept_cb);
    G->input.key_intercept_map = G->input.global_map;
    G->input.key_intercept_str[0] = '\0';
    return SEXP_VOID;
}

// Returns the first keybinding string for the named command, writing into buf.
// Returns buf on success, NULL if the command has no bindings or on error.
const char *keymap_where_is_first(AppState *state, const char *cmd_name,
                                   char *buf, size_t buf_len) {
    sexp ctx = state->chibi.ctx;
    sexp env = state->chibi.env;
    sexp fn  = sexp_env_ref(ctx, env, sexp_intern(ctx, "where-is", -1), SEXP_FALSE);
    if (!sexp_procedurep(fn)) return NULL;
    sexp result = sexp_apply(ctx, fn, sexp_list1(ctx, sexp_intern(ctx, cmd_name, -1)));
    if (sexp_exceptionp(result) || !sexp_pairp(result)) return NULL;
    sexp first = sexp_car(result);
    if (!sexp_stringp(first)) return NULL;
    strncpy(buf, sexp_string_data(first), buf_len - 1);
    buf[buf_len - 1] = '\0';
    return buf;
}

// (%set-key-unbound-cb! sym-or-#f) -> void
// When set to a symbol, that command is called via call-interactively
// whenever a key is silently ignored (no binding found, no self-insert).
sexp scm_set_key_unbound_cb(sexp ctx, sexp self, sexp n, sexp sym) {
    if (sym == SEXP_FALSE) {
        G->input.key_unbound_cb = SEXP_FALSE;
    } else if (sexp_symbolp(sym)) {
        G->input.key_unbound_cb = sym;
        sexp_preserve_object(ctx, G->input.key_unbound_cb);
    } else {
        return sexp_user_exception(ctx, self, "must be a symbol or #f", sym);
    }
    return SEXP_VOID;
}

