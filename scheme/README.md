# scheme/ Subsystem


## Purpose

Scheme scripting layer defining all user-facing commands, keybindings, modes, and themes. C provides ~60 primitive operations; Scheme composes them into the full editing experience. The Vim modal editing system lives entirely here.

## Module System

All Scheme code (except `init.scm`) is in R7RS `define-library` forms under the `(editor ...)` namespace. Each module has a `.sld` (exports) and a `.scm` (implementation) in `scheme/editor/`.

C primitives are exposed as `(editor primitives)`, registered synthetically in `scheme.c` via `add-module!`. Libraries import from it and from each other.

`scheme.c` adds `scheme/` to the module search path, registers `(editor primitives)`, then loads `init.scm`. The `import` forms in `init.scm` trigger Chibi's module loader to resolve the dependency graph automatically.

### Module dependency graph

```
(editor primitives)    ← synthetic module wrapping C FFI bindings
  ↑
(editor command)       ← imports primitives
  ↑
(editor mode)          ← imports primitives, command
(editor icon)          ← imports primitives, command
(editor built-in)      ← imports primitives, command
(editor minibuffer)    ← imports primitives, command, mode
(editor which-key)     ← imports primitives, command
  ↑
(editor vim)           ← imports primitives, command, mode
(editor theme)         ← imports primitives only

init.scm               ← imports all modules; sets global keybindings, mouse/welcome handlers, runs startup
```

## Module Responsibilities

### (editor command)
Command infrastructure. `defcommand`, `defvar`, `defun` macros for self-documenting definitions. `call-interactively` resolves interactive specs via CPS traversal and invokes commands (cached by C at init). `set-key!` wraps `%set-key!` and records the reverse binding for `where-is`. `bind-prefix!` registers prefix keymaps. Spec handlers registered with `register-spec-handler!` handle async argument collection (e.g. `minibuffer-read`).

### (editor mode)
Major/minor mode wrappers over C primitives. `define-major-mode`, `define-minor-mode`, buffer-local variables (`set-local!`/`get-local`), `register-mode-icon/full`. Pre-defines base categories `prog-mode` and `text-mode` (the default mode), plus `display-line-numbers-mode`.

### (editor icon)
Icon registration (`register-icon` wrapping `%register-icon!`), tab close/icon SVGs, default scale variables.

### (editor built-in)
~40 `defcommand` declarations for C primitives (no body). Compound commands built from primitives (e.g. `open-line-below`, `join-line`, `save-buffer`, `save-buffer-as`, `switch-to-buffer`, `describe-function`, `describe-key`).

### (editor minibuffer)
Minibuffer keymap and mode (`minibuffer-mode`). `minibuffer-read` activates the minibuffer with a prompt and Scheme callback. `execute-extended-command` (M-x equivalent). Registers `minibuffer-read` as the `'minibuffer-read` interactive spec handler.

### (editor which-key)
`which-key-toggle` command wrapping `%which-key-toggle`.

### (editor vim)
Vim-like modal editing split across `scheme/editor/vim/`, all included in one scope by `vim.sld`.

- **`core.scm`** — keymap declarations, all static key bindings (normal/insert/select/pending/command maps), mode registration and icons for the 6 vim minor modes, UTF-8 helpers, mode transition commands, state machine records, motion/operator/text-object registries, character classification, and the three dispatch functions (`vim-execute-motion`, `vim-enter-operator`, `vim-execute-text-object`).
- **`undo.scm`** — `vim-undo`/`vim-redo`/`vim-line-restore`, dot-repeat infrastructure, count helpers.
- **`motion.scm`** — motion registrations (h/j/k/l/0/$/^/w/b/e/W/B/E/goto-line/current-line), mark commands, jump list commands, f/F/t/T character-seek.
- **`operator.scm`** — register helpers, delete/change/yank operators, character replace, compound entry commands, yank/paste commands.
- **`text-object.scm`** — pair/quote/tag search helpers, text object registrations (word/WORD/sentence/paragraph/brackets/quotes/tags), dynamic `i`/`a` key bindings.
- **`visual.scm`** — visual selection helpers, visual operator commands, visual mode key bindings.
- **`macro.scm`** — `vim-recording-mode`, macro commands (`vim-start-macro`, `vim-stop-macro`, `vim-play-macro`), dynamic `q`/`@` bindings, activation calls.

### (editor theme)
Theme registration and activation. `define-theme` stores a theme record in `*themes*`. `activate-theme` runs the three-step activation pipeline (see below). Bundled themes live in `scheme/editor/theme/`.

### init.scm (not a library)
Loaded by `scheme.c`. Imports all `(editor ...)` modules. Sets global keybindings and prefix maps (buffer, help, line-numbers, split, tab). Binds M-x to `execute-extended-command`. Installs mouse click/drag handlers, registers `welcome-map`, runs startup.

