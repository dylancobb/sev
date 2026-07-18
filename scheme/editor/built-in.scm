;;; build-in.scm - built-in editor command definitions.

;; C primitive commands
(defcommand quit "quit\nExit the editor.")
(defcommand self-insert "editor: self-insert character\nInsert the character that invoked this command.")
(defcommand eval-buffer "scheme: evaluate buffer\nEvaluate the current buffer as Scheme code.")
(defcommand next-line "editor: move cursor to next line\nMove cursor to the next line.")
(defcommand prev-line "editor: move cursor to previous line\nMove cursor to the previous line.")
(defcommand forward-char "editor: move cursor forward one character")
(defcommand backward-char "editor: move cursor backward one character")
(defcommand line-start "editor: move cursor to start of line")
(defcommand line-end "editor: move cursor to end of line")
(defcommand skip-whitespace "editor: move past whitespace\nMove cursor to next non-space, non-tab character.")
(defcommand newline "editor: insert newline at cursor")
(defcommand insert-tab "editor: insert tab\nInsert tab character or expand to spaces at next tab stop.")
(defcommand delete-backward-char "editor: backspace\nDelete the character before cursor.")
(defcommand delete-forward-char "editor: delete\nDelete the character after cursor.")
(set-doc! 'move-cursor 'function "Move cursor by COUNT characters.")
(set-doc! 'delete-char 'function "Delete COUNT characters before cursor.")
(set-doc! 'insert-char 'function "Insert character CH at cursor.")
(defcommand (tab-next) "pane: next tab\nSwitch to the next tab."
  (unless (no-panes?) (%tab-next)))
(defcommand (tab-prev) "pane: previous tab\nSwitch to the previous tab."
  (unless (no-panes?) (%tab-prev)))
(defcommand reset-global-scale "sev: reset UI scale\nReset global UI scaling factor.")
(defcommand increase-global-scale "sev: increase UI scale\nIncrease global UI scaling factor.")
(defcommand decrease-global-scale "sev: decrease UI scale\nDecrease global UI scaling factor.")
(defcommand (reset-buffer-scale) "sev: reset buffer scale\nReset buffer text scaling factor."
  (unless (no-panes?) (%reset-buffer-scale)))
(defcommand (increase-buffer-scale) "sev: increase buffer scale\nIncrease buffer text scaling factor."
  (unless (no-panes?) (%increase-buffer-scale)))
(defcommand (decrease-buffer-scale) "sev: decrease buffer scale\nDecrease buffer text scaling factor."
  (unless (no-panes?) (%decrease-buffer-scale)))
(defcommand (split-horizontal) "pane: create horizontal split\nSplit the current pane horizontally."
  (unless (no-panes?) (%split-horizontal)))
(defcommand (split-vertical) "pane: create vertical split\nSplit the current pane vertically."
  (unless (no-panes?) (%split-vertical)))
(defcommand (tab-close) "pane: close tab"
  (unless (no-panes?) (%tab-close)))
(defcommand (pane-navigate-up) "pane: navigate up"
  (unless (no-panes?) (%pane-navigate-up)))
(defcommand (pane-navigate-down) "pane: navigate down"
  (unless (no-panes?) (%pane-navigate-down)))
(defcommand (pane-navigate-left) "pane: navigate left"
  (unless (no-panes?) (%pane-navigate-left)))
(defcommand (pane-navigate-right) "pane: navigate right"
  (unless (no-panes?) (%pane-navigate-right)))
(defcommand (pane-h-split-increase) "pane: increase width"
  (unless (no-panes?) (%pane-h-split-increase)))
(defcommand (pane-h-split-decrease) "pane: decrease width"
  (unless (no-panes?) (%pane-h-split-decrease)))
(defcommand (pane-v-split-increase) "pane: increase height"
  (unless (no-panes?) (%pane-v-split-increase)))
(defcommand (pane-v-split-decrease) "pane: decrease height"
  (unless (no-panes?) (%pane-v-split-decrease)))
(defcommand clay-debug "debug: toggle Clay debug mode.")
(defcommand exchange-point-and-mark "editor: swap cursor and selection boundary")

;; compound Scheme commands
(defcommand (eval-expression)
  "scheme: evaluate expression\nEvaluate a Scheme expression from the minibuffer and display the result."
  (interactive)
  (minibuffer-read "Evaluate a Scheme expression..."
    (lambda (expr-str)
      (unless (string=? expr-str "")
        (let* ((expr   (read (open-input-string expr-str)))
               (result (eval expr (interaction-environment)))
               (out    (open-output-string)))
          (write result out)
          (message (get-output-string out)))))))

(defcommand (line-start-skip-whitespace)
  "editor: move to first non-blank char\nSet cursor to first non-blank character on current line."
  (line-start) (skip-whitespace))
