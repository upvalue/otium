(library (otium process)
  (export public-bindings application-arguments application-script)
  (import (chezscheme))
  (define application-arguments (make-parameter '()))
  (define application-script (make-parameter #f))
  ;; Names must be nonempty and free of #\=; neither may contain NUL.
  (define (env-string? x name?)
    (and (string? x)
         (let ([n (string-length x)])
           (and (or (not name?) (> n 0))
                (let loop ([i 0])
                  (or (= i n)
                      (let ([c (string-ref x i)])
                        (and (not (char=? c #\nul))
                             (not (and name? (char=? c #\=)))
                             (loop (+ i 1))))))))))
  ;; Chez has getenv and putenv but no unsetenv, so unsetting goes through libc.
  (define unsetenv
    (let ([p (delay
               (begin
                 (unless (foreign-entry? "unsetenv")
                   (load-shared-object
                     (if (memq (machine-type) '(arm64osx ta6osx tarm64osx a6osx))
                         "/usr/lib/libSystem.B.dylib"
                         "libc.so.6")))
                 (foreign-procedure "unsetenv" (string) int)))])
      (lambda (name) ((force p) name))))
  (define (env-get name)
    (unless (env-string? name #t) (error 'getenv "expected a variable name" name))
    (or (getenv name) (void)))
  (define (env-set! name value)
    (unless (env-string? name #t) (error 'setenv! "expected a variable name" name))
    (cond
      [(eq? value (void))
       (unless (zero? (unsetenv name)) (error 'setenv! "could not change the environment" name))]
      [(env-string? value #f) (putenv name value)]
      [else (error 'setenv! "expected a string without NUL or nil" value)])
    (void))
  (define public-bindings
    (list (cons 'arguments (lambda () (application-arguments)))
          (cons 'script-name (lambda () (application-script)))
          (cons 'write-error (lambda (text) (display text (current-error-port))))
          (cons 'getenv env-get)
          (cons 'setenv! env-set!))))
