(library (otium expander)
  (export make-expander expander-top expander-head)
  (import (chezscheme) (only (otium reader) read-otium))

  ;; The expander itself is shared/prelude/expander.scm, read with the Otium
  ;; reader and evaluated with the Otium core forms. It uses no macros, so it
  ;; needs no expansion. The runtime supplies the three natives it calls.
  (define-record-type (expander make-expander expander?)
    (fields lookup native-syntax? invoke))

  (define current-expander (make-parameter #f))

  (define (shared-source)
    (let loop ([directories (library-directories)])
      (if (null? directories)
          (error 'macroexpand "shared expander source not found" (library-directories))
          (let ([path (string-append (caar directories) "/../shared/prelude/expander.scm")])
            (if (file-exists? path) path (loop (cdr directories)))))))

  (define shared-environment
    (let ([env (copy-environment
                 (environment '(otium core) '(only (otium conditions) error)))])
      (define-top-level-value 'expander-macro-var
        (lambda (name) ((expander-lookup (current-expander)) name)) env)
      (define-top-level-value 'expander-invoke-macro
        (lambda (macro args) ((expander-invoke (current-expander)) macro args)) env)
      (define-top-level-value 'expander-native-syntax?
        (lambda (name) ((expander-native-syntax? (current-expander)) name)) env)
      (let ([path (shared-source)])
        (call-with-input-file path
          (lambda (port)
            (let loop ()
              (let ([form (read-otium port path)])
                (unless (eof-object? form)
                  (eval form env)
                  (loop)))))))
      env))

  (define expand-top (top-level-value '*expander* shared-environment))
  (define expand-head (top-level-value 'expander-expand-head shared-environment))

  (define (expander-top expander form)
    (parameterize ([current-expander expander]) (expand-top form)))

  (define (expander-head expander form once?)
    (parameterize ([current-expander expander]) (expand-head form once?))))
