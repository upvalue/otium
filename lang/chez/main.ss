(import (chezscheme) (otium runtime) (otium launcher-options)
        (otium process) (prefix (otium conditions) ot:))

(define (fail message)
  (display (string-append "otium: " message "\n") (current-error-port))
  (exit 2))
(define (split text delimiter)
  (let loop ([start 0] [i 0] [out '()])
    (if (= i (string-length text))
        (reverse (cons (substring text start i) out))
        (if (char=? (string-ref text i) delimiter)
            (loop (+ i 1) (+ i 1) (cons (substring text start i) out))
            (loop start (+ i 1) out)))))
(define (find-project)
  (let loop ([directory (current-directory)])
    (let ([path (string-append directory "/project.ot")])
      (cond [(file-exists? path) path]
            [(equal? directory "/") #f]
            [else (loop (path-parent directory))]))))
(define (project-paths path)
  (if (and path (file-exists? path))
      (call-with-input-file path
        (lambda (port)
          (let ([source (get-string-all port)])
            (let loop ([i 0] [out '()])
              (cond [(= i (string-length source)) (reverse out)]
                    [(char=? (string-ref source i) #\")
                     (let end ([j (+ i 1)])
                       (cond [(= j (string-length source)) (reverse out)]
                             [(char=? (string-ref source j) #\")
                              (let ([entry (substring source (+ i 1) j)])
                                (loop (+ j 1)
                                  (cons (if (path-absolute? entry) entry
                                            (string-append (path-parent path) "/" entry)) out)))]
                             [else (end (+ j 1))]))]
                    [else (loop (+ i 1) out)])))))
      '()))

(guard (c [(ot:control-condition? c) (exit (ot:control-status c))]
          [else (display-condition c (current-error-port))
                (newline (current-error-port)) (exit 1)])
  (let ([script #f] [files '()] [paths '()] [project #f]
        [no-project #f] [repl? #f])
    (let loop ([args (cdr (command-line))])
      (unless (null? args)
        (let ([token (car args)])
          (cond
            [(equal? token "--")
             (unless script (fail "-- requires a script"))
             (application-arguments (cdr args))]
            [(or (= (string-length token) 0) (not (char=? (string-ref token 0) #\-)))
             (when script (fail "extra script; use --load or put application arguments after --"))
             (set! script token)
             (loop (cdr args))]
            [else
             (let* ([token (cond [(equal? token "-h") "--help"]
                                 [(equal? token "-L") "--path"] [else token])]
                    [pieces (split token #\=)]
                    [spelling (car pieces)]
                    [option (find (lambda (entry)
                                    (equal? spelling (string-append "--" (car entry))))
                              launcher-options)])
               (unless option (fail (string-append "unknown option " token)))
               (let* ([name (car option)] [owner (cadr option)] [kind (caddr option)]
                      [attached? (pair? (cdr pieces))]
                      [value (and attached? (substring token (+ 1 (string-length spelling)) (string-length token)))]
                      [rest (cdr args)])
                 (cond [(eq? kind 'flag)
                        (when attached? (fail (string-append spelling " takes no value")))]
                       [(eq? kind 'integer)
                        (unless attached? (fail (string-append spelling " requires =INTEGER")))
                        (unless (and (> (string-length value) 0)
                                     (for-all (lambda (c) (char<=? #\0 c #\9)) (string->list value))
                                     (<= 1 (string->number value) 18446744073709551615))
                          (fail (string-append spelling " requires an integer")))
                        (when (or (and (equal? name "c.heap-init") (< (string->number value) 1024))
                                  (and (equal? name "c.max-depth") (> (string->number value) 4294967295)))
                          (fail (string-append spelling " out of range")))]
                       [(not attached?)
                        (when (null? rest)
                          (fail (string-append spelling " requires a " (symbol->string kind))))
                        (set! value (car rest)) (set! rest (cdr rest))])
                 (when (eq? owner 'common)
                   (cond [(equal? name "help") (display launcher-help) (exit 0)]
                         [(equal? name "repl") (set! repl? #t)]
                         [(equal? name "path") (set! paths (append paths (list value)))]
                         [(equal? name "project") (set! project value)]
                         [(equal? name "no-project") (set! no-project #t)]
                         [(equal? name "load") (set! files (append files (list value)))]))
                 (loop rest)))]))))
    (when (and project no-project) (fail "--project and --no-project cannot be used together"))
    (let* ([environment-paths (filter (lambda (s) (not (equal? s ""))) (split (or (getenv "OTIUM_PATH") "") #\:))]
           [all-paths (append paths environment-paths
                       (if no-project '() (project-paths (or project (find-project)))))])
      (load-path (if (null? all-paths) '(".") all-paths)))
    (application-script script)
    (for-each load-source files)
    (when script (load-source script))
    (when (or repl? (and (not script) (null? files))) (repl))))
