#pragma once

#include <chibi/sexp.h>

// --- text/buffer_edit.c ---
sexp scm_insert_char(sexp ctx, sexp self, sexp n, sexp ch);
sexp scm_self_insert(sexp ctx, sexp self, sexp n);
sexp scm_delete_char(sexp ctx, sexp self, sexp n, sexp count);
sexp scm_delete_backward_char(sexp ctx, sexp self, sexp n);
sexp scm_delete_forward_char(sexp ctx, sexp self, sexp n);
sexp scm_newline(sexp ctx, sexp self, sexp n);
sexp scm_insert_tab(sexp ctx, sexp self, sexp n);
sexp scm_delete_range(sexp ctx, sexp self, sexp n, sexp sstart, sexp send);
sexp scm_set_replace_mode(sexp ctx, sexp self, sexp n, sexp val);

// --- text/point.c ---
sexp scm_point_move(sexp ctx, sexp self, sexp n, sexp count);
sexp scm_point_move_by_line(sexp ctx, sexp self, sexp n, sexp count);
sexp scm_next_line(sexp ctx, sexp self, sexp n);
sexp scm_prev_line(sexp ctx, sexp self, sexp n);
sexp scm_point_to_line_start(sexp ctx, sexp self, sexp n);
sexp scm_point_to_line_end(sexp ctx, sexp self, sexp n);
sexp scm_forward_char(sexp ctx, sexp self, sexp n);
sexp scm_backward_char(sexp ctx, sexp self, sexp n);
sexp scm_skip_whitespace(sexp ctx, sexp self, sexp n);
sexp scm_set_column(sexp ctx, sexp self, sexp n, sexp column);
sexp scm_char_at_point(sexp ctx, sexp self, sexp n);
sexp scm_point_get(sexp ctx, sexp self, sexp n);
sexp scm_point_set_to(sexp ctx, sexp self, sexp n, sexp pos);
sexp scm_buffer_length(sexp ctx, sexp self, sexp n);
sexp scm_char_at(sexp ctx, sexp self, sexp n, sexp pos);
sexp scm_last_key_char(sexp ctx, sexp self, sexp n);
sexp scm_goto_line(sexp ctx, sexp self, sexp n, sexp line_num);
sexp scm_line_count(sexp ctx, sexp self, sexp n);
sexp scm_jump_to_matching_bracket(sexp ctx, sexp self, sexp n);

// --- display/pane.c ---
sexp scm_pane_navigate_up(sexp ctx, sexp self, sexp n);
sexp scm_pane_navigate_down(sexp ctx, sexp self, sexp n);
sexp scm_pane_navigate_left(sexp ctx, sexp self, sexp n);
sexp scm_pane_navigate_right(sexp ctx, sexp self, sexp n);
sexp scm_pane_v_split_increase(sexp ctx, sexp self, sexp n);
sexp scm_pane_v_split_decrease(sexp ctx, sexp self, sexp n);
sexp scm_pane_h_split_increase(sexp ctx, sexp self, sexp n);
sexp scm_pane_h_split_decrease(sexp ctx, sexp self, sexp n);
sexp scm_split_vertical(sexp ctx, sexp self, sexp n);
sexp scm_split_horizontal(sexp ctx, sexp self, sexp n);
sexp scm_jump_push(sexp ctx, sexp self, sexp n);
sexp scm_jump_backward(sexp ctx, sexp self, sexp n);
sexp scm_jump_forward(sexp ctx, sexp self, sexp n);
sexp scm_set_mouse_click_handler(sexp ctx, sexp self, sexp n, sexp cb);
sexp scm_set_mouse_drag_handler(sexp ctx, sexp self, sexp n, sexp cb);
sexp scm_search_open(sexp ctx, sexp self, sexp n);
sexp scm_search_open_backward(sexp ctx, sexp self, sexp n);
sexp scm_search_next(sexp ctx, sexp self, sexp n);
sexp scm_search_prev(sexp ctx, sexp self, sexp n);
sexp scm_search_self_insert(sexp ctx, sexp self, sexp n);
sexp scm_search_backspace(sexp ctx, sexp self, sexp n);
sexp scm_search_forward_char(sexp ctx, sexp self, sexp n);
sexp scm_search_backward_char(sexp ctx, sexp self, sexp n);
sexp scm_search_shift_forward_char(sexp ctx, sexp self, sexp n);
sexp scm_search_shift_backward_char(sexp ctx, sexp self, sexp n);
sexp scm_search_confirm(sexp ctx, sexp self, sexp n);
sexp scm_search_cancel(sexp ctx, sexp self, sexp n);
sexp scm_search_bar_open_p(sexp ctx, sexp self, sexp n);
sexp scm_search_toggle_case(sexp ctx, sexp self, sexp n);
sexp scm_search_toggle_whole_words(sexp ctx, sexp self, sexp n);
sexp scm_search_toggle_replace(sexp ctx, sexp self, sexp n);
sexp scm_replace_self_insert(sexp ctx, sexp self, sexp n);
sexp scm_replace_backspace(sexp ctx, sexp self, sexp n);
sexp scm_replace_forward_char(sexp ctx, sexp self, sexp n);
sexp scm_replace_backward_char(sexp ctx, sexp self, sexp n);
sexp scm_replace_shift_forward_char(sexp ctx, sexp self, sexp n);
sexp scm_replace_shift_backward_char(sexp ctx, sexp self, sexp n);
sexp scm_search_replace_next(sexp ctx, sexp self, sexp n);
sexp scm_search_replace_all(sexp ctx, sexp self, sexp n);

