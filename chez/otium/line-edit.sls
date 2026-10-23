(library (otium line-edit)
  (export line-editor-available? make-line-input-port line-editor-close line-editor-service!)
  (import (chezscheme))

  (define loaded? #f)
  (define (line-editor-available?) (getenv "OTIUM_LINE_LIBRARY"))
  (define (initialize)
    (unless loaded?
      (load-shared-object (getenv "OTIUM_LINE_LIBRARY"))
      (set! loaded? #t)))
  (define service-callback #f)
  (define (line-editor-service! thunk)
    (initialize)
    (when service-callback (unlock-object service-callback))
    (set! service-callback (foreign-callable (lambda (ignored) (thunk)) (void*) void))
    (lock-object service-callback)
    ((foreign-procedure "ot_line_set_service" (void* void*) void)
      (foreign-callable-entry-point service-callback) 0))
  (define (line-editor-close)
    (when loaded? ((foreign-procedure "ot_line_close" () void))))

  (define (make-line-input-port prompt bindings)
    (initialize)
    (let ([read-line (foreign-procedure "ot_line_read" (string) string)]
          [clear-bindings (foreign-procedure "ot_line_bindings_clear" () void)]
          [add-binding (foreign-procedure "ot_line_binding" (string string) void)]
          [interrupted? (foreign-procedure "ot_line_interrupted" () boolean)]
          [text ""] [position 0])
      (make-custom-textual-input-port "otium-repl"
        (lambda (buffer start count)
          (when (and (> count 0) (= position (string-length text)))
            (clear-bindings)
            (for-each (lambda (entry) (add-binding (car entry) (cdr entry))) (bindings))
            (let ([line (read-line (prompt))])
              (when (interrupted?) (error 'repl "interrupted"))
              (set! text (if line (string-append line "\n") ""))
              (set! position 0)))
          (let ([n (min count (- (string-length text) position))])
            (do ([i 0 (+ i 1)]) ((= i n))
              (string-set! buffer (+ start i) (string-ref text (+ position i))))
            (set! position (+ position n))
            n))
        #f #f #f))))
