(import (chezscheme) (otium runtime))

(let loop ([args (cdr (command-line))])
  (cond
    [(null? args) (repl)]
    [(and (equal? (car args) "-L") (pair? (cdr args)))
     (load-path (append (load-path) (list (cadr args))))
     (loop (cddr args))]
    [(equal? (car args) "--help")
     (display "Usage: otium-chez [-L directory] [script.scm | --repl]\n")
     (display "Forms: (ns name), (require name :as alias :refer (names) :reload),\n")
     (display "       (defonce name value), (defmacro name parameters body ...), (live 'binding).\n")
     (display "Otium macros expand before Chez compilation; see chez/NOTES.txt for supported forms.\n")]
    [(and (equal? (car args) "--repl") (null? (cdr args))) (repl)]
    [(and (null? (cdr args)) (not (char=? (string-ref (car args) 0) #\-)))
     (load-source (car args))]
    [else (error 'otium-chez "invalid arguments; use --help" args)]))