// --- display/tab.c ---
sexp scm_tab_close(sexp ctx, sexp self, sexp n);
sexp scm_tab_next(sexp ctx, sexp self, sexp n);
sexp scm_tab_prev(sexp ctx, sexp self, sexp n);
sexp scm_tab_new(sexp ctx, sexp self, sexp n, sexp sbuf_name);
sexp scm_no_panes_p(sexp ctx, sexp self, sexp n);
sexp scm_buffer_close(sexp ctx, sexp self, sexp n, sexp sname);

// --- display/scale.c ---
sexp scm_reset_global_scale(sexp ctx, sexp self, sexp n);
sexp scm_increase_global_scale(sexp ctx, sexp self, sexp n);
sexp scm_decrease_global_scale(sexp ctx, sexp self, sexp n);
sexp scm_reset_buffer_scale(sexp ctx, sexp self, sexp n);
sexp scm_increase_buffer_scale(sexp ctx, sexp self, sexp n);
sexp scm_decrease_buffer_scale(sexp ctx, sexp self, sexp n);

// --- command/mode.c ---
sexp scm_register_mode(sexp ctx, sexp self, sexp n,
                       sexp sname, sexp stype, sexp skeymap);
sexp scm_set_major_mode(sexp ctx, sexp self, sexp n, sexp sname);
sexp scm_enable_minor_mode(sexp ctx, sexp self, sexp n, sexp sname);
sexp scm_disable_minor_mode(sexp ctx, sexp self, sexp n, sexp sname);
sexp scm_buffer_major_mode(sexp ctx, sexp self, sexp n);
sexp scm_buffer_minor_modes(sexp ctx, sexp self, sexp n);
sexp scm_buffer_has_minor_mode(sexp ctx, sexp self, sexp n, sexp sname);
sexp scm_set_mode_allows_input(sexp ctx, sexp self, sexp n,
                                sexp sname, sexp sval);

// --- command/keymap.c ---
sexp scm_make_keymap(sexp ctx, sexp self, sexp n);
sexp scm_set_key(sexp ctx, sexp self, sexp n,
                 sexp skeymap, sexp skeystr, sexp scommand);
sexp scm_set_keymap_parent(sexp ctx, sexp self, sexp n,
                           sexp skeymap, sexp sparent);
sexp scm_read_key_binding(sexp ctx, sexp self, sexp n, sexp callback);
sexp scm_set_key_unbound_cb(sexp ctx, sexp self, sexp n, sexp sym);
sexp scm_set_keymap_name(sexp ctx, sexp self, sexp n,
                         sexp skeymap, sexp sname);
sexp scm_bind_prefix(sexp ctx, sexp self, sexp n,
                     sexp sparent, sexp skeystr, sexp schild);

// --- text/var.c ---
sexp scm_set_local(sexp ctx, sexp self, sexp n, sexp key, sexp sval);
sexp scm_get_local(sexp ctx, sexp self, sexp n, sexp key, sexp sdefault);

// --- display/icon.c ---
sexp scm_register_icon(sexp ctx, sexp self, sexp n,
                       sexp sname, sexp sfilename, sexp scolor_role);
sexp scm_update_icon_colors(sexp ctx, sexp self, sexp n);

// --- display/mode_icon.c ---
sexp scm_register_mode_icon(sexp ctx, sexp self, sexp n,
                            sexp sname, sexp sfilename,
                            sexp srole_bg, sexp srole_label,
                            sexp srole_cursor, sexp scursor_type);

// --- display/major_mode_info.c ---
sexp scm_register_major_mode_info(sexp ctx, sexp self, sexp n,
                                  sexp sname, sexp sdisplay, sexp sicon);

// --- display/theme.c ---
sexp scm_set_cursor_override(sexp ctx, sexp self, sexp n, sexp stype);

// --- text/mark.c ---
sexp scm_mark_set_to_point(sexp ctx, sexp self, sexp n, sexp mark_char);
sexp scm_mark_position(sexp ctx, sexp self, sexp n, sexp mark_char);
sexp scm_select_mode_set(sexp ctx, sexp self, sexp n, sexp mode_int);
sexp scm_select_mode_get(sexp ctx, sexp self, sexp n);
sexp scm_swap_point_and_mark(sexp ctx, sexp self, sexp n);
sexp scm_point_to_named_mark(sexp ctx, sexp self, sexp n, sexp mark_char);

