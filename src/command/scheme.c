#include <SDL3/SDL_events.h>
#ifndef __EMSCRIPTEN__
#include <SDL3/SDL_dialog.h>
#endif
#include <chibi/eval.h>
#include <chibi/sexp.h>
#ifndef __EMSCRIPTEN__
#include <unistd.h>
#endif

#include "message.h"
#include "minibuf.h"
#include "scheme.h"
#include "scheme_bindings.h"
#include "../file_scanner.h"
#include "../text/buffer.h"
#include "../text/buffer_type.h"
#include "../text/var.h"
#include "../display/pane.h"
#include "../display/tab.h"
#include "../display/scale.h"

AppState *G;   // global app state for commands

static sexp scm_scanner_start(sexp ctx, sexp self, sexp n) {
    (void)ctx; (void)self; (void)n;
    scanner_restart(&G->scanner);
    return SEXP_VOID;
}

static sexp scm_quit(sexp ctx, sexp self, sexp n) {
    SDL_Event quit_event;
    quit_event.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&quit_event);
    return SEXP_VOID;
}

static sexp scm_eval_buffer(sexp ctx, sexp self, sexp n) {

    sexp_gc_var2(result, str);
    sexp_gc_preserve2(ctx, result, str);

    char *eval_src = buffer_text(buffer_get_current());
    result = sexp_eval_string(ctx, eval_src, -1, NULL);
    free(eval_src);

    if (sexp_exceptionp(result)) {
        sexp_print_exception(ctx, result, sexp_current_error_port(ctx));
        str = sexp_write_to_string(ctx, sexp_exception_message(result));
        Pane *pane = pane_split_horizontal(pane_get_active());
        const char *NAME = "*Backtrace*";
        Buffer *buf = buffer_get_by_name(NAME);
        if (!buf) {
            buffer_create(NAME);
            buf = buffer_get_by_name(NAME);
        } else {
            buffer_clear(buf);
        }
        insert_string(buf, "Error: ");
        insert_string(buf, sexp_string_data(str) + 1);
        delete_chars(buf, 1);
        str = sexp_write_to_string(ctx, sexp_exception_irritants(result));
        insert_string(buf, ": ");
        insert_string(buf, sexp_string_data(str) + 1);
        delete_chars(buf, 1);
        tab_set_buffer(pane->content.active_tab, buf);
        pane_set_active(pane);
    } else {
        str = sexp_write_to_string(ctx, result);
        message_send(sexp_string_data(str));
    }

    sexp_gc_release2(ctx);

    return SEXP_VOID;
}

static sexp scm_pop_to_buffer(sexp ctx, sexp self, sexp n, sexp sname) {
    if (!sexp_stringp(sname))
        return sexp_user_exception(ctx, self, "buffer name must be a string", sname);
    const char *name = sexp_string_data(sname);
    Pane *active = pane_get_active();
    Pane *pane;
    if (active)
        pane = pane_split_horizontal(active);
    else {
        if (!tab_new_with_buffer(name, false)) return SEXP_FALSE;
        pane = pane_get_active();
    }
    Buffer *buf = buffer_get_by_name(name);
    if (!buf) {
        buffer_create(name);
        buf = buffer_get_by_name(name);
    } else {
        buffer_clear(buf);
    }
    tab_set_buffer(pane->content.active_tab, buf);
    pane_set_active(pane);
    return SEXP_VOID;
}

static sexp scm_clay_debug(sexp ctx, sexp self, sexp n) {
    G->debug_open = !G->debug_open;
    Clay_SetDebugModeEnabled(G->debug_open);

    return SEXP_VOID;
}

static sexp scm_prefix_arg(sexp ctx, sexp self, sexp n) {
    return SEXP_FALSE;  // TODO: integrate with C-u handling
}

// (ignore) - do nothing
static sexp scm_ignore(sexp ctx, sexp self, sexp n) {
    return SEXP_VOID;
}

static sexp scm_set_palette(sexp ctx, sexp self, sexp n, sexp key, sexp hexstr) {
    if (!sexp_symbolp(key)) {
        return sexp_user_exception(ctx, self, "name must be a symbol", key);
    }

    sexp_preserve_object(ctx, hexstr);
    vartable_set(&G->ui.palette_table, key, hexstr);
    return hexstr;
}

static sexp scm_set_role(sexp ctx, sexp self, sexp n, sexp role, sexp val) {
    if (!sexp_symbolp(role))
        return sexp_user_exception(ctx, self, "role must be a symbol", role);
    if (!sexp_symbolp(val) && !sexp_pairp(val))
        return sexp_user_exception(ctx, self, "role value must be a symbol or plist", val);

    sexp_preserve_object(ctx, val);
    vartable_set(&G->ui.role_table, role, val);
    return val;
}

static sexp scm_clear_palette(sexp ctx, sexp self, sexp n) {
    vartable_destroy(&G->ui.palette_table);
    vartable_init(&G->ui.palette_table);
    return SEXP_VOID;
}

static sexp scm_clear_roles(sexp ctx, sexp self, sexp n) {
    vartable_destroy(&G->ui.role_table);
    vartable_init(&G->ui.role_table);
    return SEXP_VOID;
}

