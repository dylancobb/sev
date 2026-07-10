;;; core.scm - Keymaps, modes, state machine, and core dispatch

;; Keymaps
(define normal-map (make-keymap))
(define insert-map (make-keymap))
(define select-map (make-keymap))
(define pending-map (make-keymap))

;; Set parent pointers so pane commands are found via parent chain traversal.
;; pane-keymap's own parent is global-keymap, so the full chain is:
;; mode-map → pane-keymap → global-keymap
(set-keymap-parent! normal-map  pane-keymap)
(set-keymap-parent! insert-map  pane-keymap)
(set-keymap-parent! select-map  pane-keymap)
(set-keymap-parent! pending-map pane-keymap)

;; Normal mode bindings
(set-key! normal-map "0" 'vim-zero)
(set-key! normal-map "$" 'vim-motion-$)
(set-key! normal-map "^" 'vim-motion-^)
(set-key! normal-map "w" 'vim-motion-w)
(set-key! normal-map "b" 'vim-motion-b)
(set-key! normal-map "e" 'vim-motion-e)
(set-key! normal-map "W" 'vim-motion-W)
(set-key! normal-map "B" 'vim-motion-B)
(set-key! normal-map "E" 'vim-motion-E)
(set-key! normal-map "%" 'vim-motion-%)
(set-key! normal-map "i" 'vim-insert)
(set-key! normal-map "I" 'vim-I)
(set-key! normal-map "R" 'vim-replace)
(set-key! normal-map "o" 'open-line-below)
(set-key! normal-map "O" 'open-line-above)
(set-key! normal-map "a" 'append-char)
(set-key! normal-map "A" 'vim-A)
(set-key! normal-map "s" 'substitute-char)
(set-key! normal-map "S" 'vim-S)
(set-key! normal-map "D" 'vim-D)
(set-key! normal-map "C" 'vim-C)
(set-key! normal-map "x" 'vim-x)
(set-key! normal-map "X" 'vim-X)
(set-key! normal-map "d" 'vim-d)
(set-key! normal-map "c" 'vim-c)
(set-key! normal-map "." 'vim-repeat)
(set-key! normal-map "u" 'vim-undo)
(set-key! normal-map "ctrl-r" 'vim-redo)
(set-key! normal-map "U" 'vim-line-restore)
(set-key! normal-map "1" 'vim-digit-argument)
(set-key! normal-map "2" 'vim-digit-argument)
(set-key! normal-map "3" 'vim-digit-argument)
(set-key! normal-map "4" 'vim-digit-argument)
(set-key! normal-map "5" 'vim-digit-argument)
(set-key! normal-map "6" 'vim-digit-argument)
(set-key! normal-map "7" 'vim-digit-argument)
(set-key! normal-map "8" 'vim-digit-argument)
(set-key! normal-map "9" 'vim-digit-argument)
(set-key! normal-map "J" 'join-line)
(set-key! normal-map "v" 'vim-select)
(set-key! normal-map "V" 'vim-select-line)
(set-key! normal-map "ctrl-v" 'vim-select-rectangle)
(set-key! normal-map "g g" 'vim-motion-gg)
(set-key! normal-map "G" 'vim-motion-G)
(set-key! normal-map "ctrl-o" 'vim-jump-backward)
(set-key! normal-map "ctrl-i" 'vim-jump-forward)
(set-key! normal-map "/" 'vim-search-open)
(set-key! normal-map "?" 'vim-search-open-backward)
(set-key! normal-map "n" 'vim-search-next)
(set-key! normal-map "N" 'vim-search-prev)
(set-key! normal-map ":" 'command-palette)

;; Search commands
(defcommand (vim-search-open)
  "search: open search bar\nOpen the in-buffer search bar."
  (%search-open!))

(defcommand (vim-search-open-backward)
  "search: open backward search bar\nOpen the in-buffer search bar, searching backwards."
  (%search-open-backward!))

(defcommand (vim-search-next)
  "search: next match\nJump to the next search match."
  (%search-next!))

(defcommand (vim-search-prev)
  "search: previous match\nJump to the previous search match."
  (%search-prev!))

;; Search bar keybindings
(set-key! search-keymap "backspace" 'search-backspace)
(set-key! search-keymap "return"    'search-confirm)
(set-key! search-keymap "escape"    'search-cancel)

(set-key! search-keymap "left"        'search-backward-char)
(set-key! search-keymap "right"       'search-forward-char)
(set-key! search-keymap "shift-left"  'search-shift-backward-char)
(set-key! search-keymap "shift-right" 'search-shift-forward-char)
(set-key! search-keymap "ctrl-alt-c"  'search-toggle-case)
(set-key! search-keymap "ctrl-alt-w"  'search-toggle-whole-words)
(set-key! search-keymap "ctrl-alt-l"  'search-toggle-match-in-selection)

(define-minor-mode 'search-mode search-keymap #t)
(register-mode-icon/full 'search-mode "icon-normal.svg"
                         'mode.search 'label.search 'cursor.search 'thin)

;; Replace bar keybindings — usable from either query or replace focus
(set-key! search-keymap  "ctrl-shift-h" 'search-toggle-replace)
(set-key! replace-keymap "ctrl-shift-h" 'search-toggle-replace)

(set-key! replace-keymap "backspace"    'replace-backspace)
(set-key! replace-keymap "escape"       'search-cancel)
(set-key! replace-keymap "left"         'replace-backward-char)
(set-key! replace-keymap "right"        'replace-forward-char)
(set-key! replace-keymap "shift-left"   'replace-shift-backward-char)
(set-key! replace-keymap "shift-right"  'replace-shift-forward-char)
(set-key! replace-keymap "return"       'search-replace-next)
(set-key! replace-keymap "ctrl-return"  'search-replace-all)

(define-minor-mode 'replace-mode replace-keymap #t)
(register-mode-icon/full 'replace-mode "icon-normal.svg"
                         'mode.search 'label.search 'cursor.search 'thin)

;; Insert mode bindings
(set-key! insert-map "h" 'self-insert)
(set-key! insert-map "j" 'self-insert)
(set-key! insert-map "k" 'self-insert)
(set-key! insert-map "l" 'self-insert)
(set-key! insert-map "space"     'self-insert)
(set-key! insert-map "alt-h"    'vim-motion-h)
(set-key! insert-map "alt-j"    'vim-motion-j)
(set-key! insert-map "alt-k"    'vim-motion-k)
(set-key! insert-map "alt-l"    'vim-motion-l)
(set-key! insert-map "return"   'newline)
(set-key! insert-map "tab"      'insert-tab)
(set-key! insert-map "backspace" 'delete-backward-char)
(set-key! insert-map "ctrl-h"   'delete-backward-char)
(set-key! insert-map "delete"   'delete-forward-char)
(set-key! insert-map "ctrl-v"   'vim-insert-paste-clipboard)

;; Select (visual) mode bindings
(set-key! select-map ":" 'command-palette)
(set-key! select-map "v" 'vim-select)
(set-key! select-map "V" 'vim-select-line)
(set-key! select-map "ctrl-v" 'vim-select-rectangle)
(set-key! select-map "0" 'vim-zero)
(set-key! select-map "$" 'vim-visual-dollar)
(set-key! select-map "^" 'line-start-skip-whitespace)
(set-key! select-map "o" 'exchange-point-and-mark)
(set-key! select-map "1" 'vim-digit-argument)
(set-key! select-map "2" 'vim-digit-argument)
(set-key! select-map "3" 'vim-digit-argument)
(set-key! select-map "4" 'vim-digit-argument)
(set-key! select-map "5" 'vim-digit-argument)
(set-key! select-map "6" 'vim-digit-argument)
(set-key! select-map "7" 'vim-digit-argument)
(set-key! select-map "8" 'vim-digit-argument)
(set-key! select-map "9" 'vim-digit-argument)

;; Operator-pending mode: only motions, counts, cancel, and doubled ops
(set-key! pending-map "w" 'vim-motion-w)
(set-key! pending-map "b" 'vim-motion-b)
(set-key! pending-map "e" 'vim-motion-e)
(set-key! pending-map "W" 'vim-motion-W)
(set-key! pending-map "B" 'vim-motion-B)
(set-key! pending-map "E" 'vim-motion-E)
(set-key! pending-map "%" 'vim-motion-%)
(set-key! pending-map "$" 'vim-motion-$)
(set-key! pending-map "^" 'vim-motion-^)
(set-key! pending-map "0" 'vim-zero)
(set-key! pending-map "1" 'vim-digit-argument)
(set-key! pending-map "2" 'vim-digit-argument)
(set-key! pending-map "3" 'vim-digit-argument)
(set-key! pending-map "4" 'vim-digit-argument)
(set-key! pending-map "5" 'vim-digit-argument)
(set-key! pending-map "6" 'vim-digit-argument)
(set-key! pending-map "7" 'vim-digit-argument)
(set-key! pending-map "8" 'vim-digit-argument)
(set-key! pending-map "9" 'vim-digit-argument)
(set-key! pending-map "g g" 'vim-motion-gg)
(set-key! pending-map "G" 'vim-motion-G)
(set-key! pending-map "d" 'vim-d)
(set-key! pending-map "c" 'vim-c)

;; Register selection state
(define current-vim-register #\")

(defcommand (vim-use-register)
  "vim: paste from register\nSelect register for next op/paste."
  (set! current-vim-register (last-key-char)))

;; Named prefix keymaps for dispatch commands — must come before the per-char loops.
(let ((reg-km (make-keymap)))
  (%set-keymap-name! reg-km "use-register")
  (bind-prefix! normal-map "\"" reg-km)
  (bind-prefix! select-map "\"" reg-km))
(set-command-display-binding! 'vim-use-register "\" <reg>")

;; Bind " + (a-z, A-Z, ") sequences in normal-map and select-map
(for-each
  (lambda (ch)
    (let ((seq (string-append "\"" " " (string ch))))
      (set-key! normal-map seq 'vim-use-register)
      (set-key! select-map seq 'vim-use-register)))
  (append
    (string->list "abcdefghijklmnopqrstuvwxyz")
    (string->list "ABCDEFGHIJKLMNOPQRSTUVWXYZ")
    (list #\")))

;; Special registers: "+ (clipboard) and "_ (black hole)
(set-key! normal-map "\" +" 'vim-use-register)
(set-key! select-map "\" +" 'vim-use-register)
(set-key! normal-map "\" _" 'vim-use-register)
(set-key! select-map "\" _" 'vim-use-register)

;; Named prefix keymaps for mark dispatch.
(let ((m-km (make-keymap)))
  (%set-keymap-name! m-km "set-mark")
  (bind-prefix! normal-map "m" m-km))
(let ((gl-km (make-keymap)))
  (%set-keymap-name! gl-km "goto-mark")
  (bind-prefix! normal-map "'" gl-km)
  (bind-prefix! pending-map "'" gl-km))
(let ((ge-km (make-keymap)))
  (%set-keymap-name! ge-km "goto-mark-exact")
  (bind-prefix! normal-map "`" ge-km)
  (bind-prefix! pending-map "`" ge-km))
(set-command-display-binding! 'vim-set-mark        "m <mark>")
(set-command-display-binding! 'vim-goto-mark-line  "' <mark>")
(set-command-display-binding! 'vim-goto-mark-exact "` <mark>")

;; m / ' / ` — bind all 26 letters programmatically
(do ((i 0 (+ i 1)))
    ((= i 26))
  (let ((ch (string (integer->char (+ (char->integer #\a) i)))))
    (set-key! normal-map  (string-append "m " ch) 'vim-set-mark)
    (set-key! normal-map  (string-append "' " ch) 'vim-goto-mark-line)
    (set-key! normal-map  (string-append "` " ch) 'vim-goto-mark-exact)
    (set-key! pending-map (string-append "' " ch) 'vim-goto-mark-line)
    (set-key! pending-map (string-append "` " ch) 'vim-goto-mark-exact)))

;; Named prefix keymaps for character-seek dispatch.
(let ((f-km (make-keymap)) (F-km (make-keymap))
      (t-km (make-keymap)) (T-km (make-keymap)))
  (%set-keymap-name! f-km "find-char")
  (%set-keymap-name! F-km "find-char-back")
  (%set-keymap-name! t-km "to-char")
  (%set-keymap-name! T-km "to-char-back")
  (for-each (lambda (map)
    (bind-prefix! map "f" f-km)
    (bind-prefix! map "F" F-km)
    (bind-prefix! map "t" t-km)
    (bind-prefix! map "T" T-km))
    (list normal-map pending-map select-map)))
(set-command-display-binding! 'vim-motion-f "f <char>")
(set-command-display-binding! 'vim-motion-F "F <char>")
(set-command-display-binding! 'vim-motion-t "t <char>")
(set-command-display-binding! 'vim-motion-T "T <char>")

;; f/F/t/T — bind all printable non-space chars programmatically
(do ((i 33 (+ i 1)))
    ((= i 127))
  (let ((ch (string (integer->char i))))
    (for-each (lambda (map)
      (set-key! map (string-append "f " ch) 'vim-motion-f)
      (set-key! map (string-append "F " ch) 'vim-motion-F)
      (set-key! map (string-append "t " ch) 'vim-motion-t)
      (set-key! map (string-append "T " ch) 'vim-motion-T))
      (list normal-map pending-map select-map))))

;; f/F/t/T — space needs SPC notation
(for-each (lambda (map)
  (set-key! map "f SPC" 'vim-motion-f)
  (set-key! map "F SPC" 'vim-motion-F)
  (set-key! map "t SPC" 'vim-motion-t)
  (set-key! map "T SPC" 'vim-motion-T))
  (list normal-map pending-map select-map))

;; r — replace char(s) without entering insert mode
(set-key! normal-map "r" 'vim-char-replace-setup)
(set-key! select-map "r" 'vim-char-replace-setup)

;; Register modes (second arg = keymap, third arg = allows-input)
(define-minor-mode 'vim-normal-mode normal-map)
(define-minor-mode 'vim-insert-mode insert-map #t)
(define-minor-mode 'vim-replace-mode insert-map #t)
(define-minor-mode 'vim-select-mode select-map)
(define-minor-mode 'vim-pending-mode pending-map)

;; Register mode icons
(register-mode-icon/full 'vim-normal-mode  "icon-normal.svg"
                         'mode.normal  'label.normal  'cursor.normal  'solid)
(register-mode-icon/full 'vim-insert-mode  "icon-insert.svg"
                         'mode.insert  'label.insert  'cursor.insert  'thin)
(register-mode-icon/full 'vim-replace-mode "icon-replace.svg"
                         'mode.replace 'label.replace 'cursor.replace 'under)
(register-mode-icon/full 'vim-select-mode  "icon-select.svg"
                         'mode.select  'label.select  'cursor.select  'solid)
(register-mode-icon/full 'vim-pending-mode "icon-pending.svg"
                         'mode.pending 'label.pending 'cursor.pending  'under)

;; UTF-8 helpers
(define (utf8-byte-len-at pos)
  (let* ((raw (char->integer (char-at pos)))
         (b   (if (< raw 0) (+ raw 256) raw)))
    (cond ((< b 128) 1)
          ((< b 224) 2)
          ((< b 240) 3)
          (else 4))))

(define (count-chars-in-range start end)
  (let loop ((pos start) (n 0))
    (if (>= pos end)
        n
        (loop (+ pos (utf8-byte-len-at pos)) (+ n 1)))))

;; State transitions
(defcommand (vim-normal)
  "vim: normal mode\nReturn to normal mode."
  (let ((was-editing? (or (%buffer-has-minor-mode? 'vim-insert-mode)
                          (%buffer-has-minor-mode? 'vim-replace-mode)
                          (%buffer-has-minor-mode? 'vim-select-mode)
                          (%buffer-has-minor-mode? 'vim-pending-mode))))
  (unless was-editing?
    (when (%search-bar-open?) (%search-cancel!)))
  ;; Finalize insert/replace session change (only meaningful when panes exist)
  (unless (no-panes?)
    (when (%change-active?)
      (let ((typed (%change-current-inserts)))
        (when vim-pending-repeat-info
          (let ((ri vim-pending-repeat-info))
            (%change-set-repeat-info!
              (make-repeat-info (ri-op ri) (ri-op-count ri)
                                (ri-motion ri) (ri-motion-count ri)
                                (ri-text-object ri) (ri-text-object-kind ri)
                                (ri-setup ri) typed))))
        (set! vim-pending-repeat-info #f)
        (when vim-rect-insert-info
          (let* ((info vim-rect-insert-info)
                 (row-min (car info))
                 (row-max (cadr info))
                 (col     (car (cddr info)))
                 (ragged? (and (> (length info) 3) (list-ref info 3))))
            (set! vim-rect-insert-info #f)
            (when (> (string-length typed) 0)
              (let loop ((line row-max))
                (when (> line row-min)
                  (point-set! (if ragged?
                                  (line-content-end line)
                                  (min (+ (%line-start-position line) col)
                                       (%line-end-position line))))
                  (%insert-string typed)
                  (loop (- line 1)))))))
        (%end-change))))
  ;; Mode cleanup always runs
  (disable-minor-mode 'vim-insert-mode)
  (disable-minor-mode 'vim-replace-mode)
  (disable-minor-mode 'vim-select-mode)
  (disable-minor-mode 'vim-pending-mode)
  (%set-cursor-override! #f)
  (enable-minor-mode 'vim-normal-mode)
  (%select-mode-set! 0)
  (%set-replace-mode! #f)
  (vim-reset-count)
  (set-local! 'mode-name "Normal")
  (message-clear)
  (when (%macro-recording?)
    (disable-minor-mode 'vim-recording-mode)
    (enable-minor-mode 'vim-recording-mode))
  ;; In read-only buffers: first escape exits any active mode, second closes the tab.
  (when (and (not was-editing?) (buffer-read-only?))
    (call-interactively 'tab-close))))

(defcommand (vim-insert)
  "vim: insert mode\nEnter insert mode."
  (unless (buffer-read-only?)
    (unless (%change-active?)
      (%begin-change)
      (set! vim-pending-repeat-info (make-repeat-info #f 1 #f #f #f #f 'insert #f)))
    (disable-minor-mode 'vim-normal-mode)
    (disable-minor-mode 'vim-replace-mode)
    (disable-minor-mode 'vim-select-mode)
    (disable-minor-mode 'vim-recording-mode)
    (enable-minor-mode 'vim-insert-mode)
    (%select-mode-set! 0)
    (%set-replace-mode! #f)
    (message-clear)
    (set-local! 'mode-name "Insert")))

(defcommand (vim-replace)
  "vim: replace mode\nEnter replace mode."
  (unless (buffer-read-only?)
    (unless (%change-active?)
      (%begin-change)
      (set! vim-pending-repeat-info (make-repeat-info #f 1 #f #f #f #f 'replace #f)))
    (disable-minor-mode 'vim-normal-mode)
    (disable-minor-mode 'vim-insert-mode)
    (disable-minor-mode 'vim-select-mode)
    (disable-minor-mode 'vim-recording-mode)
    (enable-minor-mode 'vim-replace-mode)
    (%select-mode-set! 0)
    (%set-replace-mode! #t)
    (message-clear)
    (set-local! 'mode-name "Replace")))

(define (enter-visual-submode mode-int name)
  (set! vim-last-visual-text-object #f)
  (set! vim-last-visual-text-object-kind #f)
  (unless (%buffer-has-minor-mode? 'vim-select-mode)
    (%mark-set-to-point! #\<))
  (disable-minor-mode 'vim-normal-mode)
  (disable-minor-mode 'vim-insert-mode)
  (disable-minor-mode 'vim-replace-mode)
  (enable-minor-mode 'vim-select-mode)
  (%select-mode-set! mode-int)
  (message-clear)
  (set-local! 'mode-name name))

(defcommand (vim-select)
  "vim: select mode\nEnter select mode."
  (if (and (%buffer-has-minor-mode? 'vim-select-mode) (= (%select-mode-get) 1))
      (vim-normal)
      (enter-visual-submode 1 "Select")))

(defcommand (vim-select-line)
  "vim: select line mode\nEnter visual line mode."
  (if (and (%buffer-has-minor-mode? 'vim-select-mode) (= (%select-mode-get) 2))
      (vim-normal)
      (enter-visual-submode 2 "Line")))

(defcommand (vim-select-rectangle)
  "vim: select rectangle mode\nEnter visual block mode."
  (if (and (%buffer-has-minor-mode? 'vim-select-mode) (rect-mode? (%select-mode-get)))
      (vim-normal)
      (enter-visual-submode 3 "Rectangle")))

(defcommand (vim-visual-dollar)
  "vim: line end\nMove to end of line; in rectangle mode, enables ragged right edge."
  (when (rect-mode? (%select-mode-get))
    (%select-mode-set! 4))
  (line-end))


;; Query function for UI
(define (vim-state)
  (cond
    ((%buffer-has-minor-mode? 'vim-normal-mode) 'normal)
    ((%buffer-has-minor-mode? 'vim-insert-mode) 'insert)
    (else #f)))

;; Enable vim mode
(defun (vim-mode)
  "vim: enable vim mode\nEnable vim-like modal editing."
  (enable-minor-mode 'vim-normal-mode))


;;;
;;; Vim State Machine
;;;

;; Range record
(define-record-type <range>
  (make-range start end type)
  range?
  (start range-start)
  (end range-end)
  (type range-type))  ; 'char | 'line

;; State variables
(define vim-sm-state 'normal)      ; 'normal | 'operator-pending
(define vim-count #f)              ; #f or accumulated int
(define vim-op-count #f)           ; count before operator
(define vim-pending-op #f)         ; operator symbol or #f
(define vim-op-echo #f)            ; echo string like "d" or "2d", or #f

;; Repeat-info record
(define-record-type <repeat-info>
  (make-repeat-info op op-count motion motion-count text-object text-object-kind setup insert-text)
  repeat-info?
  (op ri-op) (op-count ri-op-count)
  (motion ri-motion) (motion-count ri-motion-count)
  (text-object ri-text-object) (text-object-kind ri-text-object-kind)
  (setup ri-setup) (insert-text ri-insert-text))

;; Pending repeat-info (set during operator exec, consumed on insert exit)
(define vim-pending-repeat-info #f)

;; Visual text-object tracking (for dot repeat of visual operations)
(define vim-last-visual-text-object #f)
(define vim-last-visual-text-object-kind #f)
(define vim-rect-insert-info #f)  ; #f or (list row-min row-max col [ragged?])

(define (rect-mode? m) (or (= m 3) (= m 4)))

;; Motion registry
(define *motion-table* (make-hash-table eq?))

(define (register-motion! name proc)
  (hash-table-set! *motion-table* name proc))

(define (motion-ref name)
  (hash-table-ref/default *motion-table* name #f))

;; Operator registry
(define *operator-table* (make-hash-table eq?))

(define (register-operator! name proc)
  (hash-table-set! *operator-table* name proc))

(define (operator-ref name)
  (hash-table-ref/default *operator-table* name #f))

;; Text object registry
(define *text-object-table* (make-hash-table eq?))

(define (register-text-object! name proc)
  (hash-table-set! *text-object-table* name proc))

(define (text-object-ref name)
  (hash-table-ref/default *text-object-table* name #f))

(define (text-object-for-char ch)
  (case ch
    ((#\w) 'word)
    ((#\W) 'WORD)
    ((#\s) 'sentence)
    ((#\p) 'paragraph)
    ((#\[ #\]) 'bracket)
    ((#\( #\) #\b) 'paren)
    ((#\{ #\} #\B) 'brace)
    ((#\< #\>) 'angle)
    ((#\t) 'tag)
    ((#\") 'dquote)
    ((#\') 'squote)
    ((#\`) 'backtick)
    (else #f)))

;; Reset state machine
(define (vim-reset-count)
  (set! vim-sm-state 'normal)
  (set! vim-count #f)
  (set! vim-op-count #f)
  (set! vim-pending-op #f)
  (set! vim-op-echo #f)
  (message-clear)
  (%set-key-unbound-cb! #f)
  (disable-minor-mode 'vim-pending-mode)
  (enable-minor-mode 'vim-normal-mode)
  (set-local! 'mode-name "Normal")
  (when (%macro-recording?)
    (disable-minor-mode 'vim-recording-mode)
    (enable-minor-mode 'vim-recording-mode)))

;; Word character classification
(define (vim-word-char? c)
  (or (and (char>=? c #\a) (char<=? c #\z))
      (and (char>=? c #\A) (char<=? c #\Z))
      (and (char>=? c #\0) (char<=? c #\9))
      (char=? c #\_)))

(define (vim-whitespace? c)
  (or (char=? c #\space) (char=? c #\tab) (char=? c #\newline)
      (char=? c #\return)))

(define (vim-punctuation? c)
  (and (not (vim-word-char? c))
       (not (vim-whitespace? c))
       (not (char=? c #\null))))

;; Compute effective count: multiply op-count and motion-count
(define (vim-effective-count)
  (* (or vim-op-count 1) (or vim-count 1)))

;; Core dispatch: execute a motion
(define (vim-execute-motion motion-sym)
  (let ((proc (motion-ref motion-sym)))
    (when proc
      (if (eq? vim-sm-state 'operator-pending)
          ;; Operator-pending: save point, run motion, build range, apply op
          (let* ((the-op vim-pending-op)
                 (the-op-count vim-op-count)
                 (the-motion-count vim-count)
                 (saved-point (point-get))
                 (eff-count (vim-effective-count))
                 (_ (proc eff-count))
                 (new-point (point-get))
                 (start (min saved-point new-point))
                 (end (max saved-point new-point))
                 (range (make-range start end
                                   (if (eq? motion-sym 'current-line) 'line 'char)))
                 (op-proc (operator-ref the-op))
                 (ri (make-repeat-info the-op the-op-count
                                       motion-sym the-motion-count #f #f #f #f)))
            (point-set! saved-point)
            (vim-reset-count)
            (%begin-change)
            (when op-proc (op-proc range))
            ;; If operator entered insert mode, defer change end
            (if (%buffer-has-minor-mode? 'vim-insert-mode)
                (set! vim-pending-repeat-info ri)
                (begin
                  (%change-set-repeat-info! ri)
                  (%end-change))))
          ;; Normal state: just run motion with count
          (begin
            (proc (or vim-count 1))
            (set! vim-count #f)
            (message-clear))))))

;; Core dispatch: enter operator-pending
(define (vim-enter-operator op-sym)
  (unless (and (buffer-read-only?)
               (not (eq? op-sym 'op-yank)))
  (if (and (eq? vim-sm-state 'operator-pending)
           (eq? vim-pending-op op-sym))
      ;; Doubled operator (dd, cc): move to line-start first so
      ;; saved-point in vim-execute-motion captures the beginning
      (let* ((orig-point (point-get))
             (_ (line-start))
             (line-start-pos (point-get))
             (col (+ 1 (- orig-point line-start-pos))))
        (vim-execute-motion 'current-line)
        (set-column col))
      (let* ((count-str (if vim-count (number->string vim-count) ""))
             (ch (last-key-char))
             (op-str (if ch (string ch) ""))
             (echo (string-append count-str op-str)))
        (set! vim-op-echo echo)
        (set! vim-op-count (or vim-count 1))
        (set! vim-count #f)
        (set! vim-pending-op op-sym)
        (set! vim-sm-state 'operator-pending)
        (disable-minor-mode 'vim-normal-mode)
        (enable-minor-mode 'vim-pending-mode)
        (set-local! 'mode-name "Pending")
        (%set-key-unbound-cb! 'vim-normal)
        (message-echo (string-append echo "-"))))))

;; Core dispatch: execute a text object
(define (vim-execute-text-object kind)
  (let* ((ch (last-key-char))
         (sym (and ch (text-object-for-char ch)))
         (proc (and sym (text-object-ref sym))))
    (when proc
      (let ((range (proc kind (vim-effective-count))))
        (when range
          (if (eq? vim-sm-state 'operator-pending)
              ;; Operator-pending: wrap in transaction, apply operator
              (let ((op-proc (operator-ref vim-pending-op))
                    (ri (make-repeat-info vim-pending-op vim-op-count
                                          #f vim-count sym kind #f #f)))
                (vim-reset-count)
                (%begin-change)
                (when op-proc (op-proc range))
                (if (%buffer-has-minor-mode? 'vim-insert-mode)
                    (set! vim-pending-repeat-info ri)
                    (begin
                      (%change-set-repeat-info! ri)
                      (%end-change))))
              ;; Visual mode: set selection
              (when (%buffer-has-minor-mode? 'vim-select-mode)
                (set! vim-last-visual-text-object sym)
                (set! vim-last-visual-text-object-kind kind)
                (point-set! (range-start range))
                (%mark-set-to-point! #\<)
                (point-set! (- (range-end range) 1))
                (set! vim-count #f)
                (message-clear))))))))
