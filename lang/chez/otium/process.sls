(library (otium process)
  (export public-bindings application-arguments application-script)
  (import (chezscheme))
  (define application-arguments (make-parameter '()))
  (define application-script (make-parameter #f))
  (define public-bindings
    (list (cons 'arguments (lambda () (application-arguments)))
          (cons 'script-name (lambda () (application-script)))
          (cons 'write-error (lambda (text) (display text (current-error-port)))))))
