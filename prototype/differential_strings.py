#!/usr/bin/env python3
"""Compare UTF-8 strings, buffers and printing with Chez."""
import differential

differential.CASES = [
    '(println (list (substring "ab" -5 99) (substring "ab" 99) (substring "ab" 1 0) (substring "aé🙂z" 1 3)))',
    '(println (list (get "aé🙂" 2) (get "a" -1 :missing) (string->list "aé🙂") (string #\\a #\\é #\\🙂)))',
    '(println (list (string-split "  a\\tb\\nc ") (string-split "a,,b," ",") (string-split "") (string-split "" ",")))',
    '(println (list (string-join "-" [1 2 3]) (string-join ":" nil) (string-join "-" (list #\\a #\\b))))',
    '(println (list (string-replace "ab" "" "X") (string-replace "a🙂b🙂" "🙂" "λ") (string-trim " \\té\\n")))',
    '(println (list (string-index "a🙂é🙂" "🙂" 2) (string-repeat "é🙂" 2)))',
    '(def b (buffer "é")) (def before (buffer->string b)) (buffer-push! b "🙂" 7) (println (list before (buffer->string b) (length b)))',
    '(println (list (contains? "a🙂b" "🙂") (contains? \'(1 2 3) 2) (contains? nil 1)))',
    '(println (list (str 1 "x" :k \'sym 2.5) (str #\\a "bc") (str ["x"])))',
    '(println (list (string->number " 42\\n") (string->number "1 ;hi") (string->number "1 2") (string->number " 3.5 ")))',
    '(println (list (string<? "a\\0b" "a\\0c") (substring "a\\0b" 1 2)))',
    '(println (list (name :hello) (name \'hello) (symbol "hello") (symbol :hello)))',
    '(println (list (char->integer #\\🙂) (integer->char 955) (char-upcase #\\a) (char-downcase #\\A) (char-whitespace? #\\tab)))',
    '(println (list (string-upcase "abC") (string-downcase "AbC") (string-starts-with? "abc" "ab") (string-ends-with? "abc" "bc")))',
    '(display "a" "b") (newline) (write "a" "b") (newline)',
]
differential.CASES += ['(println (list (keyword "foo") (keyword \'foo) (keyword :foo) (keyword ":foo") (symbol :foo)))']
differential.INVALID = ['(string-ref "a" -1)', '(string-ref "a" 1)', '(substring "a" 0.5)',
    '(string-split "a" "")', '(string-split "a" nil)', "(list->string '(1))", '(integer->char 55296)', '(integer->char 1114112)', '(buffer-push! "a" 1)']
if __name__ == '__main__':
    differential.main()
