" Vim filetype plugin
" Language: Otium

if exists('b:did_ftplugin')
  finish
endif
let b:did_ftplugin = 1

setlocal comments=:;;;,:;;,:;
setlocal commentstring=;\ %s
setlocal formatoptions-=t
setlocal lisp

" Otium symbols may contain punctuation that other Lisps reserve.
setlocal iskeyword+=33,36-38,42-43,45-47,58,60-64,94,124,126

let &l:define = '^\s*(\%(define\|def\|define-\|defmacro\|defn\)\s\+'
let &l:lispwords = join([
      \ 'if', 'define', 'def', 'define-', 'lambda', 'fn', 'defmacro',
      \ 'begin', 'do', 'let', 'while', 'cond', 'ns', 'handler-bind',
      \ 'restart-case', 'try', 'unwind-protect', 'defer', 'defparam',
      \ 'with-params', 'when', 'unless', 'defn', 'dotimes'
      \ ], ',')

let b:undo_ftplugin = 'setlocal comments< commentstring< define< formatoptions< iskeyword< lisp< lispwords<'