## Key Invariants

- All commands reachable from keybindings are Scheme symbols invoked via `call-interactively`
- `call-interactively` must be defined before `scheme_init()` returns (C caches it)
- Keymap parent chains enable inheritance: mode keymaps inherit `global-keymap`
- Lookup order: minor mode keymaps → major mode keymap → global keymap (each traverses parent chain)
- Only one major mode per buffer; multiple minor modes allowed
- Minor modes with `allows_input: true` (insert, replace, minibuffer) capture unbound chars as `self-insert`
- Vim state is pure Scheme; C only provides motion/edit primitives
- Theme activation is immediate; no persistence layer

## Common Modification Workflows

### Adding a new command
1. If a C primitive is needed: add it in the appropriate subsystem, register via `SDEF()` in `scheme_init()`, add to `(editor primitives)` exports in `scheme.c`
2. `(defcommand (name) "docstring" body)` in the appropriate module
3. Add `(interactive (minibuffer-read "Prompt: "))` if it takes user input
4. Export from the module's `.sld`
5. `(set-key! keymap "key" 'name)`

### Adding a new vim motion
1. `(register-motion! 'name (lambda (count) ...))` in `vim/motion.scm`; lambda must move point
2. `defcommand` wrapper calling `(vim-execute-motion 'name)`
3. Bind in `normal-map`, `pending-map`, `select-map` (bindings in `vim/core.scm` or inline)
4. Export from `editor/vim.sld`

### Adding a new vim operator
1. `(register-operator! 'name (lambda (range) ...))` in `vim/operator.scm`
2. `defcommand` wrapper calling `(vim-enter-operator 'name)`
3. Bind in `normal-map`; visual binding goes in `vim/visual.scm`
4. Export from `editor/vim.sld`

### Adding a new mode
1. `(define-minor-mode 'name keymap [allows-input])` or `(define-major-mode 'name "Display Name" [icon-or-#f [keymap]])` in `mode.scm`
   - Major modes take a required display name (shown in the status bar) and an optional icon symbol (e.g. `'scheme-icon`); pass `#f` or omit for no icon
2. Optionally `(register-mode-icon/full 'name "file.svg" 'bg-role 'label-role 'cursor-role 'cursor-type)` for vim-style minor mode pill icons
3. Create activation/deactivation commands; export from `editor/mode.sld`

### Adding a new theme

Themes have three parts:

**`palette`** — an alist of `(key . value)` where value is either a hex string `"#RRGGBB"` or an `(alpha key float)` form referencing an earlier entry in the same palette. Use whatever names make sense for the color family.

**`canonical-map`** — maps the fixed canonical key set to palette keys (or `(alpha palette-key float)` for transparency). The canonical keys are: `bg-0..bg-3`, `fg-0..fg-2`, `red orange yellow green cyan blue purple magenta pink teal lime gold`, `accent-0..accent-2`, `fg-text fg-text-dim fg-warm selection-bg`. Keys not in the canonical-map fall back to a same-name lookup in the palette.

**`overrides`** — a partial alist of `(role . palette-key)` applied on top of `*default-roles*`. Only supply entries that differ from the defaults. Role values may also be `(alpha palette-key float)`.

```scheme
(define-theme
  'my-theme
  "My Theme Display Name"
  'palette
  '((bg-0    . "#1a1b26")
    (bg-1    . "#16161e")
    (bg-2    . "#13131a")
    (fg-0    . "#2a2b3d")
    (fg-1    . "#3b3c52")
    (fg-2    . "#414868")
    (accent-0 . "#565f89")
    (accent-1 . "#6b7394")
    (accent-2 . "#828bb8")
    (text-0  . "#c0caf5")
    (text-1  . "#a9b1d6")
    (text-2  . "#9aa5ce")
    (select  . (alpha accent-2 0.25))
    (blue    . "#7aa2f7")
    (cyan    . "#7dcfff")
    (green   . "#9ece6a")
    (yellow  . "#e0af68")
    (orange  . "#ff9e64")
    (red     . "#f7768e")
    (purple  . "#bb9af7")
    (pink    . "#ff007c"))
  'canonical-map
  '((fg-text      . text-0)
    (fg-text-dim  . text-2)
    (fg-warm      . text-1)
    (selection-bg . select))
  'overrides
  '((mode.normal . blue)))   ; omit to keep *default-roles* value
```

Activate with `(activate-theme 'my-theme)`. To activate on startup, add the call to `init.scm`.

To add a theme to the bundled set: put the `define-theme` call in a new file under `scheme/editor/theme/`, then `(load "editor/theme/my-theme.scm")` from `scheme/editor/theme.scm`.

### Adding a new module
1. `scheme/editor/foo.sld` — `(define-library (editor foo) ...)` with exports
2. `scheme/editor/foo.scm` — implementation
3. `(import (editor foo))` in `init.scm`
