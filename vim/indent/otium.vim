" Vim indent file
" Language: Otium

if exists('b:did_indent')
  finish
endif
let b:did_indent = 1

setlocal autoindent
setlocal nosmartindent

let b:undo_indent = 'setlocal autoindent< smartindent<'