(defcommand (join-line)
  "editor: join current and next line\nDelete newline character between the current and subsequent line."
  (unless (buffer-read-only?)
    (line-end) (delete-forward-char)))

;; Return the file extension (after last "."), or "" if none.
(define (file-extension path)
  (let loop ((i (- (string-length path) 1)))
    (cond
      ((< i 0) "")
      ((char=? (string-ref path i) #\.) (substring path (+ i 1) (string-length path)))
      (else (loop (- i 1))))))

;; Alist mapping file extensions to mode-activation procedures.
(define *auto-mode-alist*
  (list (cons "scm" scheme-mode)
        (cons "sld" scheme-mode)
        (cons "json" json-mode)))

;; Activate the appropriate major mode for the current buffer based on filename.
(define (set-auto-mode!)
  (let* ((name (%buffer-file-name))
         (ext  (if name (file-extension name) "")))
    (let loop ((alist *auto-mode-alist*))
      (cond
        ((null? alist) (%ts-disable!) (%set-major-mode 'text-mode))
        ((string=? (caar alist) ext) ((cdar alist)))
        (else (loop (cdr alist)))))))

(defcommand (save-buffer-as)
  "editor: save file as...\nSave the current buffer under a new file name."
  (interactive)
  (if (no-panes?)
      (message "No buffer to save")
      (minibuffer-read "Save as: "
        (lambda (filename)
          (if (string=? filename "")
              (message "Filename cannot be empty")
              (begin
                (%set-buffer-file-name! filename)
                (%buffer-set-name! filename)
                (set-auto-mode!)
                (if (%buffer-write)
                    (message (string-append "Saved " filename))
                    (message (string-append "Failed to save " filename)))))))))

(defcommand (save-buffer)
  "editor: save file\nSave the current buffer. If no file name is set, prompt for one."
  (interactive)
  (if (no-panes?)
      (message "No buffer to save")
      (let ((filename (%buffer-file-name)))
        (if (not (eq? filename #f))
            (if (%buffer-write)
                (begin
                  (%tab-set-preview! #f)
                  (message (string-append "Saved " filename)))
                (message (string-append "Failed to save " filename)))
            (call-interactively 'save-buffer-as)))))

(defcommand (open-file)
  "editor: open file\nOpen an existing file into a new buffer, or switch to it if already open."
  (interactive)
  (minibuffer-read "Open a file..."
    (lambda (filename)
      (if (string=? filename "")
          (message "Filename cannot be empty")
          (if (not (file-exists? filename))
              (message (string-append "File not found: " filename))
              (begin
                (%jump-push!)
                (when (no-panes?)
                  (%tab-new! filename)
                  (%tab-set-preview! #t))
                (if (%buffer-create filename)
                    (begin
                      (%tab-set-buffer! filename)
                      (%set-buffer-file-name! filename)
                      (begin
                      (if (%buffer-read)
                          (message (string-append "Opened " filename))
                          (message (string-append "Failed to read " filename)))
                      (set-auto-mode!)))
                    (begin
                      (%tab-set-buffer! filename)
                      (message (string-append "Switched to buffer " filename))))))))))

(define (file-picker-open! path)
  (if (file-exists? path)
      (begin
        (%jump-push!)
        (when (no-panes?) (%tab-new! path) (%tab-set-preview! #t))
        (if (%buffer-create path)
            (begin
              (%tab-set-buffer! path)
              (%set-buffer-file-name! path)
              (if (%buffer-read)
                  (message (string-append "Opened " path))
                  (message (string-append "Failed to read " path)))
              (set-auto-mode!))
            (begin
              (%tab-set-buffer! path)
              (message (string-append "Switched to " path)))))
      (message (string-append "File not found: " path))))

(defcommand (file-picker)
  "editor: find file\nOpen the async file picker (Ctrl-P)."
  (%minibuffer-activate-file-picker))

(defcommand (open-project)
  "project: open\nOpen a project directory, changing the working directory to it."
  (interactive)
  (if (not (%show-open-folder-dialog))
      (minibuffer-read "Open project: "
        (lambda (path)
          (if (string=? path "")
              (message "Path cannot be empty")
              (if (not (%chdir path))
                  (message (string-append "Cannot open project: " path))
                  (begin
                    (%update-recent-project! path)
                    (%scanner-start!)
                    (message (string-append "Opened project " path)))))))))


(defcommand (read-file)
  "editor: paste file at cursor\nInsert the contents of a file at the point in the current buffer without moving point or other text."
  (interactive)
  (minibuffer-read "Insert file: "
    (lambda (filename)
      (if (string=? filename "")
          (message "Filename cannot be empty")
          (begin
            (if (%buffer-insert filename)
                (message (string-append "Inserted " filename))
                (message (string-append "Could not read " filename))))))))

(defcommand (buffer-new)
  "editor: new buffer\nCreate a new untitled buffer and display it in the current tab."
  (interactive)
  (let ((name "untitled"))
    (if (no-panes?)
        (%tab-new-fresh! name)
        (begin
          (%buffer-create name)
          (%tab-set-buffer! name)))
    (message (string-append "Created buffer " name))))

(defcommand (buffer-rename)
  "editor: rename buffer\nRename the current buffer."
  (interactive)
  (if (no-panes?)
      (message "No buffer to rename")
      (minibuffer-read "Rename buffer to: "
        (lambda (new-name)
          (if (string=? new-name "")
              (message "Buffer name cannot be empty")
              (if (%buffer-set-name! new-name)
                  (message (string-append "Buffer renamed to " new-name))
                  (message (string-append "Name already in use: " new-name))))))))

(defcommand (switch-to-buffer)
  "editor: switch buffer\nSwitch the current tab to a named buffer."
  (interactive)
  (if (no-panes?)
      (message "No pane to switch buffer in")
      (minibuffer-read "Switch to buffer: "
        (lambda (name)
          (if (string=? name "")
              (message "Buffer name cannot be empty")
              (if (%tab-set-buffer! name)
                  (message (string-append "Switched to " name))
                  (message (string-append "No buffer named \"" name "\""))))))))

(defcommand (buffer-close)
  "editor: close buffer\nClose a named buffer, closing all tabs that currently display it."
  (interactive)
  (minibuffer-read "Close buffer: "
    (lambda (name)
      (if (string=? name "")
          (message "Buffer name cannot be empty")
          (if (%buffer-close! name)
              (message (string-append "Closed buffer " name))
              (message (string-append "No buffer named \"" name "\"")))))))

(defcommand (tab-new)
  "pane: new tab\nCreate a new tab. Prompts for a buffer name; untitled if left empty."
  (interactive)
  (let ((buf-name "untitled"))
    (if (%tab-new-fresh! buf-name)
        (begin
          (set-major-mode! 'text-mode)
          (message (string-append "Opened tab " buf-name)))
        (message (string-append "Failed to create tab " buf-name)))))


;; In-buffer search bar commands (search-mode keybindings)
(defcommand (search-backspace)
  "search: delete character\nDelete the last character from the search query."
  (%search-backspace!))

(defcommand (search-confirm)
  "search: confirm\nConfirm search and jump to the first match after the cursor."
  (%search-confirm!))

(defcommand (search-cancel)
  "search: cancel\nCancel search and clear highlights."
  (%search-cancel!))

(defcommand (search-forward-char)
  "search: move cursor forward one character"
  (%search-forward-char!))

(defcommand (search-backward-char)
  "search: move cursor backward one character"
  (%search-backward-char!))

(defcommand (search-shift-forward-char)
  "search: move cursor right, extending selection"
  (%search-shift-forward-char!))

(defcommand (search-shift-backward-char)
  "search: move cursor left, extending selection"
  (%search-shift-backward-char!))

(defcommand (search-toggle-case)
  "search: toggle case sensitivity\nToggle case-sensitive matching in the search bar."
  (%search-toggle-case!))

(defcommand (search-toggle-regex)
  "search: toggle regular expressions
Toggle regex matching in the search bar."
  (%search-toggle-regex!))

(defcommand (search-toggle-whole-words)
  "search: toggle whole-word matching\nToggle whole-word matching in the search bar."
  (%search-toggle-whole-words!))

(defcommand (search-toggle-match-in-selection)
  "search: toggle match in selection\nRestrict search matches to the active visual selection."
  (%search-toggle-match-in-selection!))

;; Replace bar commands
(defcommand (search-toggle-replace)
  "search: toggle replace bar\nOpen or close the replacement-string bar."
  (%search-toggle-replace!))

(defcommand (replace-backspace)
  "search: delete character from replacement field"
  (%replace-backspace!))

(defcommand (replace-forward-char)
  "search: move cursor forward one character in replacement field"
  (%replace-forward-char!))

(defcommand (replace-backward-char)
  "search: move cursor backward one character in replacement field"
  (%replace-backward-char!))

(defcommand (replace-shift-forward-char)
  "search: move cursor right, extending selection, in replacement field"
  (%replace-shift-forward-char!))

(defcommand (replace-shift-backward-char)
  "search: move cursor left, extending selection, in replacement field"
  (%replace-shift-backward-char!))

(defcommand (search-replace-next)
  "search: replace next match\nReplace the match at or ahead of the cursor with the replacement string."
  (%search-replace-next!))

(defcommand (search-replace-all)
  "search: replace all matches\nReplace every match in the buffer with the replacement string."
  (%search-replace-all!))
