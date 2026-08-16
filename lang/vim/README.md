# Otium support for Vim and Neovim

This directory is a Vim runtime package for Otium. It provides syntax
highlighting, Lisp indentation, comment settings, definition lookup, and
symbol rules for names such as `foo/bar?` and `put!`.

Add this directory to `runtimepath`, then enable the usual filetype features:

```vim
set runtimepath^=/path/to/otium/lang/vim
filetype plugin indent on
syntax enable
```

The package does not choose a filename extension. Select the filetype with
`:set filetype=otium`, a modeline, or an autocmd for the file pattern you use.
For example, an Otium modeline is itself a valid line comment:

```lisp
; vim: set filetype=otium :
```

The filetype plugin enables Vim's Lisp mode and teaches it Otium's body forms.
The syntax groups and the `()`, `[]`, and `{}` delimiter pairs follow the
conventions used by vim-sexp. A structural editing plugin that keeps its own
filetype allow-list will need `otium` added to that list.

## Conjure

The package also includes an Otium client for
[Conjure](https://github.com/Olical/conjure). Build the interpreter, then add
the client before Conjure loads:

```lua
vim.g["conjure#filetypes"] = { "otium" }
vim.g["conjure#filetype#otium"] = "otium.conjure"
vim.g["conjure#client#otium#stdio#command"] = {
  "/path/to/otium/lang/build/otium",
  "--server",
}
```

The client does not need to know about a checkout's load path. Otium reads that
from the nearest `project.ot`, and the client passes `--project` with the file
nearest the current buffer so the result does not depend on where Neovim was
started. A command that already names `--project` or `--no-project` is left
alone, which is also how you point the client at a different project file.

Conjure starts one live Otium process. Its normal evaluation mappings work,
including `<localleader>ee` for the current form, `<localleader>eb` for the
buffer, and `K` for documentation. `<localleader>mx` macroexpands the current
form. The client adds `<localleader>cs`, `<localleader>cS`, and
`<localleader>ei` to start, stop, and interrupt the process.