static sexp scm_clipboard_get(sexp ctx, sexp self, sexp n) {
    char *text = SDL_GetClipboardText();
    sexp result = sexp_c_string(ctx, text ? text : "", -1);
    SDL_free(text);
    return result;
}

static sexp scm_clipboard_set(sexp ctx, sexp self, sexp n, sexp stext) {
    sexp_assert_type(ctx, sexp_stringp, SEXP_STRING, stext);
    SDL_SetClipboardText(sexp_string_data(stext));
    return SEXP_VOID;
}

static sexp scm_buffer_file_name(sexp ctx, sexp self, sexp n) {
    char name[FILE_NAME_MAX] = {0};
    get_file_name(name, sizeof(name));
    if (name[0] == '\0') {
        return SEXP_FALSE;
    }
    return sexp_c_string(ctx, name, -1);
}

static sexp scm_set_buffer_file_name(sexp ctx, sexp self, sexp n, sexp sname) {
    if (!sexp_stringp(sname))
        return sexp_user_exception(ctx, self, "filename must be a string", sname);
    set_file_name(sexp_string_data(sname));
    return SEXP_VOID;
}

static sexp scm_buffer_write(sexp ctx, sexp self, sexp n) {
    bool ok = buffer_write();
    return ok ? SEXP_TRUE : SEXP_FALSE;
}

static sexp scm_buffer_modified_p(sexp ctx, sexp self, sexp n) {
    return get_modified() ? SEXP_TRUE : SEXP_FALSE;
}

static sexp scm_set_buffer_modified(sexp ctx, sexp self, sexp n, sexp val) {
    set_modified(sexp_truep(val));
    return SEXP_VOID;
}

static sexp scm_buffer_set_name(sexp ctx, sexp self, sexp n, sexp sname) {
    if (!sexp_stringp(sname))
        return sexp_user_exception(ctx, self, "buffer name must be a string", sname);
    bool ok = buffer_set_name(sexp_string_data(sname));
    return ok ? SEXP_TRUE : SEXP_FALSE;
}

static sexp scm_buffer_insert(sexp ctx, sexp self, sexp n, sexp sname) {
    if (!sexp_stringp(sname))
        return sexp_user_exception(ctx, self, "filename must be a string", sname);
    bool ok = buffer_insert(sexp_string_data(sname));
    return ok ? SEXP_TRUE : SEXP_FALSE;
}

static sexp scm_buffer_read(sexp ctx, sexp self, sexp n) {
    bool ok = buffer_read();
    return ok ? SEXP_TRUE : SEXP_FALSE;
}

static sexp scm_buffer_create(sexp ctx, sexp self, sexp n, sexp sname) {
    if (!sexp_stringp(sname))
        return sexp_user_exception(ctx, self, "buffer name must be a string", sname);
    Buffer *buf = buffer_create(sexp_string_data(sname));
    return buf ? SEXP_TRUE : SEXP_FALSE;
}

static sexp scm_set_welcome_keymap(sexp ctx, sexp self, sexp n, sexp skm) {
    if (!sexp_cpointerp(skm))
        return sexp_user_exception(ctx, self, "expected keymap cpointer", skm);
    G->input.welcome_map = sexp_cpointer_value(skm);
    return SEXP_VOID;
}

static sexp scm_tab_set_buffer(sexp ctx, sexp self, sexp n, sexp sname) {
    if (!sexp_stringp(sname))
        return sexp_user_exception(ctx, self, "buffer name must be a string", sname);
    Buffer *buf = buffer_get_by_name(sexp_string_data(sname));
    if (!buf) return SEXP_FALSE;
    Pane *pane = pane_get_active();
    if (!pane || !pane->content.active_tab) return SEXP_FALSE;
    // If another tab in this pane already shows buf, switch to it.
    Tab *existing = pane_tab_for_buffer(pane, buf);
    if (existing) {
        pane->content.active_tab = existing;
        sync_active_buffer();
        return SEXP_TRUE;
    }
    if (buf != buffer_get_current())
        pane_push_jump();
    // If the active tab is not in preview mode, open in a new preview tab.
    if (!pane->content.active_tab->is_preview) {
        Tab *tab = display_tab_new(pane, buf);
        if (!tab) return SEXP_FALSE;
        tab->is_preview = true;
        sync_active_buffer();
        return SEXP_TRUE;
    }
    // Current tab is preview: replace its buffer (remains preview).
    tab_set_buffer(pane->content.active_tab, buf);
    sync_active_buffer();
    return SEXP_TRUE;
}

static sexp scm_tab_set_preview(sexp ctx, sexp self, sexp n, sexp sbool) {
    Pane *pane = pane_get_active();
    if (!pane || !pane->content.active_tab) return SEXP_FALSE;
    pane->content.active_tab->is_preview = sexp_truep(sbool);
    return SEXP_TRUE;
}


#ifndef __EMSCRIPTEN__
static void folder_dialog_callback(void *userdata, const char * const *filelist, int filter) {
    (void)filter;
    AppState *state = (AppState *)userdata;
    if (!filelist || !filelist[0]) return;
    SDL_strlcpy(state->pending_folder_dialog, filelist[0], PATH_MAX);
    SDL_Event ev = {0};
    ev.type = SDL_EVENT_USER;
    ev.user.code = FOLDER_DIALOG_EVENT;
    SDL_PushEvent(&ev);
}

