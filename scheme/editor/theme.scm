(define *themes* (make-hash-table))
(define *current-theme* #f)
(define (current-theme) *current-theme*)

;;; --- Helpers ---

(define (parse-theme-args args)
  (let loop ((a args) (pal '()) (cmap '()) (ovr '()))
    (cond
      ((null? a) (values pal cmap ovr))
      ((eq? (car a) 'palette)       (loop (cddr a) (cadr a) cmap ovr))
      ((eq? (car a) 'canonical-map) (loop (cddr a) pal (cadr a) ovr))
      ((eq? (car a) 'overrides)     (loop (cddr a) pal cmap (cadr a)))
      (else (error "define-theme: unknown key" (car a))))))

(define (define-theme name display-name . kwargs)
  (call-with-values
    (lambda () (parse-theme-args kwargs))
    (lambda (pal cmap ovr)
      (hash-table-set! *themes* name (list display-name pal cmap ovr)))))

(define (theme-display-name sym)
  (let ((t (hash-table-ref/default *themes* sym #f)))
    (and t (car t))))

(define (list-themes)
  (map (lambda (s) (cons s (theme-display-name s)))
       (hash-table-keys *themes*)))

;;; --- Alpha helper ---

(define (byte->hex2 n)
  (let ((s (number->string (max 0 (min 255 (exact (round n)))) 16)))
    (if (= (string-length s) 1) (string-append "0" s) s)))

;; Apply an opacity (0.0–1.0) to a hex colour string.
;; Accepts "#RRGGBB" or "#RRGGBBAA"; always returns "#RRGGBBAA".
(define (alpha hex-str opacity)
  (let* ((s   (if (char=? (string-ref hex-str 0) #\#)
                  (substring hex-str 1 (string-length hex-str))
                  hex-str))
         (rgb (substring s 0 6))
         (a   (* (max 0.0 (min 1.0 opacity)) 255)))
    (string-append "#" rgb (byte->hex2 a))))

;;; --- Canonical palette schema ---

(define *canonical-keys*
  '(;; neutrals
    bg-0 bg-1 bg-2 bg-3 fg-0 fg-1 fg-2
    ;; core hues
    red orange yellow green cyan blue purple
    ;; optional hues
    magenta pink teal lime gold
    ;; accents
    accent-0 accent-1 accent-2
    ;; extended virtual keys (supplied via canonical-map)
    fg-text fg-text-dim fg-warm selection-bg))

;; Resolve an (alpha key float) form against an alist of key→hex-string.
;; Returns the computed hex string, or #f if the key is missing.
(define (resolve-alpha form alist)
  (let* ((ref-key (cadr form))
         (opacity (car (cddr form)))
         (entry   (assq ref-key alist)))
    (and entry (string? (cdr entry)) (alpha (cdr entry) opacity))))

;; Resolve a value that is either a hex string or an (alpha key float) form.
(define (resolve-hex val alist)
  (cond
    ((string? val) val)
    ((and (pair? val) (eq? (car val) 'alpha)) (resolve-alpha val alist))
    (else #f)))

;; Load the raw palette sequentially, resolving (alpha key float) forms
;; against previously-loaded entries. Returns resolved alist of key→hex.
(define (load-raw-palette raw-palette)
  (let loop ((entries raw-palette) (resolved '()))
    (if (null? entries)
        resolved
        (let* ((entry (car entries))
               (key   (car entry))
               (hex   (resolve-hex (cdr entry) resolved)))
          (when hex (%set-palette! key hex))
          (loop (cdr entries)
                (if hex (cons (cons key hex) resolved) resolved))))))

;; Build canonical alist from resolved palette + canonical-map.
;; cmap values may be a raw palette symbol or (alpha raw-key float).
(define (build-canonical resolved-palette cmap)
  (map (lambda (ckey)
         (let ((mapped (assq ckey cmap)))
           (cons ckey
                 (if mapped
                     (let ((cmap-val (cdr mapped)))
                       (if (pair? cmap-val)
                           ;; (alpha raw-key float) in canonical-map
                           (resolve-alpha cmap-val resolved-palette)
                           ;; symbol: look up in resolved palette
                           (let ((p (assq cmap-val resolved-palette)))
                             (and p (cdr p)))))
                     ;; no mapping: try direct name match in resolved palette
                     (let ((p (assq ckey resolved-palette)))
                       (and p (cdr p)))))))
       *canonical-keys*))

(define (merge-roles base overrides)
  (let loop ((base base) (result overrides))
    (if (null? base) result
        (loop (cdr base)
              (if (assq (caar base) overrides)
                  result
                  (cons (car base) result))))))

;;; --- Default role mapping ---

(define *default-roles*
  '(;; structural
    (ui.bg             . bg-1)
    (pane.bg           . bg-0)
    (bar.bg            . bg-2)
    (line.bg           . fg-0)
    ;; borders
    (border.active     . purple)
    (border.inactive   . fg-0)
    (border.bell       . yellow)
    ;; chrome
    (scrollbar         . fg-0)
    (scrollbar.hover   . accent-0)
    (tab.active        . bg-0)
    (tab.hover         . bg-0)
    (tab.inactive      . bg-2)
    (tab.close         . fg-0)
    ;; text
    (text.primary      . fg-text)
    (text.faded        . fg-2)
    (text.key          . fg-text-dim)
    (text.command      . fg-text)
    (text.prefix       . fg-text)
    (text.linenum      . purple)
    (diff.added        . green)
    (diff.modified     . yellow)
    (diff.deleted      . red)
    (search.error      . red)
    (selection         . selection-bg)
    (selection.hover   . (alpha selection-bg 0.2))
    (message.hover     . (alpha fg-0 0.5))
    ;; mode indicators
    (mode.normal       . blue)
    (mode.insert       . green)
    (mode.replace      . red)
    (mode.select       . yellow)
    (mode.pending      . pink)
    (mode.minibuffer   . blue)
    (mode.search       . blue)
    ;; mode labels
    (label.normal      . fg-0)
    (label.insert      . fg-0)
    (label.replace     . fg-0)
    (label.select      . fg-0)
    (label.pending     . fg-0)
    (label.minibuffer  . fg-0)
    (label.search      . fg-0)
    ;; cursors
    (cursor.normal     . fg-warm)
    (cursor.insert     . fg-warm)
    (cursor.replace    . fg-warm)
    (cursor.select     . fg-warm)
    (cursor.pending    . fg-warm)
    (cursor.minibuffer . fg-warm)
    (cursor.search     . fg-warm)
    ;; macro
    (macro.indicator   . red)
    (macro.bg          . fg-2)
    ;; syntax
    (hl.keyword        . (:color purple))
    (hl.string         . (:color green))
    (hl.comment        . (:color accent-0 :font buf-italic))
    (hl.number         . (:color orange))
    (hl.constant       . (:color orange))
    (hl.function       . (:color blue))
    (hl.builtin        . (:color cyan))
    (hl.operator       . (:color fg-text))
    (hl.bracket        . (:color accent-0))
    (hl.property       . (:color blue))
    (hl.bracket.match  . (:color fg-warm :bg selection-bg))))

;;; --- Activation ---

;; If a role value is (alpha key float), create a synthetic palette entry
;; and return its key symbol. Otherwise return val unchanged.
(define *alpha-seq* 0)
(define (resolve-role-value val full-palette)
  (if (and (pair? val) (eq? (car val) 'alpha))
      (let ((hex (resolve-alpha val full-palette)))
        (if hex
            (let ((sym (string->symbol
                        (string-append "%alpha"
                                       (number->string
                                        (begin (set! *alpha-seq* (+ *alpha-seq* 1))
                                               *alpha-seq*))))))
              (%set-palette! sym hex)
              sym)
            val))
      val))

(defun (activate-theme input)
  "Switch to the named theme."
  (let* ((sym           (if (string? input) (string->symbol input) input))
         (entry         (hash-table-ref *themes* sym))
         (raw-palette   (list-ref entry 1))
         (canonical-map (list-ref entry 2))
         (overrides     (list-ref entry 3)))

    (%clear-palette!)
    (%clear-roles!)

    ;; 1. Load raw palette sequentially; (alpha key float) values are resolved
    ;;    against previously-loaded entries in the same palette.
    (let* ((resolved     (load-raw-palette raw-palette))
           ;; 2. Build canonical entries and push them into palette_table so
           ;;    default role symbols like 'purple, 'fg-text resolve via C.
           (canonical    (build-canonical resolved canonical-map))
           ;; Combined lookup for role resolution: raw + canonical
           (full-palette (append canonical resolved)))
      (for-each (lambda (e) (when (cdr e) (%set-palette! (car e) (cdr e))))
                canonical)

      ;; 3. Apply merged roles; (alpha key float) role values get a synthetic
      ;;    palette entry so C's symbol→palette lookup continues to work.
      (for-each (lambda (r)
                  (%set-role! (car r) (resolve-role-value (cdr r) full-palette)))
                (merge-roles *default-roles* overrides)))

    (set! *current-theme* sym)
    (%update-icon-colors!)))

;;; --- Bundled themes (one file per family in theme/) ---
