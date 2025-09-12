" Vim syntax file
" Language: Otium
" Maintainer: Generated for Otium language
" Last Change: 2025

if exists("b:current_syntax")
  finish
endif

" Keywords
syn keyword otiumKeyword fn if else return
syn keyword otiumBoolean true false
syn keyword otiumNull null

" Built-in functions
syn keyword otiumBuiltin print

" Operators
syn match otiumOperator "\v\+|-|\*|/|\%"
syn match otiumOperator "\v\=\="
syn match otiumOperator "\v!\="
syn match otiumOperator "\v\<\="
syn match otiumOperator "\v\>\="
syn match otiumOperator "\v\<"
syn match otiumOperator "\v\>"
syn match otiumOperator "\v\="
syn match otiumOperator "\v\&\&"
syn match otiumOperator "\v\|\|"
syn match otiumOperator "\v!"

" Assignment operator
syn match otiumAssign "\v:="

" Numbers
syn match otiumNumber "\v<\d+>"
syn match otiumNumber "\v<\d+\.\d+>"

" Strings
syn region otiumString start='"' skip='\\"' end='"'
syn region otiumString start="'" skip="\\'" end="'"

" Comments (assuming C-style comments)
syn match otiumComment "\v//.*$"
syn region otiumComment start="/\*" end="\*/"

" Identifiers (variables and function names)
syn match otiumIdentifier "\v<[a-zA-Z_][a-zA-Z0-9_]*>"

" Parentheses, brackets, braces
syn match otiumDelimiter "\v[\(\)\[\]\{\}]"

" Punctuation
syn match otiumPunctuation "\v[,;]"

" Function definition
syn match otiumFunction "\v<[a-zA-Z_][a-zA-Z0-9_]*>\ze\s*:=\s*fn"

" Function calls
syn match otiumFunctionCall "\v<[a-zA-Z_][a-zA-Z0-9_]*>\ze\s*\("

" Define highlighting
hi def link otiumKeyword Keyword
hi def link otiumBoolean Boolean
hi def link otiumNull Constant
hi def link otiumBuiltin Function
hi def link otiumOperator Operator
hi def link otiumAssign Operator
hi def link otiumNumber Number
hi def link otiumString String
hi def link otiumComment Comment
hi def link otiumIdentifier Identifier
hi def link otiumDelimiter Delimiter
hi def link otiumPunctuation Delimiter
hi def link otiumFunction Function
hi def link otiumFunctionCall Function

let b:current_syntax = "otium"