// --- text/register.c ---
sexp scm_register_set(sexp ctx, sexp self, sexp n, sexp sname, sexp stext);
sexp scm_register_append(sexp ctx, sexp self, sexp n, sexp sname, sexp stext);
sexp scm_register_get(sexp ctx, sexp self, sexp n, sexp sname);
sexp scm_register_set_shape(sexp ctx, sexp self, sexp n, sexp sname, sexp sshape);
sexp scm_register_get_shape(sexp ctx, sexp self, sexp n, sexp sname);
sexp scm_register_set_block_width(sexp ctx, sexp self, sexp n, sexp sname, sexp swidth);
sexp scm_register_get_block_width(sexp ctx, sexp self, sexp n, sexp sname);

// --- text/buffer_edit.c (substring / insert-string) ---
sexp scm_buffer_substring(sexp ctx, sexp self, sexp n, sexp sstart, sexp send);
sexp scm_insert_string(sexp ctx, sexp self, sexp n, sexp stext);

// --- text/point.c (line queries) ---
sexp scm_position_line(sexp ctx, sexp self, sexp n, sexp spos);
sexp scm_line_start_position(sexp ctx, sexp self, sexp n, sexp sline);
sexp scm_line_end_position(sexp ctx, sexp self, sexp n, sexp sline);

// --- text/buffer_edit.c (change/undo) ---
sexp scm_begin_change(sexp ctx, sexp self, sexp n);
sexp scm_end_change(sexp ctx, sexp self, sexp n);
sexp scm_undo(sexp ctx, sexp self, sexp n);
sexp scm_redo(sexp ctx, sexp self, sexp n);
sexp scm_change_active(sexp ctx, sexp self, sexp n);
sexp scm_change_set_repeat_info(sexp ctx, sexp self, sexp n, sexp info);
sexp scm_change_last_repeat_info(sexp ctx, sexp self, sexp n);
sexp scm_line_restore(sexp ctx, sexp self, sexp n);
sexp scm_change_current_inserts(sexp ctx, sexp self, sexp n);

// --- command/macro.c ---
sexp scm_macro_start(sexp ctx, sexp self, sexp n, sexp sname);
sexp scm_macro_stop(sexp ctx, sexp self, sexp n);
sexp scm_macro_play(sexp ctx, sexp self, sexp n, sexp sname);
sexp scm_macro_is_recording(sexp ctx, sexp self, sexp n);

// --- command/message.c ---
sexp scm_message(sexp ctx, sexp self, sexp n, sexp sarg);
sexp scm_message_echo_scm(sexp ctx, sexp self, sexp n, sexp sarg);
sexp scm_message_clear(sexp ctx, sexp self, sexp n);
sexp scm_message_lock(sexp ctx, sexp self, sexp n);
sexp scm_message_unlock(sexp ctx, sexp self, sexp n);

// --- command/minibuf.c ---
sexp scm_minibuffer_activate(sexp ctx, sexp self, sexp n, sexp sprompt, sexp on_submit, sexp on_cancel);
sexp scm_minibuffer_activate_major_modes(sexp ctx, sexp self, sexp n);
sexp scm_minibuffer_submit(sexp ctx, sexp self, sexp n);
sexp scm_minibuffer_cancel(sexp ctx, sexp self, sexp n);
sexp scm_minibuffer_activep(sexp ctx, sexp self, sexp n);
sexp scm_minibuffer_activate_commands(sexp ctx, sexp self, sexp n);
sexp scm_minibuffer_activate_describe_function(sexp ctx, sexp self, sexp n);
sexp scm_minibuffer_activate_describe_command(sexp ctx, sexp self, sexp n);
sexp scm_minibuffer_activate_describe_variable(sexp ctx, sexp self, sexp n);
sexp scm_minibuffer_activate_themes(sexp ctx, sexp self, sexp n);
sexp scm_minibuffer_select_next(sexp ctx, sexp self, sexp n);
sexp scm_minibuffer_select_prev(sexp ctx, sexp self, sexp n);

// --- display/which_key.c ---
sexp scm_which_key_toggle(sexp ctx, sexp self, sexp n);

// --- state_io.c ---
sexp scm_record_command_usage(sexp ctx, sexp self, sexp n, sexp sym);
sexp scm_update_recent_project(sexp ctx, sexp self, sexp n, sexp spath);
sexp scm_chdir(sexp ctx, sexp self, sexp n, sexp spath);
sexp scm_open_recent_project(sexp ctx, sexp self, sexp n, sexp sidx);

// --- text/treesitter.c ---
sexp scm_ts_tree_string(sexp ctx, sexp self, sexp n);
sexp scm_ts_enable(sexp ctx, sexp self, sexp n);
sexp scm_ts_disable(sexp ctx, sexp self, sexp n);
sexp scm_ts_enable_json(sexp ctx, sexp self, sexp n);
