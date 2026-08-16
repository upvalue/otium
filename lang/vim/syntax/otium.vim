" Vim syntax file
" Language: Otium

" Otium's reader is case-sensitive and treats most punctuation as part of a
" symbol. Keep this list in step with ftplugin/otium.vim.

if exists('b:current_syntax')
  finish
endif

syntax case match
syntax iskeyword @,48-57,_,192-255,33,36-38,42-43,45-47,58,60-64,94,124,126

syntax keyword otiumTodo TODO FIXME XXX NOTE contained
syntax match otiumComment /;.*/ contains=otiumTodo,@Spell

syntax match otiumInvalidEscape /\\./ contained
syntax match otiumEscape /\\[ntr0e"\\]/ contained
syntax region otiumString start=/"/ skip=/\\\\\|\\"/ end=/"/ contains=otiumEscape,otiumInvalidEscape

syntax match otiumNumber /\<[+-]\?\d\+\>/
syntax match otiumNumber /\<[+-]\?0[xX][0-9a-fA-F]\+\>/
syntax match otiumNumber /\<[+-]\?\%(\d\+\.\d*\|\.\d\+\)\%([eE][+-]\?\d\+\)\?\>/
syntax match otiumNumber /\<[+-]\?\d\+[eE][+-]\?\d\+\>/

syntax keyword otiumBoolean true false
syntax match otiumBoolean /#\%(t\|f\|true\|false\)\>/
syntax keyword otiumNil nil
syntax match otiumKeyword /\<:\k\+\>/

syntax match otiumQuote /,@\|['`,]/
syntax match otiumDelimiter /[()[\]{}]/

syntax keyword otiumSpecialForm
      \ quote if define def define- set! lambda fn defmacro begin do let while
      \ and or cond else quasiquote unquote unquote-splicing ns in-ns require
      \ handler-bind restart-case try catch unwind-protect defer defparam with-params

syntax keyword otiumMacro when unless defn -> ->> dotimes

syntax keyword otiumBuiltin
      \ * *expander* + - / < <= = > >= abs append apply array array->list array?
      \ boolean? buffer buffer->string buffer-push! buffer? caar cadr car cddr cdr
      \ ceiling comp compute-restarts condition-message condition-of-type?
      \ condition-type cons concat contains? copy count current-ns dec define-condition
      \ describe display dissoc drop empty? eq? equal? error error? eval even? exit
      \ expander-lexical? expander-macro-var false? filter find-restart first float?
      \ floor for-each frequencies gensym get get-in group-by identity inc int?
      \ invoke-restart keys keyword keyword? last length list list->array list? macro?
      \ macroexpand macroexpand-1 map max merge min modulo name neg? newline nil? not
      \ nth null? number->string number? odd? pair? partial pop! pos? print println
      \ procedure? push! put! quit quotient range read-string reduce remainder rest
      \ restart-description restart-name reverse round second signal sort sort-by str
      \ string->number string->symbol string-append string-contains? string-downcase
      \ string-ends-with? string-join string-length string-replace string-split
      \ string-starts-with? string-trim string-upcase string<? string? substring symbol
      \ symbol->string symbol? table table? take third true? type type-pred update
      \ update! values write zero?

highlight default link otiumTodo Todo
highlight default link otiumComment Comment
highlight default link otiumString String
highlight default link otiumEscape SpecialChar
highlight default link otiumInvalidEscape Error
highlight default link otiumNumber Number
highlight default link otiumBoolean Boolean
highlight default link otiumNil Constant
highlight default link otiumKeyword Constant
highlight default link otiumQuote SpecialChar
highlight default link otiumDelimiter Delimiter
highlight default link otiumSpecialForm Statement
highlight default link otiumMacro Macro
highlight default link otiumBuiltin Function

syntax sync fromstart
let b:current_syntax = 'otium'