static sexp scm_show_open_folder_dialog(sexp ctx, sexp self, sexp n) {
    (void)ctx; (void)self; (void)n;
    SDL_ShowOpenFolderDialog(folder_dialog_callback, G, G->window, NULL, false);
    return SEXP_TRUE;
}
#else
static sexp scm_show_open_folder_dialog(sexp ctx, sexp self, sexp n) {
    (void)ctx; (void)self; (void)n;
    return SEXP_FALSE;
}
#endif


void scheme_init(AppState *state) {
    G = state;
    sexp_scheme_init();

#ifdef __EMSCRIPTEN__
    sexp ctx = sexp_make_eval_context(NULL, NULL, NULL, 0, 16*1024*1024);
#else
    sexp ctx = sexp_make_eval_context(NULL, NULL, NULL, 0, 512*1024*1024);
#endif
    if (!ctx || sexp_exceptionp(ctx)) {
        fprintf(stderr, "ERROR: failed to create eval context\n");
        return;
    }

#ifndef __EMSCRIPTEN__
    // Prepend the lib/ directory next to the binary to the module path before
    // loading the standard env so chibi can find init-7.scm without requiring
    // a system-wide chibi-scheme installation.
    char *basePath = (char *)SDL_GetBasePath();
    char libPath[1024];
    snprintf(libPath, sizeof(libPath), "%slib", basePath);
    {
        sexp_gc_var2(ldir, lpath);
        sexp_gc_preserve2(ctx, ldir, lpath);
        ldir = sexp_c_string(ctx, libPath, -1);
        lpath = sexp_cons(ctx, ldir, sexp_global(ctx, SEXP_G_MODULE_PATH));
        sexp_global(ctx, SEXP_G_MODULE_PATH) = lpath;
        sexp_gc_release2(ctx);
    }
#endif

    sexp env = sexp_load_standard_env(ctx, NULL, SEXP_SEVEN);
    if (sexp_exceptionp(env)) {
        fprintf(stderr, "ERROR: failed to load standard env\n");
        // Try to print exception message directly
        sexp msg = sexp_exception_message(env);
        if (sexp_stringp(msg)) {
            fprintf(stderr, "  Exception: %s\n", sexp_string_data(msg));
        }
        sexp irritants = sexp_exception_irritants(env);
        if (sexp_pairp(irritants) && sexp_stringp(sexp_car(irritants))) {
            fprintf(stderr, "  Irritant: %s\n", sexp_string_data(sexp_car(irritants)));
        }
        return;
    }

    sexp_load_standard_ports(ctx, env, stdin, stdout, stderr, 0);

    // Import (scheme base) for when, unless, guard, etc.
    // Exclude bindings already provided by sexp_load_standard_env to avoid warnings.
    sexp_eval_string(ctx, "(import (except (scheme base) equal? let-syntax letrec-syntax))", -1, env);

    state->chibi.ctx = ctx;
    state->chibi.env = env;

    sexp_gc_var4(global_km, pane_km, search_km, replace_km);
    sexp_gc_preserve4(ctx, global_km, pane_km, search_km, replace_km);

    // DON'T register custom types for now - just use built-in CPOINTER
    global_km = sexp_make_cpointer(
        ctx,
        SEXP_CPOINTER,
        state->input.global_map,
        SEXP_FALSE,
        0
    );

    sexp_env_define(ctx, env,
                    sexp_intern(ctx, "global-keymap", -1),
                    global_km);

    pane_km = sexp_make_cpointer(
        ctx,
        SEXP_CPOINTER,
        state->input.pane_map,
        SEXP_FALSE,
        0
    );

    sexp_env_define(ctx, env,
                    sexp_intern(ctx, "pane-keymap", -1),
                    pane_km);

    search_km = sexp_make_cpointer(
        ctx,
        SEXP_CPOINTER,
        state->input.search_map,
        SEXP_FALSE,
        0
    );

    sexp_env_define(ctx, env,
                    sexp_intern(ctx, "search-keymap", -1),
                    search_km);

    replace_km = sexp_make_cpointer(
        ctx,
        SEXP_CPOINTER,
        state->input.replace_map,
        SEXP_FALSE,
        0
    );

    sexp_env_define(ctx, env,
                    sexp_intern(ctx, "replace-keymap", -1),
                    replace_km);

    sexp_gc_release4(ctx);

    #define SDEF(a, b, c) sexp_define_foreign(ctx, env, a, b, c)

    // Register foreign functions
    SDEF("quit", 0, scm_quit);
    sexp_define_foreign_opt(ctx, env, "message", 1, (sexp_proc1)scm_message, SEXP_FALSE);
    SDEF("message-echo", 1, scm_message_echo_scm);
    SDEF("message-clear", 0, scm_message_clear);
    SDEF("message-lock", 0, scm_message_lock);
    SDEF("message-unlock", 0, scm_message_unlock);
    SDEF("make-keymap", 0, scm_make_keymap);
    SDEF("%set-key!", 3, scm_set_key);
    SDEF("insert-char", 1, scm_insert_char);
    SDEF("self-insert", 0, scm_self_insert);
    SDEF("delete-char", 1, scm_delete_char);
    SDEF("move-point", 1, scm_point_move);
    SDEF("move-point-by-line", 1, scm_point_move_by_line);
    SDEF("next-line", 0, scm_next_line);
    SDEF("prev-line", 0, scm_prev_line);
    SDEF("forward-char", 0, scm_forward_char);
    SDEF("backward-char", 0, scm_backward_char);
    SDEF("newline", 0, scm_newline);
    SDEF("insert-tab", 0, scm_insert_tab);
    SDEF("delete-backward-char", 0, scm_delete_backward_char);
    SDEF("delete-forward-char", 0, scm_delete_forward_char);
    SDEF("set-column", 1, scm_set_column);
    SDEF("line-start", 0, scm_point_to_line_start);
    SDEF("line-end", 0, scm_point_to_line_end);
    SDEF("skip-whitespace", 0, scm_skip_whitespace);
    SDEF("set-column", 1, scm_set_column);
    SDEF("char-at-point", 0, scm_char_at_point);
    SDEF("point-get", 0, scm_point_get);
    SDEF("point-set!", 1, scm_point_set_to);
    SDEF("buffer-length", 0, scm_buffer_length);
    SDEF("delete-range", 2, scm_delete_range);
    SDEF("char-at", 1, scm_char_at);
    SDEF("last-key-char", 0, scm_last_key_char);
    SDEF("jump-to-matching-bracket", 0, scm_jump_to_matching_bracket);
    SDEF("%set-replace-mode!", 1, scm_set_replace_mode);
    SDEF("%tab-next", 0, scm_tab_next);
    SDEF("%tab-prev", 0, scm_tab_prev);
    SDEF("%tab-new!", 1, scm_tab_new);
    SDEF("%tab-new-fresh!", 1, scm_tab_new_fresh);
    SDEF("no-panes?", 0, scm_no_panes_p);
    SDEF("reset-global-scale", 0, scm_reset_global_scale);
    SDEF("increase-global-scale", 0, scm_increase_global_scale);
    SDEF("decrease-global-scale", 0, scm_decrease_global_scale);
    SDEF("%reset-buffer-scale", 0, scm_reset_buffer_scale);
    SDEF("%increase-buffer-scale", 0, scm_increase_buffer_scale);
    SDEF("%decrease-buffer-scale", 0, scm_decrease_buffer_scale);
    SDEF("%split-vertical", 0, scm_split_vertical);
    SDEF("%split-horizontal", 0, scm_split_horizontal);
    SDEF("%tab-close", 0, scm_tab_close);
    SDEF("%pop-to-buffer", 1, scm_pop_to_buffer);
    SDEF("%pane-navigate-up", 0, scm_pane_navigate_up);
    SDEF("%pane-navigate-down", 0, scm_pane_navigate_down);
    SDEF("%pane-navigate-left", 0, scm_pane_navigate_left);
    SDEF("%pane-navigate-right", 0, scm_pane_navigate_right);
    SDEF("%pane-v-split-increase", 0, scm_pane_v_split_increase);
    SDEF("%pane-v-split-decrease", 0, scm_pane_v_split_decrease);
    SDEF("%pane-h-split-increase", 0, scm_pane_h_split_increase);
    SDEF("%pane-h-split-decrease", 0, scm_pane_h_split_decrease);
    SDEF("eval-buffer", 0, scm_eval_buffer);
    SDEF("%ts-tree-string", 0, scm_ts_tree_string);
    SDEF("%ts-enable!", 0, scm_ts_enable);
    SDEF("%ts-disable!", 0, scm_ts_disable);
    SDEF("%ts-enable-json!", 0, scm_ts_enable_json);
    SDEF("clay-debug", 0, scm_clay_debug);
    SDEF("prefix-arg", 0, scm_prefix_arg);
    SDEF("%set-keymap-parent!", 2, scm_set_keymap_parent);
    SDEF("%set-keymap-name!", 2, scm_set_keymap_name);
    SDEF("%bind-prefix!", 3, scm_bind_prefix);
    SDEF("%read-key-binding", 1, scm_read_key_binding);
    SDEF("%set-key-unbound-cb!", 1, scm_set_key_unbound_cb);
    SDEF("%set-mode-allows-input!", 2, scm_set_mode_allows_input);
    SDEF("ignore", 0, scm_ignore);
    SDEF("%buffer-has-minor-mode?", 1, scm_buffer_has_minor_mode);
    SDEF("%buffer-file-name", 0, scm_buffer_file_name);
    SDEF("%set-buffer-file-name!", 1, scm_set_buffer_file_name);
    SDEF("%buffer-write", 0, scm_buffer_write);
    SDEF("%buffer-modified?", 0, scm_buffer_modified_p);
    SDEF("%set-buffer-modified!", 1, scm_set_buffer_modified);
    SDEF("%buffer-set-name!", 1, scm_buffer_set_name);
    SDEF("%buffer-insert", 1, scm_buffer_insert);
    SDEF("%buffer-read", 0, scm_buffer_read);
    SDEF("%buffer-create", 1, scm_buffer_create);
    SDEF("%tab-set-buffer!", 1, scm_tab_set_buffer);
    SDEF("%tab-set-preview!", 1, scm_tab_set_preview);
    SDEF("%buffer-close!", 1, scm_buffer_close);
    SDEF("%set-welcome-keymap!", 1, scm_set_welcome_keymap);

    // Mode primitives
    SDEF("%register-mode", 3, scm_register_mode);
    SDEF("%set-major-mode", 1, scm_set_major_mode);
    SDEF("%enable-minor-mode", 1, scm_enable_minor_mode);
    SDEF("%disable-minor-mode", 1, scm_disable_minor_mode);
    SDEF("%buffer-major-mode", 0, scm_buffer_major_mode);
    SDEF("%buffer-minor-modes", 0, scm_buffer_minor_modes);

    // Variable primitives
    SDEF("%set-local!", 2, scm_set_local);
    SDEF("%get-local", 2, scm_get_local);
    SDEF("%set-palette!", 2, scm_set_palette);
    SDEF("%update-icon-colors!", 0, scm_update_icon_colors);
    SDEF("%register-icon!", 3, scm_register_icon);
    SDEF("%register-mode-icon!", 6, scm_register_mode_icon);
    SDEF("%register-major-mode-info!", 3, scm_register_major_mode_info);
    SDEF("%set-cursor-override!", 1, scm_set_cursor_override);
    SDEF("%set-role!", 2, scm_set_role);
    SDEF("%clear-palette!", 0, scm_clear_palette);
    SDEF("%clear-roles!", 0, scm_clear_roles);

    // Change / undo primitives
    SDEF("%begin-change", 0, scm_begin_change);
    SDEF("%end-change", 0, scm_end_change);
    SDEF("%undo", 0, scm_undo);
    SDEF("%redo", 0, scm_redo);
    SDEF("%change-active?", 0, scm_change_active);
    SDEF("%change-set-repeat-info!", 1, scm_change_set_repeat_info);
    SDEF("%change-last-repeat-info", 0, scm_change_last_repeat_info);
    SDEF("%line-restore", 0, scm_line_restore);
    SDEF("%change-current-inserts", 0, scm_change_current_inserts);

    // Clipboard primitives
    SDEF("%clipboard-get",  0, scm_clipboard_get);
    SDEF("%clipboard-set!", 1, scm_clipboard_set);

    // Register primitives
    SDEF("%register-set!",       2, scm_register_set);
    SDEF("%register-append!",    2, scm_register_append);
    SDEF("%register-get",        1, scm_register_get);
    SDEF("%register-set-shape!",       2, scm_register_set_shape);
    SDEF("%register-shape",            1, scm_register_get_shape);
    SDEF("%register-set-block-width!", 2, scm_register_set_block_width);
    SDEF("%register-block-width",      1, scm_register_get_block_width);
    SDEF("%buffer-substring", 2, scm_buffer_substring);
    SDEF("%insert-string",    1, scm_insert_string);

    // Macro primitives
    SDEF("%macro-start!",     1, scm_macro_start);
    SDEF("%macro-stop!",      0, scm_macro_stop);
    SDEF("%macro-play",       1, scm_macro_play);
    SDEF("%macro-recording?", 0, scm_macro_is_recording);

    // Minibuffer primitives
    SDEF("%minibuffer-activate", 3, scm_minibuffer_activate);
    SDEF("%minibuffer-submit",   0, scm_minibuffer_submit);
    SDEF("%minibuffer-cancel",   0, scm_minibuffer_cancel);
    SDEF("%minibuffer-active?",  0, scm_minibuffer_activep);
    SDEF("%minibuffer-activate-commands",         0, scm_minibuffer_activate_commands);
    SDEF("%minibuffer-activate-describe-function", 0, scm_minibuffer_activate_describe_function);
    SDEF("%minibuffer-activate-describe-command",  0, scm_minibuffer_activate_describe_command);
    SDEF("%minibuffer-activate-describe-variable", 0, scm_minibuffer_activate_describe_variable);
    SDEF("%minibuffer-activate-themes",           0, scm_minibuffer_activate_themes);
    SDEF("%minibuffer-activate-major-modes!",     0, scm_minibuffer_activate_major_modes);
    SDEF("%minibuffer-select-next",       0, scm_minibuffer_select_next);
    SDEF("%minibuffer-select-prev",       0, scm_minibuffer_select_prev);
    SDEF("%minibuffer-activate-file-picker", 0, scm_minibuffer_activate_file_picker);

    // File scanner primitives
    SDEF("%scanner-start!", 0, scm_scanner_start);

    // Which-key primitives
    SDEF("%which-key-toggle", 0, scm_which_key_toggle);

    // State I/O primitives
    SDEF("%record-command-usage",      1, scm_record_command_usage);
    SDEF("%update-recent-project!",    1, scm_update_recent_project);
    SDEF("%chdir",                     1, scm_chdir);
    SDEF("%open-recent-project!",      1, scm_open_recent_project);
    SDEF("%show-open-folder-dialog",   0, scm_show_open_folder_dialog);

    // Jump list primitives
    SDEF("%jump-push!",     0, scm_jump_push);
    SDEF("%jump-backward!", 0, scm_jump_backward);
    SDEF("%jump-forward!",  0, scm_jump_forward);

    // Mouse handler primitives
    SDEF("%set-mouse-click-handler!", 1, scm_set_mouse_click_handler);
    SDEF("%set-mouse-drag-handler!",  1, scm_set_mouse_drag_handler);

    // In-buffer search primitives
    SDEF("%search-open!",           0, scm_search_open);
    SDEF("%search-open-backward!",  0, scm_search_open_backward);
    SDEF("%search-next!",       0, scm_search_next);
    SDEF("%search-prev!",       0, scm_search_prev);
    SDEF("search-self-insert",  0, scm_search_self_insert);
    SDEF("%search-backspace!",       0, scm_search_backspace);
    SDEF("%search-forward-char!",         0, scm_search_forward_char);
    SDEF("%search-backward-char!",        0, scm_search_backward_char);
    SDEF("%search-shift-forward-char!",   0, scm_search_shift_forward_char);
    SDEF("%search-shift-backward-char!",  0, scm_search_shift_backward_char);
    SDEF("%search-confirm!",        0, scm_search_confirm);
    SDEF("%search-cancel!",     0, scm_search_cancel);
    SDEF("%search-bar-open?",   0, scm_search_bar_open_p);
    SDEF("%search-toggle-case!", 0, scm_search_toggle_case);
    SDEF("%search-toggle-whole-words!", 0, scm_search_toggle_whole_words);
    SDEF("%search-toggle-regex!", 0, scm_search_toggle_regex);
    SDEF("%search-toggle-match-in-selection!", 0, scm_search_toggle_match_in_selection);
    SDEF("%search-toggle-replace!",       0, scm_search_toggle_replace);
    SDEF("replace-self-insert",           0, scm_replace_self_insert);
    SDEF("%replace-backspace!",           0, scm_replace_backspace);
    SDEF("%replace-forward-char!",        0, scm_replace_forward_char);
    SDEF("%replace-backward-char!",       0, scm_replace_backward_char);
    SDEF("%replace-shift-forward-char!",  0, scm_replace_shift_forward_char);
    SDEF("%replace-shift-backward-char!", 0, scm_replace_shift_backward_char);
    SDEF("%search-replace-next!",         0, scm_search_replace_next);
    SDEF("%search-replace-all!",          0, scm_search_replace_all);

    // Mark / selection primitives
    SDEF("%mark-set-to-point!", 1, scm_mark_set_to_point);
    SDEF("%select-mode-set!", 1, scm_select_mode_set);
    SDEF("%select-mode-get", 0, scm_select_mode_get);
    SDEF("exchange-point-and-mark", 0, scm_swap_point_and_mark);
    SDEF("%point-to-mark!", 1, scm_point_to_named_mark);
    SDEF("%goto-line", 1, scm_goto_line);
    SDEF("%line-count", 0, scm_line_count);
    SDEF("%mark-position", 1, scm_mark_position);
    SDEF("%position-line", 1, scm_position_line);
    SDEF("%line-start-position", 1, scm_line_start_position);
    SDEF("%line-end-position", 1, scm_line_end_position);

    // Resolve scheme resource path
    #ifdef __EMSCRIPTEN__
    #define RESOURCES_PATH "/resources/"
    #else
    char resourcesPath[1024];
    snprintf(resourcesPath, sizeof(resourcesPath), "%sscheme/", basePath);
    #define RESOURCES_PATH resourcesPath
    #endif

    // Add scheme dir to module search path so Chibi finds editor/*.sld
    {
        sexp_gc_var2(dir_str, new_path);
        sexp_gc_preserve2(ctx, dir_str, new_path);
        dir_str = sexp_c_string(ctx, RESOURCES_PATH, -1);
        new_path = sexp_cons(ctx, dir_str, sexp_global(ctx, SEXP_G_MODULE_PATH));
        sexp_global(ctx, SEXP_G_MODULE_PATH) = new_path;
        sexp_gc_release2(ctx);
    }

    // Register (editor primitives) module — makes all C bindings
    // available to .sld libraries via (import (editor primitives))
    sexp meta = sexp_global(ctx, SEXP_G_META_ENV);
    sexp_env_define(ctx, meta, sexp_intern(ctx, "%editor-env", -1), env);

    sexp result;
    result = sexp_eval_string(ctx,
        "(add-module! '(editor primitives) "
        "(make-module "
        "'(quit message message-echo message-clear message-lock message-unlock "
        "make-keymap %set-key! insert-char self-insert delete-char "
        "move-point move-point-by-line next-line prev-line "
        "forward-char backward-char newline insert-tab "
        "delete-backward-char delete-forward-char set-column "
        "line-start line-end skip-whitespace char-at-point "
        "point-get point-set! buffer-length delete-range char-at "
        "last-key-char %set-replace-mode! %tab-next %tab-prev %tab-new! %tab-new-fresh! no-panes? "
        "reset-global-scale increase-global-scale decrease-global-scale "
        "%reset-buffer-scale %increase-buffer-scale %decrease-buffer-scale "
        "%split-vertical %split-horizontal %tab-close %pop-to-buffer "
        "%pane-navigate-up %pane-navigate-down "
        "%pane-navigate-left %pane-navigate-right "
        "%pane-v-split-increase %pane-v-split-decrease "
        "%pane-h-split-increase %pane-h-split-decrease "
        "eval-buffer %ts-tree-string %ts-enable! %ts-disable! %ts-enable-json! clay-debug prefix-arg "
        "%set-keymap-parent! %set-keymap-name! %bind-prefix! "
        "%read-key-binding %set-key-unbound-cb! %set-mode-allows-input! ignore "
        "%buffer-has-minor-mode? "
        "%buffer-file-name %set-buffer-file-name! %buffer-write "
        "%buffer-modified? %set-buffer-modified! %buffer-set-name! "
        "%buffer-insert %buffer-read %buffer-create %tab-set-buffer! %tab-set-preview! %buffer-close! "
        "%register-mode %set-major-mode "
        "%enable-minor-mode %disable-minor-mode "
        "%buffer-major-mode %buffer-minor-modes "
        "%set-local! %get-local %set-palette! "
        "%update-icon-colors! %register-icon! "
        "%register-mode-icon! %register-major-mode-info! %set-cursor-override! %set-role! "
        "%clear-palette! %clear-roles! "
        "%begin-change %end-change %undo %redo "
        "%change-active? %change-set-repeat-info! %change-last-repeat-info "
        "%line-restore %change-current-inserts "
        "%mark-set-to-point! %select-mode-set! %select-mode-get "
        "exchange-point-and-mark %point-to-mark! %goto-line "
        "%line-count %mark-position %position-line "
        "%line-start-position %line-end-position "
        "%clipboard-get %clipboard-set! "
        "%register-set! %register-append! %register-get "
        "%register-set-shape! %register-shape "
        "%register-set-block-width! %register-block-width "
        "%buffer-substring %insert-string "
        "%macro-start! %macro-stop! %macro-play %macro-recording? "
        "%minibuffer-activate %minibuffer-submit "
        "%minibuffer-cancel %minibuffer-active? "
        "%minibuffer-activate-commands %minibuffer-activate-describe-function "
        "%minibuffer-activate-describe-command "
        "%minibuffer-activate-describe-variable %minibuffer-activate-themes "
        "%minibuffer-activate-major-modes! "
        "%minibuffer-select-next %minibuffer-select-prev "
        "%minibuffer-activate-file-picker "
        "%scanner-start! "
        "%which-key-toggle "
        "%record-command-usage %update-recent-project! %chdir %open-recent-project! %show-open-folder-dialog "
        "%jump-push! %jump-backward! %jump-forward! "
        "jump-to-matching-bracket "
        "%set-mouse-click-handler! %set-mouse-drag-handler! "
        "%search-open! %search-open-backward! %search-next! %search-prev! "
        "search-self-insert %search-backspace! %search-forward-char! %search-backward-char! %search-shift-forward-char! %search-shift-backward-char! %search-confirm! %search-cancel! %search-bar-open? %search-toggle-case! %search-toggle-whole-words! %search-toggle-regex! "
        "%search-toggle-match-in-selection! "
        "%search-toggle-replace! replace-self-insert %replace-backspace! %replace-forward-char! %replace-backward-char! %replace-shift-forward-char! %replace-shift-backward-char! %search-replace-next! %search-replace-all! "
        "global-keymap pane-keymap search-keymap replace-keymap eval) "
        "%editor-env '()))",
        -1, meta);
    if (sexp_exceptionp(result)) {
        fprintf(stderr, "ERROR: failed to register (editor primitives)\n");
        sexp_print_exception(ctx, result, sexp_current_error_port(ctx));
    }

    // Cache role symbols for fast lookup
    #define INTERN_ROLE(field, name) do { \
        state->ui.roles.field = sexp_intern(ctx, name, -1); \
        sexp_preserve_object(ctx, state->ui.roles.field); \
    } while(0)

    INTERN_ROLE(ui_bg, "ui.bg");
    INTERN_ROLE(pane_bg, "pane.bg");
    INTERN_ROLE(border_active, "border.active");
    INTERN_ROLE(border_inactive, "border.inactive");
    INTERN_ROLE(border_bell, "border.bell");
    INTERN_ROLE(bar_bg, "bar.bg");
    INTERN_ROLE(line_bg, "line.bg");
    INTERN_ROLE(scrollbar, "scrollbar");
    INTERN_ROLE(scrollbar_hover, "scrollbar.hover");
    INTERN_ROLE(tab_active, "tab.active");
    INTERN_ROLE(tab_hover, "tab.hover");
    INTERN_ROLE(tab_inactive, "tab.inactive");
    INTERN_ROLE(tab_close, "tab.close");
    INTERN_ROLE(text_primary, "text.primary");
    INTERN_ROLE(text_faded, "text.faded");
    INTERN_ROLE(text_key, "text.key");
    INTERN_ROLE(text_command, "text.command");
    INTERN_ROLE(text_prefix, "text.prefix");
    INTERN_ROLE(text_linenum, "text.linenum");
    INTERN_ROLE(diff_added,    "diff.added");
    INTERN_ROLE(diff_modified, "diff.modified");
    INTERN_ROLE(diff_deleted,  "diff.deleted");
    INTERN_ROLE(selection,       "selection");
    INTERN_ROLE(selection_hover, "selection.hover");
    INTERN_ROLE(message_hover,   "message.hover");
    INTERN_ROLE(mode_normal, "mode.normal");
    INTERN_ROLE(mode_insert, "mode.insert");
    INTERN_ROLE(mode_replace, "mode.replace");
    INTERN_ROLE(mode_select, "mode.select");
    INTERN_ROLE(mode_command, "mode.command");
    INTERN_ROLE(mode_pending, "mode.pending");
    INTERN_ROLE(mode_minibuffer, "mode.minibuffer");
    INTERN_ROLE(mode_search,    "mode.search");
    INTERN_ROLE(mode_help, "mode.help");
    INTERN_ROLE(label_normal, "label.normal");
    INTERN_ROLE(label_insert, "label.insert");
    INTERN_ROLE(label_replace, "label.replace");
    INTERN_ROLE(label_select, "label.select");
    INTERN_ROLE(label_command, "label.command");
    INTERN_ROLE(label_pending, "label.pending");
    INTERN_ROLE(label_minibuffer, "label.minibuffer");
    INTERN_ROLE(label_search,    "label.search");
    INTERN_ROLE(label_help, "label.help");
    INTERN_ROLE(cursor_normal, "cursor.normal");
    INTERN_ROLE(cursor_insert, "cursor.insert");
    INTERN_ROLE(cursor_replace, "cursor.replace");
    INTERN_ROLE(cursor_select, "cursor.select");
    INTERN_ROLE(cursor_command, "cursor.command");
    INTERN_ROLE(cursor_pending, "cursor.pending");
    INTERN_ROLE(cursor_minibuffer, "cursor.minibuffer");
    INTERN_ROLE(cursor_search,    "cursor.search");
    INTERN_ROLE(cursor_help, "cursor.help");
    INTERN_ROLE(macro_indicator, "macro.indicator");
    INTERN_ROLE(macro_bg, "macro.bg");
    INTERN_ROLE(hl_keyword,  "hl.keyword");
    INTERN_ROLE(hl_string,   "hl.string");
    INTERN_ROLE(hl_comment,  "hl.comment");
    INTERN_ROLE(hl_number,   "hl.number");
    INTERN_ROLE(hl_constant, "hl.constant");
    INTERN_ROLE(hl_function, "hl.function");
    INTERN_ROLE(hl_builtin,  "hl.builtin");
    INTERN_ROLE(hl_operator, "hl.operator");
    INTERN_ROLE(hl_bracket,          "hl.bracket");
    INTERN_ROLE(hl_property,         "hl.property");
    INTERN_ROLE(hl_bracket_match,    "hl.bracket.match");
    INTERN_ROLE(hl_search,           "hl.search");
    INTERN_ROLE(hl_search_active,    "hl.search.active");
    INTERN_ROLE(search_error,        "search.error");

    #undef INTERN_ROLE

    #define INTERN_SYM(field, name) \
        state->ui.symbols.field = sexp_intern(ctx, name, -1)

    INTERN_SYM(kw_color,   ":color");
    INTERN_SYM(kw_font,    ":font");
    INTERN_SYM(kw_size,    ":size");
    INTERN_SYM(kw_bg,      ":bg");
    INTERN_SYM(buf_normal, "buf-normal");
    INTERN_SYM(buf_bold,   "buf-bold");
    INTERN_SYM(buf_italic, "buf-italic");
    INTERN_SYM(ui_normal,  "ui-normal");
    INTERN_SYM(ui_bold,    "ui-bold");
    INTERN_SYM(ui_italic,  "ui-italic");

    #undef INTERN_SYM

    // Load init.scm — its (import ...) forms trigger library loading
    #define LOAD_SCRIPT(name) do { \
        char path_[1024]; \
        snprintf(path_, sizeof(path_), "%s" name ".scm", RESOURCES_PATH); \
        result = sexp_load(ctx, sexp_c_string(ctx, path_, -1), env); \
        if (sexp_exceptionp(result)) \
            sexp_print_exception(ctx, result, sexp_current_error_port(ctx)); \
    } while(0)

    LOAD_SCRIPT("init");

    #undef LOAD_SCRIPT

    // Load user init.scm from ~/.config/sev/init.scm (Linux/desktop only)
#ifndef __EMSCRIPTEN__
    {
        char user_init[1024];
        const char *xdg = getenv("XDG_CONFIG_HOME");
        if (xdg && xdg[0]) {
            snprintf(user_init, sizeof(user_init), "%s/sev/init.scm", xdg);
        } else {
            const char *home = getenv("HOME");
            if (home && home[0])
                snprintf(user_init, sizeof(user_init), "%s/.config/sev/init.scm", home);
            else
                user_init[0] = '\0';
        }
        if (user_init[0] && access(user_init, F_OK) == 0) {
            result = sexp_load(ctx, sexp_c_string(ctx, user_init, -1), env);
            if (sexp_exceptionp(result))
                sexp_print_exception(ctx, result, sexp_current_error_port(ctx));
            reset_scale(state);
        }
    }
#endif

    // Get call-interactively procedure (imported from (editor command) by init.scm)
    state->chibi.call_interactively = sexp_env_ref(
        ctx, env,
        sexp_intern(ctx, "call-interactively", -1),
        SEXP_FALSE
    );
    if (state->chibi.call_interactively == SEXP_FALSE) {
        fprintf(stderr, "ERROR: call-interactively not found\n");
    }
    sexp_preserve_object(ctx, state->chibi.call_interactively);
}
