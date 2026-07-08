;;; init.scm - Editor initialization and keybindings

;; ── Global settings ───────────────────────────────────────────────────────────

(define cursor-blink #t)

(import (editor command) (editor mode) (editor icon)
        (editor built-in) (editor vim) (editor theme)
        (editor minibuffer) (editor which-key))

;; ── Global keymap (base) ─────────────────────────────────────────────────────
;; Bindings that are always available regardless of whether any pane is open.

(set-key! global-keymap "ctrl-q" 'quit)
(set-key! global-keymap "ctrl-e" 'eval-expression)
(set-key! global-keymap "ctrl-shift-p" 'command-palette)
(set-key! global-keymap "ctrl-p" 'file-picker)
(set-key! global-keymap "ctrl-shift-h" 'search-toggle-replace)
(set-key! global-keymap "alt-0" 'reset-global-scale)
(set-key! global-keymap "alt-=" 'increase-global-scale)
(set-key! global-keymap "alt--" 'decrease-global-scale)

;; SPC prefix for global (base) — open-file, buffer-new, help
(define base-spc-map (make-keymap))
(%set-keymap-name! base-spc-map "leader")
(bind-prefix! global-keymap "space" base-spc-map)

;; SPC b — buffer operations available on welcome screen
(define base-buffer-map (make-keymap))
(%set-keymap-name! base-buffer-map "buffer")
(set-key! base-buffer-map "n" 'buffer-new)
(bind-prefix! base-spc-map "b" base-buffer-map)

;; SPC f — file operations (always available: open creates a pane if none)
(define base-f-map (make-keymap))
(%set-keymap-name! base-f-map "file/find")
(set-key! base-f-map "o" 'file-picker)
(bind-prefix! base-spc-map "f" base-f-map)

;; SPC h — help (always available)
(define help-map (make-keymap))
(%set-keymap-name! help-map "help")
(set-key! help-map "f" 'describe-function)
(set-key! help-map "k" 'describe-key)
(set-key! help-map "c" 'describe-command)
(set-key! help-map "v" 'describe-variable)
(set-key! help-map "w" 'which-key-toggle)
(bind-prefix! base-spc-map "h" help-map)

;; SPC p — project operations (always available)
(define project-map (make-keymap))
(%set-keymap-name! project-map "project")
(set-key! project-map "o" 'open-project)
(bind-prefix! base-spc-map "p" project-map)

;; SPC t — tab operations
(define base-t-map (make-keymap))
(%set-keymap-name! base-t-map "tabs/theme")
(set-key! base-t-map "n" 'tab-new)
(set-key! base-t-map "h" 'theme-picker)
(bind-prefix! base-spc-map "t" base-t-map)

;; ── Pane keymap ───────────────────────────────────────────────────────────────
;; Bindings that only make sense when at least one pane is open.
;; The C dispatch layer inserts this into the lookup chain only when
;; pane_get_root() != NULL. Its parent is global-keymap so base bindings
;; remain reachable through the parent chain.

(%set-keymap-parent! pane-keymap global-keymap)
(%set-keymap-name!   pane-keymap "pane")

;; Mode / escape
(set-key! pane-keymap "escape"     'vim-normal)
(set-key! pane-keymap "ctrl-["     'vim-normal)

;; Tab navigation
(set-key! pane-keymap "ctrl-tab"       'tab-next)
(set-key! pane-keymap "ctrl-shift-tab" 'tab-prev)

;; Pane management
(set-key! pane-keymap "ctrl-w"     'tab-close)

;; File save
(set-key! pane-keymap "ctrl-s"     'save-buffer)
(set-key! pane-keymap "ctrl-shift-s"   'save-buffer-as)

;; Buffer-local text scale
(set-key! pane-keymap "ctrl-0"     'reset-buffer-scale)
(set-key! pane-keymap "ctrl-="     'increase-buffer-scale)
(set-key! pane-keymap "ctrl--"     'decrease-buffer-scale)

;; Vim motions (cursor movement)
(set-key! pane-keymap "h"       'vim-motion-h)
(set-key! pane-keymap "j"       'vim-motion-j)
(set-key! pane-keymap "k"       'vim-motion-k)
(set-key! pane-keymap "l"       'vim-motion-l)
(set-key! pane-keymap "left"    'vim-motion-h)
(set-key! pane-keymap "down"    'vim-motion-j)
(set-key! pane-keymap "up"      'vim-motion-k)
(set-key! pane-keymap "right"   'vim-motion-l)

;; Pane navigation
(set-key! pane-keymap "ctrl-h"     'pane-navigate-left)
(set-key! pane-keymap "ctrl-j"     'pane-navigate-down)
(set-key! pane-keymap "ctrl-k"     'pane-navigate-up)
(set-key! pane-keymap "ctrl-l"     'pane-navigate-right)
(set-key! pane-keymap "ctrl-left"  'pane-navigate-left)
(set-key! pane-keymap "ctrl-down"  'pane-navigate-down)
(set-key! pane-keymap "ctrl-up"    'pane-navigate-up)
(set-key! pane-keymap "ctrl-right" 'pane-navigate-right)

;; Pane resize
(set-key! pane-keymap "ctrl-shift-left"  'pane-v-split-decrease)
(set-key! pane-keymap "ctrl-shift-down"  'pane-h-split-increase)
(set-key! pane-keymap "ctrl-shift-up"    'pane-h-split-decrease)
(set-key! pane-keymap "ctrl-shift-right" 'pane-v-split-increase)

;; SPC prefix for pane — extends base-spc-map via parent chain
(define pane-spc-map (make-keymap))
(%set-keymap-name!   pane-spc-map "leader")
(%set-keymap-parent! pane-spc-map base-spc-map)
(bind-prefix! pane-keymap "space" pane-spc-map)

;; SPC b — full buffer map (extends base-buffer-map)
(define pane-buffer-map (make-keymap))
(%set-keymap-name!   pane-buffer-map "buffer")
(%set-keymap-parent! pane-buffer-map base-buffer-map)
(set-key! pane-buffer-map "r" 'buffer-rename)
(set-key! pane-buffer-map "c" 'buffer-close)
(set-key! pane-buffer-map "s" 'switch-to-buffer)
(bind-prefix! pane-spc-map "b" pane-buffer-map)

;; SPC s — split
(define split-map (make-keymap))
(%set-keymap-name! split-map "split")
(set-key! split-map "h" 'split-horizontal)
(set-key! split-map "v" 'split-vertical)
(bind-prefix! pane-spc-map "s" split-map)

;; SPC l — line numbers
(define base-l-map (make-keymap))
(%set-keymap-name! base-l-map "line/language")
(set-key! base-l-map "n" 'toggle-line-numbers)
(set-key! base-l-map "r" 'toggle-relative-line-numbers)
(set-key! base-l-map "v" 'toggle-visual-line-numbers)
(set-key! base-l-map "s" 'set-buffer-mode)
(bind-prefix! pane-spc-map "l" base-l-map)

;; ── Mouse handlers ────────────────────────────────────────────────────────────

;; Click: exit any visual select mode, then move point to clicked position.
(%set-mouse-click-handler!
  (lambda (button buf-pos clicks)
    (when (= button 1)
      (when (%buffer-has-minor-mode? 'vim-select-mode)
        (vim-normal))
      (point-set! buf-pos))))

;; Drag: on first motion enter visual-char mode anchored at the click position
;; (the click handler already moved point there), then track cursor.
;; In vim buffers this activates vim-select-mode; in others point just follows.
(%set-mouse-drag-handler!
  (lambda (current-pos start-pos)
    (when (and (not (%buffer-has-minor-mode? 'vim-select-mode))
               (or (%buffer-has-minor-mode? 'vim-normal-mode)
                   (%buffer-has-minor-mode? 'vim-insert-mode)
                   (%buffer-has-minor-mode? 'vim-replace-mode)))
      ;; Point is at start-pos (set by click handler); vim-select anchors
      ;; mark < there, then enables vim-select-mode with SELECT_REGULAR.
      (vim-select))
    (point-set! current-pos)))

;; ── Welcome keymap ────────────────────────────────────────────────────────────
;; Inherits global-keymap (base only). No ignore shadows needed: pane commands
;; are absent from global-keymap and unreachable here.

(define welcome-map (make-keymap))
(%set-keymap-parent! welcome-map global-keymap)
(%set-keymap-name!   welcome-map "welcome")
(%set-welcome-keymap! welcome-map)

(set-key! welcome-map ":" 'command-palette)

(defcommand (open-recent-1) "project: open most recent" (interactive) (%open-recent-project! 1))
(defcommand (open-recent-2) "project: open 2nd most recent" (interactive) (%open-recent-project! 2))
(defcommand (open-recent-3) "project: open 3rd most recent" (interactive) (%open-recent-project! 3))
(defcommand (open-recent-4) "project: open 4th most recent" (interactive) (%open-recent-project! 4))
(defcommand (open-recent-5) "project: open 5th most recent" (interactive) (%open-recent-project! 5))

(set-key! welcome-map "1" 'open-recent-1)
(set-key! welcome-map "2" 'open-recent-2)
(set-key! welcome-map "3" 'open-recent-3)
(set-key! welcome-map "4" 'open-recent-4)
(set-key! welcome-map "5" 'open-recent-5)

(reset-global-scale)
(message-clear)
