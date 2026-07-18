(define-library (editor built-in)
  (import (except (scheme base) newline)
          (scheme file)
          (scheme read)
          (scheme write)
          (scheme eval)
          (scheme repl)
          (editor primitives) (editor command) (editor minibuffer) (editor mode))
  (export
    quit self-insert eval-buffer
    next-line prev-line forward-char backward-char
    line-start line-end skip-whitespace newline insert-tab
    delete-backward-char delete-forward-char delete-char insert-char
    tab-next tab-prev
    reset-global-scale increase-global-scale decrease-global-scale
    reset-buffer-scale increase-buffer-scale decrease-buffer-scale
    split-horizontal split-vertical tab-close
    pane-navigate-up pane-navigate-down pane-navigate-left pane-navigate-right
    pane-h-split-increase pane-h-split-decrease
    pane-v-split-increase pane-v-split-decrease
    clay-debug exchange-point-and-mark
    line-start-skip-whitespace join-line
    save-buffer save-buffer-as open-file open-project read-file
    buffer-new buffer-rename switch-to-buffer buffer-close tab-new
    eval-expression
    file-picker file-picker-open!
    search-backspace search-confirm search-cancel
    search-forward-char search-backward-char
    search-shift-forward-char search-shift-backward-char
    search-toggle-case
    search-toggle-whole-words
    search-toggle-regex
    search-toggle-match-in-selection
    search-toggle-replace
    replace-backspace
    replace-forward-char replace-backward-char
    replace-shift-forward-char replace-shift-backward-char
    search-replace-next search-replace-all)
  (include "built-in.scm"))
