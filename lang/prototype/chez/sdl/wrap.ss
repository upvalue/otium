(import (chezscheme))

;; Select the upstream definitions needed by our exports. Unrelated unfinished
;; bindings and duplicate names in the top-level upstream script stay unused.
(define source (cadr (command-line)))
(define exports (call-with-input-file (caddr (command-line)) read))
(define output (cadddr (command-line)))
(define definitions (make-eq-hashtable))
(define ordered '())
(define selected (make-eq-hashtable))
(define (read-all path)
  (call-with-input-file path
    (lambda (port)
      (let loop ()
        (let ([form (read port)])
          (unless (eof-object? form)
            (when (pair? form)
              (case (car form)
                [(include) (read-all (string-append source "/" (cadr form)))]
                [(define define-ftype define-syntax)
                 (let ([name (if (pair? (cadr form)) (caadr form) (cadr form))])
                   (unless (hashtable-contains? definitions name)
                     (hashtable-set! definitions name form)
                     (set! ordered (cons name ordered))))]))
            (loop)))))))
(read-all (string-append source "/SDL.scm"))
(define (select! value)
  (cond
    [(pair? value) (select! (car value)) (select! (cdr value))]
    [(and (symbol? value) (hashtable-contains? definitions value)
          (not (hashtable-contains? selected value)))
     (hashtable-set! selected value #t)
     (select! (hashtable-ref definitions value #f))]))
(for-each (lambda (name)
            (unless (hashtable-contains? definitions name)
              (error 'wrap "missing upstream definition" name))
            (select! name)) exports)
(when (file-exists? output) (delete-file output))
(call-with-output-file output
  (lambda (port)
    (display ";; Generated subset of chez-sdl3; see sdl/DEPENDENCY.txt and upstream LICENSE.\n" port)
    ;; Preserve SDL's license notice from the original header.
    (call-with-input-file (string-append source "/SDL.scm")
      (lambda (input)
        (let loop ([n 0])
          (when (< n 23)
            (display (get-line input) port) (newline port) (loop (+ n 1))))))
    (pretty-print
      `(library (sdl3)
         (export ,@exports)
         (import (chezscheme))
         (define otium-sdl-loaded
           (begin
             (load-shared-object
               (or (getenv "SDL3_LIBRARY") (error 'sdl3 "set SDL3_LIBRARY")))
             #t))
         ,@(map (lambda (name) (hashtable-ref definitions name #f))
             (filter (lambda (name) (hashtable-contains? selected name)) (reverse ordered))))
      port)))
