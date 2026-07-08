;; Icon registry wrappers and built-in icon registrations.

(define (register-icon name filename color-role)
  (%register-icon! name filename color-role))

;; Tab bar icons
(register-icon 'tab-close "icon-close.svg" 'text.primary)
(register-icon 'tab-icon "tab-icon.svg" 'text.primary)
(register-icon 'welcome-icon "welcome-icon.svg" 'text.primary)
(register-icon 'plaintext-icon "icon-plaintext.svg" 'text.primary)
(register-icon 'scheme-icon "icon-scheme.svg" 'text.primary)
(register-icon 'json-icon "icon-json.svg" 'text.primary)
(register-icon 'icon-unknown-extension "icon-unknown-extension.svg" 'text.primary)

(register-icon 'caret-left "icon-caret-left.svg" 'text.primary)
(register-icon 'caret-right "icon-caret-right.svg" 'text.primary)
(register-icon 'caret-left-faded "icon-caret-left.svg" 'text.faded)
(register-icon 'caret-right-faded "icon-caret-right.svg" 'text.faded)

(register-icon 'case-icon        "icon-case.svg" 'text.primary)
(register-icon 'case-icon-active "icon-case.svg" 'border.active)

(register-icon 'word-icon        "icon-word.svg" 'text.primary)
(register-icon 'word-icon-active "icon-word.svg" 'border.active)

(register-icon 'match-replace-icon        "icon-match-replace.svg" 'text.primary)
(register-icon 'match-replace-icon-active "icon-match-replace.svg" 'border.active)

(register-icon 'replace-next-icon        "icon-replace-next.svg" 'text.primary)
(register-icon 'replace-next-icon-faded  "icon-replace-next.svg" 'text.faded)
(register-icon 'replace-all-icon         "icon-replace-all.svg"  'text.primary)
(register-icon 'replace-all-icon-faded   "icon-replace-all.svg"  'text.faded)

(register-icon 'new-icon "icon-plus.svg" 'text.primary)
(register-icon 'open-icon "icon-open.svg" 'text.primary)
(register-icon 'help-icon "icon-help.svg" 'text.primary)
(register-icon 'palette-icon "icon-palette.svg" 'text.primary)
(register-icon 'project-icon "icon-project.svg" 'text.primary)

(defvar default-scale "Default scaling factor for app UI." 1.0)
(defvar default-buffer-scale "Default scaling factor for buffer text." 1.0)
