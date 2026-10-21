(library (otium runtime)
  (export evaluate load-source require-namespace current-namespace namespace-name
          load-path namespace-ref namespace-set! repl dev-start! dev-stop! dev-service! dev-run
          report-condition)
  (import (chezscheme) (prefix (otium generic-core) gm:)
          (prefix (otium conditions) ot:) (otium expander) (otium line-edit)
          (otium runtime-documentation) (otium reader)
          (only (otium values) keyword otium-write) (prefix (otium values) val:))

  (define dev-active? #f)
  (define dev-deadline 0)
  (define dev-last-poll 0)
  (define dev-busy? #f)
  (define dev-clock #f)
  (define dev-poll #f)
  (define dev-send #f)
  (define dev-close #f)
  (define dev-next #f)
  (define dev-client #f)
  (define dev-old-timer #f)
  (define dev-output "")
  (define dev-original-output #f)
  (define dev-output-port #f)
  (define (dev-flush-output!)
    (when dev-output-port (flush-output-port dev-output-port))
    (unless (string=? dev-output "")
      (let ([text dev-output])
        (set! dev-output "")
        (dev-send 0 (dev-repr (val:table (keyword "event") (keyword "output")
                                      (keyword "text") text))))))
  (define (dev-repr value)
    (call-with-string-output-port (lambda (port) (otium-write value port))))
  (define (dev-call member . args)
    (apply (namespace-ref 'otium.dev member) args))
  (define (dev-service!) (when dev-active? (dev-call 'service!)))
  (define (dev-run) (dev-call 'run))
  (define (dev-stop!)
    (when dev-active?
      (dev-flush-output!)
      (set! dev-active? #f)
      (when dev-original-output (current-output-port dev-original-output))
      (set-timer 0)
      (timer-interrupt-handler dev-old-timer)
      (dev-close)))
  (define (dev-tick)
    (set-timer 10000)
    (when (and dev-active? (not dev-busy?))
      (let ([now (dev-clock)])
        (when (>= (- now dev-last-poll) 25)
          (set! dev-last-poll now)
          (dev-flush-output!)
          (when (or (not (= (dev-poll 0) 0)) (and (> dev-deadline 0) (>= now dev-deadline)))
            (let ([abort? #f])
              (dynamic-wind
                (lambda () (set! dev-busy? #t))
                (lambda ()
                  (set! abort?
                    (ot:restart-case
                      (dev-call 'break! #f (symbol->string (namespace-name (current-namespace))))
                      (continue () #f)
                      (abort () #t))))
                (lambda () (set! dev-busy? #f)))
              (when abort? (ot:error "evaluation aborted"))))))))
  (define (dev-start! path watchdog)
    (load-shared-object (getenv "OTIUM_DEV_LIBRARY"))
    (set! dev-clock (foreign-procedure "ot_dev_clock" () unsigned-64))
    (set! dev-poll (foreign-procedure "ot_dev_poll" (int) int))
    (set! dev-send (foreign-procedure "ot_dev_send" (int string) void))
    (set! dev-close (foreign-procedure "ot_dev_close" () void))
    (set! dev-next (foreign-procedure "ot_dev_next" () string))
    (set! dev-client (foreign-procedure "ot_dev_client" () int))
    (require-namespace 'otium.dev #f)
    (dev-call 'listen! path watchdog)
    (set! dev-old-timer (timer-interrupt-handler))
    (set! dev-active? #t)
    (set! dev-original-output (current-output-port))
    (let ([sink (make-custom-textual-output-port "dev-output"
                 (lambda (text start count)
                   (let ([part (substring text start (+ start count))])
                     (display part dev-original-output)
                     (flush-output-port dev-original-output)
                     (set! dev-output (string-append dev-output part)))
                   count)
                 #f #f #f)])
      (set! dev-output-port sink)
      (current-output-port sink))
    (parameterize ([current-namespace (find-namespace 'user)])
      (evaluate (read-otium (open-input-string "(require (otium.dev :as dev))") "<dev-start>")))
    (timer-interrupt-handler dev-tick)
    (set-timer 10000))
  (define (dev-evaluate code space path)
    (unless (and (string? code) (string? space) (string? path))
      (ot:error "eval expects string code, namespace and path"))
    (let ([output (open-output-string)] [results '()] [failure #f]
          [failed? #f] [saved-busy? dev-busy?] [line 1] [counted 0])
      (parameterize ([current-output-port output]
                     [current-namespace (find-namespace (string->symbol space))]
                     [source-path path])
        (dynamic-wind
          (lambda () (set! dev-busy? #f))
          (lambda ()
            (ot:try
              (call-with-port (open-input-string code)
                (lambda (port)
                  (let loop ()
                    (let skip ()
                      (let ([c (peek-char port)])
                        (cond
                          [(and (char? c) (char-whitespace? c)) (read-char port) (skip)]
                          [(eqv? c #\;) (get-line port) (skip)])))
                    (let ([position (port-position port)])
                      (do () ((= counted position))
                        (when (char=? (string-ref code counted) #\newline)
                          (set! line (+ line 1)))
                        (set! counted (+ counted 1))))
                    (let ([form (read-otium port path)])
                      (unless (eof-object? form)
                        (set! results (cons (dev-repr (evaluate form)) results))
                        (loop))))))
              (catch ((lambda (c) #t) c) (set! failed? #t) (set! failure c))))
          (lambda () (set! dev-busy? saved-busy?))))
      (when (and failed? (val:table? failure))
        (val:put! failure (keyword "source")
          (val:table (keyword "path") path (keyword "line") line)))
      (val:table (keyword "ok") (not failed?)
        (keyword "value") (if (null? results) "nil" (car results))
        (keyword "values") (reverse results)
        (keyword "condition") failure
        (keyword "output") (get-output-string output))))
  (define (dev-namespaces)
    (map (lambda (space) (val:table (keyword "name") (namespace-name space)))
         (vector->list (hashtable-values namespaces))))
  (define (dev-namespace-bindings name)
    (unless (symbol? name) (ot:error "bindings: expected namespace symbol"))
    (let* ([space (or (hashtable-ref namespaces name #f) (ot:error "bindings: unknown namespace"))]
           [env (namespace-environment space)])
      (map (lambda (member)
             (let* ([value (and (top-level-bound? member env) (top-level-value member env))]
                    [entry (val:table
                             (keyword "name") member
                             (keyword "kind") (keyword (cond [(otium-macro? value) "macro"]
                                                             [(procedure? value) "function"]
                                                             [else "value"]))
                             (keyword "public")
                             (not (hashtable-contains? (namespace-privates space) member))
                             (keyword "doc") (hashtable-ref (namespace-definitions space) member ""))])
               (when (procedure? value)
                 (val:put! entry (keyword "arity-mask") (procedure-arity-mask value)))
               entry))
           (vector->list (hashtable-keys (namespace-definitions space))))))
  (define (dev-bindings)
    (list (cons '%enabled? (lambda () dev-active?))
          (cons '%clock (lambda () (if dev-clock (dev-clock) 0)))
          (cons '%pid (lambda () ((foreign-procedure "ot_dev_pid" () int))))
          (cons '%uid (lambda () ((foreign-procedure "ot_dev_uid" () int))))
          (cons '%private-directory! (lambda (path)
            (= 1 ((foreign-procedure "ot_dev_private_directory" (string) int) path))))
          (cons '%open (lambda (path)
            (= 0 ((foreign-procedure "ot_dev_open" (string) int) path))))
          (cons '%cwd (lambda () ((foreign-procedure "ot_dev_cwd" () string))))
          (cons '%runtime (lambda () (keyword "chez")))
          (cons '%now (lambda () ((foreign-procedure "ot_dev_now" () integer-64))))
          (cons '%mtime (lambda (path)
            (let ([mtime ((foreign-procedure "ot_dev_mtime" (string) integer-64) path)])
              (and (>= mtime 0) mtime))))
          (cons '%realpath (lambda (path) ((foreign-procedure "ot_dev_realpath" (string) string) path)))
          (cons '%namespaces dev-namespaces)
          (cons '%bindings dev-namespace-bindings)
          (cons '%poll (lambda (ms) (if dev-poll (begin (dev-flush-output!) (dev-poll ms)) 0)))
          (cons '%next (lambda () (and dev-next (dev-next))))
          (cons '%client (lambda () (dev-client)))
          (cons '%send (lambda (client text) (dev-send client text)))
          (cons '%repr dev-repr)
          (cons '%evaluate dev-evaluate)
          (cons '%deadline (lambda (value)
            (if (= value -1) dev-deadline (set! dev-deadline value))))))

  (define-record-type otium-macro
    (fields transformer namespace name))

  (define-record-type namespace
    (fields name environment imports definitions privates
            (mutable loaded?) (mutable loading?) (mutable source)))
  (define namespaces (make-eq-hashtable))
  (define current-namespace (make-parameter #f))
  (define load-path (make-parameter '(".")))
  (define source-path (make-parameter #f))
  (define expansion-budget (make-parameter #f))

  (define (with-expansion thunk)
    (if (expansion-budget) (thunk)
        (parameterize ([expansion-budget (vector 10000 '())]) (thunk))))

  (define (binding-value space name)
    (let ([import (hashtable-ref (namespace-imports space) name #f)])
      (if import
          (namespace-ref (car import) (cdr import))
          (let ([env (namespace-environment space)])
            (and (top-level-bound? name env) (top-level-value name env))))))

  ;; Syntax that Otium provides walks like any other form. Other Chez syntax,
  ;; reached through the chez/ prefix, is left for Chez to expand.
  (define otium-syntax
    (let ([env (environment '(otium core)
                 '(only (otium conditions) handler-bind try restart-case unwind-protect defer))])
      (filter (lambda (name) (not (top-level-bound? name env))) (environment-symbols env))))

  (define (namespace-expander space)
    (make-expander
      (lambda (name)
        (let ([value (binding-value space name)])
          (and (otium-macro? value) value)))
      (lambda (name)
        (and (not (hashtable-contains? (namespace-imports space) name))
             (not (top-level-bound? name (namespace-environment space)))
             (top-level-syntax? name (namespace-environment space))
             (or (let ([s (symbol->string name)])
                   (and (>= (string-length s) 5) (string=? (substring s 0 5) "chez/")))
                 (procedure? (top-level-syntax name (namespace-environment space))))
             (not (memq name otium-syntax))))
      (lambda (macro args)
        (let ([budget (expansion-budget)])
          (when (= (vector-ref budget 0) 0)
            (ot:fail "E0111" 'macro (otium-macro-name macro)
              'notes (val:array
                       (format "most recent expansions: ~{~a~^, ~}" (reverse (vector-ref budget 1))))))
          (vector-set! budget 0 (- (vector-ref budget 0) 1))
          (vector-set! budget 1
            (cons (otium-macro-name macro)
              (let take ([names (vector-ref budget 1)] [n 11])
                (if (or (null? names) (= n 0)) '()
                    (cons (car names) (take (cdr names) (- n 1))))))))
        (guard (c [(ot:control-condition? c) (raise c)]
                  [else
                   (let ([inner (if (ot:otium-condition? c) (ot:otium-value c) c)])
                     (ot:fail "E0112" 'macro (otium-macro-name macro) 'cause inner
                       'notes (val:array
                                (string-append "caused by: "
                                  (if (and (val:table? inner)
                                           (string? (ot:condition-message inner)))
                                      (ot:condition-message inner)
                                      (call-with-string-output-port
                                        (lambda (port) (display-condition c port))))))))])
          (parameterize ([current-namespace (otium-macro-namespace macro)])
            (call-with-values
              (lambda () (apply (otium-macro-transformer macro) args))
              (lambda results
                (unless (= (length results) 1)
                  (error 'defmacro "transformer must return one datum" results))
                (car results))))))))

  (define (namespace-ref name member)
    (top-level-value member (namespace-environment (find-namespace name))))

  (define (namespace-set! name member value)
    (set-top-level-value! member value
      (namespace-environment (find-namespace name))))

  (define (find-namespace name)
    (unless (symbol? name) (error 'ns "expected a symbol" name))
    (or (hashtable-ref namespaces name #f)
        (let* ([env (copy-environment
                      (environment '(otium core)
                        '(only (otium conditions)
                           error signal condition-type condition-message error? type-pred
                           define-condition condition-of-type? handler-bind try restart-case
                           invoke-restart compute-restarts find-restart restart-name
                           restart-description unwind-protect defer quit exit)))]
               [space (make-namespace name env (make-eq-hashtable)
                        (make-eq-hashtable) (make-eq-hashtable) #f #f #f)])
          (hashtable-set! namespaces name space)
          (for-each (lambda (entry) (define-top-level-value (car entry) (cdr entry) env))
            gm:public-bindings)
          (install-application! env)
          (define-top-level-value 'describe
            (lambda (name)
              (unless (symbol? name) (error 'describe "expected a symbol" name))
              (describe-binding (current-namespace) name)) env)
          (define-top-level-value 'current-ns (lambda () (namespace-name (current-namespace))) env)
          (define-top-level-value 'macro? otium-macro? env)
          ;; Native syntax may preserve nested forms past the Otium traversal.
          ;; Reject declarations there too; lexical value bindings still shadow them.
          (for-each
            (lambda (name)
              (define-top-level-syntax name
                (lambda (form)
                  (gm:fail 'macroexpand 'invalid-declaration
                    "declaration requires top-level context" (syntax->datum form))) env))
            '(defclass defabstract defgeneric defmethod reset-class! reset-generic!))
          (for-each
            (lambda (entry)
              (define-top-level-value (car entry)
                (lambda (form)
                  (parameterize ([current-namespace space])
                    (with-expansion
                      (lambda ()
                        (expander-head (namespace-expander space) form (cdr entry)))))) env))
            '((macroexpand-1 . #t) (macroexpand . #f)))
          (define-top-level-value 'macroexpand-all
            (lambda (form)
              (parameterize ([current-namespace space])
                (with-expansion
                  (lambda ()
                    (let ([expander (namespace-expander space)])
                      (expander-top
                        (make-expander (expander-lookup expander) (expander-native-syntax? expander)
                          (expander-invoke expander) #t)
                        form)))))) env)
          (define-top-level-value 'eval
            (lambda (form)
              (parameterize ([current-namespace space]) (evaluate form))) env)
          ;; Capture the defining namespace, including when live is called later.
          (define-top-level-value 'live
            (lambda (name)
              (unless (symbol? name) (error 'live "expected a binding name" name))
              (lambda args
                (let ([import (hashtable-ref (namespace-imports space) name #f)])
                  (apply (if import
                             (namespace-ref (car import) (cdr import))
                             (top-level-value name env))
                    args))))
            env)
          (when (eq? name 'otium.generic)
            (for-each
              (lambda (entry)
                (define-top-level-value (car entry) (cdr entry) env)
                (hashtable-set! (namespace-definitions space) (car entry)
                  "Typed maps and generic dispatch.")) gm:public-bindings)
            (namespace-loaded?-set! space #t))
          (when (eq? name 'otium.dev)
            (for-each
              (lambda (entry)
                (define-top-level-value (car entry) (cdr entry) env)
                (hashtable-set! (namespace-definitions space) (car entry) "Development server primitive."))
              (dev-bindings)))
          (when (eq? name 'otium.ffi)
            (for-each
              (lambda (entry)
                (define-top-level-value (car entry) (cdr entry) env)
                (hashtable-set! (namespace-definitions space) (car entry)
                  "Portable foreign calls and native memory.")
                (when (char=? (string-ref (symbol->string (car entry)) 0) #\%)
                  (hashtable-set! (namespace-privates space) (car entry) #t)))
              ((eval 'make-native-bindings (environment '(otium ffi)))
               (lambda (member) (top-level-value member env)))))
          (when (eq? name 'otium.process)
            (for-each
              (lambda (entry)
                (define-top-level-value (car entry) (cdr entry) env)
                (hashtable-set! (namespace-definitions space) (car entry)
                  "Portable command-line programs."))
              (eval 'public-bindings
                (environment '(otium process))))
            (namespace-loaded?-set! space #t))
          (when (eq? name 'otium.reader)
            (for-each
              (lambda (entry)
                (define-top-level-value (car entry) (cdr entry) env)
                (hashtable-set! (namespace-definitions space) (car entry)
                  "Source tokens for the formatter and linter."))
              (eval 'public-bindings (environment '(otium reader))))
            (namespace-loaded?-set! space #t))
          (if (eq? name 'otium.core)
              (parameterize ([current-namespace space])
                (read-source (shared-source "core.ot")))
              (let ([core (find-namespace 'otium.core)])
                (for-each
                  (lambda (member)
                    (unless (hashtable-contains? (namespace-privates core) member)
                      (define-top-level-value member
                        (top-level-value member (namespace-environment core)) env)))
                  (vector->list (hashtable-keys (namespace-definitions core))))))
          space)))

  ;; The standard library, lib/ beside the Chez library directory.
  (define (library-root)
    (let loop ([directories (library-directories)])
      (if (null? directories) (error 'prelude "lib directory not found")
          (let ([path (string-append (caar directories) "/../lib")])
            (if (file-directory? path) path (loop (cdr directories)))))))

  (define (shared-source relative)
    (string-append (library-root) "/otium/" relative))

  (define (import-value! space local target member)
    (unless (hashtable-contains? (namespace-definitions target) member)
      (ot:fail "E0203" 'namespace (namespace-name target) 'name member))
    (when (hashtable-contains? (namespace-privates target) member)
      (ot:fail "E0204" 'namespace (namespace-name target) 'name member))
    (unless (top-level-bound? member (namespace-environment target))
      (error 'require "syntax imports are not supported yet" member))
    (when (hashtable-contains? (namespace-definitions space) local)
      (ot:fail "E0207" 'detail (format "import of `~a` would replace a local definition" local)
        'name local))
    (let ([target-name (namespace-name target)])
      (define-top-level-syntax local
        (make-variable-transformer
          (lambda (form)
            (with-syntax ([target (datum->syntax #'anchor target-name)]
                          [member (datum->syntax #'anchor member)])
              (syntax-case form (set!)
                [(set! _ value) #'(namespace-set! 'target 'member value)]
                [(_ . args) #'((namespace-ref 'target 'member) . args)]
                [id (identifier? #'id) #'(namespace-ref 'target 'member)]))))
        (namespace-environment space))
      (hashtable-set! (namespace-imports space) local (cons target-name member))))

  (define (source-for name)
    (let ([relative (list->string
                      (map (lambda (c) (if (char=? c #\.) #\/ c))
                           (string->list (symbol->string name))))])
      ;; The standard library is searched after every user directory.
      (let search ([paths (append (load-path) (list (library-root)))])
        (if (null? paths) (ot:fail "E0202" 'namespace name 'paths (apply val:array (load-path)))
            (let next-suffix ([suffixes '(".ot" ".scm")])
              (if (null? suffixes) (search (cdr paths))
                  (let ([path (string-append (car paths) "/" relative (car suffixes))])
                    (if (file-exists? path) path (next-suffix (cdr suffixes))))))))))

  (define (read-source path)
    (let ([positions (make-source-positions
                       (call-with-port (open-file-input-port path)
                         (lambda (port) (make-source-file-descriptor path port))))])
      (parameterize ([source-path path] [current-source-positions positions])
        (call-with-input-file path
          (lambda (port)
            (let loop ()
              (let ([form (read-located port path)])
                (unless (eof-object? form)
                  (evaluate-located form)
                  (loop)))))))))

  ;; Read errors become coded conditions that point into the file.
  (define (read-located port path)
    (guard (e [(and (reader-location? e) (reader-location-code e))
               (let* ([code (reader-location-code e)] [start (reader-location-start e)]
                      [end (and start (if (member code '("E0001" "E0002")) (+ start 1)
                                          (max (+ start 1) (reader-location-end e))))])
                 (ot:fail code 'span
                   (if start
                       (span-table path (reader-location-line e) (reader-location-column e) start end)
                       (void))))])
      (read-otium port path)))

  (define current-top-form (make-parameter #f))
  (define (evaluate-located form)
    (let ([source (source-of form)])
      (parameterize ([current-top-form form])
        (with-exception-handler
          (lambda (e)
            (when (ot:otium-condition? e)
              (let ([c (ot:otium-value e)])
                (when (val:table? c)
                  (if (val:nil? (val:get c (keyword "span")))
                      (locate! c)
                      (note-require! c source)))))
            (raise-continuable e))
          (lambda () (evaluate form))))))

  (define (span-table path line column start end)
    (val:table (keyword "path") (display-path path) (keyword "line") line (keyword "column") column
      (keyword "start") start (keyword "end") end))
  ;; Paths in diagnostics drop . and .. segments, and are relative to the
  ;; current directory when they lie beneath it.
  (define (display-path path)
    (let* ([absolute? (and (> (string-length path) 0) (char=? (string-ref path 0) #\/))]
           [segments (let loop ([parts (split-path path)] [out '()])
                       (cond [(null? parts) (reverse out)]
                             [(member (car parts) '("" ".")) (loop (cdr parts) out)]
                             [(and (equal? (car parts) "..") (pair? out) (not (equal? (car out) "..")))
                              (loop (cdr parts) (cdr out))]
                             [else (loop (cdr parts) (cons (car parts) out))]))]
           [joined (apply string-append
                     (if absolute? "/" "")
                     (let join ([segments segments])
                       (cond [(null? segments) '()]
                             [(null? (cdr segments)) (list (car segments))]
                             [else (cons (car segments) (cons "/" (join (cdr segments))))])))]
           [here (string-append (current-directory) "/")])
      (if (and absolute? (> (string-length joined) (string-length here))
               (string=? (substring joined 0 (string-length here)) here))
          (substring joined (string-length here) (string-length joined))
          joined)))
  (define (split-path path)
    (let loop ([i 0] [start 0] [out '()])
      (cond [(= i (string-length path)) (reverse (cons (substring path start i) out))]
            [(char=? (string-ref path i) #\/) (loop (+ i 1) (+ i 1) (cons (substring path start i) out))]
            [else (loop (+ i 1) start out)])))
  (define (source-table source)
    (span-table (source-file-descriptor-path (source-object-sfd source))
      (source-object-line source) (source-object-column source)
      (source-object-bfp source) (source-object-efp source)))

  ;; Record where condition c came from while the stack that raised it is
  ;; still there: the innermost Otium frames with source positions, else the
  ;; form an expander error names, else the top-level form being evaluated.
  (define (locate! c)
    (when (equal? (val:get c (keyword "code")) "E0201")
      (suggest-names! c))
    (let ([frames (condition-frames c)])
      (cond
        [(and (equal? (val:get c (keyword "code")) "E0201")
              (string? (val:get c (keyword "name")))
              (or (and (pair? frames) (token-in-source (cdar frames) (val:get c (keyword "name"))))
                  (let ([source (mentioning (current-top-form) (val:get c (keyword "name")))])
                    (and source (token-in-source source (val:get c (keyword "name")))))))
         => (lambda (span) (val:put! c (keyword "span") span))]
        [(pair? frames)
         (val:put! c (keyword "span") (source-table (cdar frames)))
         (val:put! c (keyword "frames")
           (apply val:array
             (map (lambda (frame)
                    (val:table (keyword "name") (or (car frame) (void))
                      (keyword "span") (source-table (cdr frame))))
                  frames)))]
        [(source-of (val:get c (keyword "form")))
         => (lambda (source) (val:put! c (keyword "span") (source-table source)))]
        [(source-of (current-top-form))
         => (lambda (source) (val:put! c (keyword "span") (source-table source)))])))

;; Close spellings of an unbound name among the names visible in the
  ;; current namespace, for "did you mean" help.
  (define (suggest-names! c)
    (let ([name (val:get c (keyword "name"))])
      (when (string? name)
        (let* ([space (current-namespace)]
               [candidates (append (map symbol->string (environment-symbols (namespace-environment space)))
                             (map symbol->string (vector->list (hashtable-keys (namespace-imports space)))))]
               [limit (if (< (string-length name) 5) 1 2)]
               [close (filter (lambda (candidate)
                                (and (not (string=? candidate name))
                                     (<= (edit-distance name candidate) limit)))
                        candidates)]
               [close (list-head (sort string<? (remove-duplicates close)) (min 3 (length (remove-duplicates close))))])
          (unless (null? close)
            (val:put! c (keyword "suggestions") (apply val:array close)))))))
  (define (remove-duplicates strings)
    (let loop ([strings strings] [out '()])
      (cond [(null? strings) (reverse out)]
            [(member (car strings) out) (loop (cdr strings) out)]
            [else (loop (cdr strings) (cons (car strings) out))])))
  (define (edit-distance a b)
    (let* ([n (string-length a)] [m (string-length b)])
      (if (> (abs (- n m)) 2) 3
          (let ([row (make-vector (+ m 1) 0)])
            (do ([j 0 (+ j 1)]) ((> j m)) (vector-set! row j j))
            (do ([i 1 (+ i 1)]) ((> i n) (vector-ref row m))
              (let loop ([j 1] [diagonal (vector-ref row 0)])
                (vector-set! row 0 i)
                (when (<= j m)
                  (let ([above (vector-ref row j)])
                    (vector-set! row j
                      (min (+ above 1) (+ (vector-ref row (- j 1)) 1)
                        (+ diagonal (if (char=? (string-ref a (- i 1)) (string-ref b (- j 1))) 0 1))))
                    (loop (+ j 1) above)))))))))

  (define (condition-frames c)
    (let ([k (ot:condition-continuation-of c)])
      (if (not k) '()
          (let loop ([i (inspect/object k)] [depth 0] [frames '()])
            (if (or (not (eq? (i 'type) 'continuation)) (= depth 200))
                (reverse frames)
                (loop (i 'link) (+ depth 1)
                  (let ([source (or (otium-source (guard (e [#t #f]) (i 'source-object)))
                                    (otium-source (guard (e [#t #f]) ((i 'code) 'source-object))))])
                    (if (and source (not (and (pair? frames) (eq? (cdar frames) source))))
                        (cons (cons (let ([code (i 'code)]) (and code (code 'name))) source) frames)
                        frames))))))))

  ;; Source objects from Otium files, which carry a line; Chez's own
  ;; libraries do not.
  (define (otium-source source)
    (and source (source-object? source) (source-object-line source) source))

  ;; The span of the first occurrence of the token name within source, or #f.
  (define (token-in-source source name)
    (let* ([path (source-file-descriptor-path (source-object-sfd source))]
           [text (guard (e [#t #f]) (call-with-input-file path get-string-all))]
           [n (string-length name)])
      (and text (<= (source-object-efp source) (string-length text))
           (let search ([i (source-object-bfp source)])
             (cond
               [(> (+ i n) (source-object-efp source)) #f]
               [(and (string=? (substring text i (+ i n)) name)
                     (or (= i 0) (token-boundary? (string-ref text (- i 1))))
                     (or (= (+ i n) (string-length text)) (token-boundary? (string-ref text (+ i n)))))
                (let count ([j 0] [line 1] [column 1])
                  (if (= j i)
                      (span-table path line column i (+ i n))
                      (if (char=? (string-ref text j) #\newline)
                          (count (+ j 1) (+ line 1) 1)
                          (count (+ j 1) line (+ column 1)))))]
               [else (search (+ i 1))])))))
  (define (token-boundary? c)
    (or (char-whitespace? c) (memv c '(#\( #\) #\[ #\] #\{ #\} #\' #\` #\, #\" #\;))))

  ;; The source of the innermost list in form that has name as an element.
  ;; An unbound reference leaves no frame behind, so this finds it instead.
  (define (mentioning form name)
    (and (pair? form)
         (or (let loop ([x form])
               (cond [(not (pair? x)) #f]
                     [(and (pair? (car x)) (mentioning (car x) name))]
                     [else (loop (cdr x))]))
             (and (let loop ([x form])
                    (cond [(not (pair? x)) #f]
                          [(and (symbol? (car x)) (string=? (symbol->string (car x)) name)) #t]
                          [else (loop (cdr x))]))
                  (source-of form)))))

  ;; An error from another file gains a note naming the top-level form here,
  ;; at source, that led to it, such as a require.
  (define (note-require! c source)
    (let ([span (val:get c (keyword "span"))])
      (when (and source (val:table? span)
                 (not (equal? (val:get span (keyword "path"))
                              (display-path (source-file-descriptor-path (source-object-sfd source))))))
        (let ([notes (val:get c (keyword "notes"))])
          (val:put! c (keyword "notes")
            (apply val:array
              (append (if (val:array? notes) (val:collection-apply list notes) '())
                (list (format "reached from ~a:~a:~a"
                        (display-path (source-file-descriptor-path (source-object-sfd source)))
                        (source-object-line source) (source-object-column source))))))))))

  ;; Print a condition that reached the top level. Otium conditions go through
  ;; otium.diagnostics; anything that fails there falls back to Chez's display.
  (define (report-condition c port)
    (let ([value (and (ot:otium-condition? c) (ot:otium-value c))] [failure #f])
      (or (and value (val:table? value)
               (guard (e [#t (set! failure e) #f])
                 (let ([render (namespace-ref (namespace-name (require-namespace 'otium.diagnostics #f))
                                 'render)])
                   (display (render value) port)
                   #t)))
          (begin
            (display-condition c port)
            (newline port)
            (when failure
              (display "note: otium.diagnostics could not render this error: " port)
              (display-condition failure port)
              (newline port))))))

  (define (load-namespace! space path)
    (when (namespace-loading? space)
      (ot:fail "E0205" 'namespace (namespace-name space)))
    (dynamic-wind
      (lambda () (namespace-loading?-set! space #t))
      (lambda ()
        (when dev-active? (dev-call 'record-load! (namespace-name space) path))
        (parameterize ([current-namespace space])
          (read-source path))
        (namespace-source-set! space path)
        (namespace-loaded?-set! space #t))
      (lambda () (namespace-loading?-set! space #f))))

  (define (require-namespace name reload?)
    (let ([space (find-namespace name)])
      (when (namespace-loading? space)
        (ot:fail "E0205" 'namespace name))
      (when (and (not (memq name '(otium.generic otium.process otium.reader)))
                 (or reload? (not (namespace-loaded? space))))
        (load-namespace! space (or (namespace-source space) (source-for name))))
      space))

  (define (load-source path)
    ;; A script may switch namespaces without changing its caller's namespace.
    (parameterize ([current-namespace (current-namespace)])
      (read-source path)))

  ;; (require chez) makes every Chez binding available as chez/name in the
  ;; current namespace. The import keyword is installed under a fresh name.
  (define (require-chez! space)
    (let ([env (namespace-environment space)] [keyword (gensym "import")])
      (define-top-level-syntax keyword (top-level-syntax 'import (scheme-environment)) env)
      (eval `(,keyword (prefix (chezscheme) chez/)) env)))

  (define (require-form specs)
    (for-each (lambda (spec) (require-spec (if (symbol? spec) (list spec) spec))) specs))

  (define (require-spec args)
    (when (and (pair? args) (eq? (car args) 'chez))
      (unless (null? (cdr args)) (error 'require "chez takes no options" args)))
    (unless (and (list? args) (pair? args) (symbol? (car args)))
      (error 'require "expected (require namespace [:as alias] [:refer (names)] [:reload])"))
    (if (eq? (car args) 'chez)
        (require-chez! (current-namespace))
    (let ([name (car args)] [space (current-namespace)])
      (let parse ([rest (cdr args)] [alias (car args)] [refers '()] [reload? #f])
        (cond
          [(null? rest)
           (let ([target (require-namespace name reload?)])
             (for-each
               (lambda (member)
                 (when (and (top-level-bound? member (namespace-environment target))
                            (not (hashtable-contains? (namespace-privates target) member)))
                   ;; Macro expansions may name their defining module even when
                   ;; the caller required it under an alias.
                   (unless (eq? alias name)
                     (import-value! space
                       (string->symbol (string-append (symbol->string name) "/"
                                         (symbol->string member)))
                       target member))
                   (import-value! space
                     (string->symbol (string-append (symbol->string alias) "/"
                                       (symbol->string member)))
                     target member)))
               (vector->list (hashtable-keys (namespace-definitions target))))
             (for-each (lambda (member) (import-value! space member target member)) refers))]
          [(or (eq? (car rest) ':reload) (eq? (car rest) (keyword "reload"))) (parse (cdr rest) alias refers #t)]
          [(and (or (eq? (car rest) ':as) (eq? (car rest) (keyword "as"))) (pair? (cdr rest)) (symbol? (cadr rest)))
           (parse (cddr rest) (cadr rest) refers reload?)]
          [(and (or (eq? (car rest) ':refer) (eq? (car rest) (keyword "refer"))) (pair? (cdr rest))
                (list? (cadr rest)) (for-all symbol? (cadr rest)))
           (parse (cddr rest) alias (cadr rest) reload?)]
          [else (error 'require "invalid options" rest)])))))

  (define (declaration-option? value name)
    (or (eq? value (keyword name))
        (eq? value (string->symbol (string-append ":" name)))))

  (define (class-declaration! space form)
    (define (invalid message . details)
      (apply gm:fail (car form) 'invalid-class-definition message details))
    (define env (namespace-environment space))
    (define name (and (pair? (cdr form)) (cadr form)))
    (define abstract? (eq? (car form) 'defabstract))
    (define reset? (eq? (car form) 'reset-class!))
    (unless (and (symbol? name) (not (memv #\/ (string->list (symbol->string name)))))
      (invalid "expected local class name"))
    (let* ([rest (cddr form)]
           [parent (if (and (pair? rest) (declaration-option? (car rest) "is"))
                       (begin (unless (and (pair? (cdr rest)) (symbol? (cadr rest)))
                                (invalid "expected abstract parent"))
                              (eval (cadr rest) env)) gm:Object)]
           [rest (if (and (pair? rest) (declaration-option? (car rest) "is")) (cddr rest) rest)]
           [accessors '()]
           [slots
             (map (lambda (slot)
                    (let* ([slot-name (if (symbol? slot) slot (and (pair? slot) (car slot)))]
                           [options (if (symbol? slot) '() (and (pair? slot) (cdr slot)))]
                           [init #f] [accessor #f] [state? #f] [seen '()])
                      (unless (and (symbol? slot-name) (list? options) (even? (length options)))
                        (invalid "expected slot symbol and option/value pairs" slot))
                      (let loop ([options options])
                        (unless (null? options)
                          (let ([key (car options)] [value (cadr options)])
                            (when (memq key seen) (invalid "duplicate slot option" key))
                            (set! seen (cons key seen))
                            (cond [(declaration-option? key "init")
                                   (set! init (compile-form (expander-top (namespace-expander space) `(fn () ,value)) env))]
                                  [(declaration-option? key "accessor")
                                   (unless (and (symbol? value)
                                                (not (memv #\/ (string->list (symbol->string value)))))
                                     (invalid "expected local accessor name" value))
                                   (set! accessor value)]
                                  [(declaration-option? key "state")
                                   (unless (boolean? value) (invalid "expected boolean state flag" value))
                                   (set! state? value)]
                                  [else (invalid "unknown slot option" key)]))
                          (loop (cddr options))))
                      (when (and state? (not init)) (invalid "state slot requires initializer" slot-name))
                      (let ([key (keyword (symbol->string slot-name))])
                        (when accessor (set! accessors (cons (cons accessor key) accessors)))
                        (list key init state?)))) rest)]
           [defined? (hashtable-contains? (namespace-definitions space) name)]
           [bound? (top-level-bound? name env)]
           [owner (cons (namespace-name space) name)]
           [old (and bound? (top-level-value name env))])
      (when (or (eq? (namespace-name space) 'otium.generic)
                (hashtable-contains? (namespace-imports space) name)
                (and reset? (not defined?))
                (and (not defined?) (or bound? (memq name (environment-symbols env))))
                (and defined? (not reset?)
                     (not (and (gm:descriptor? old) (equal? owner (gm:descriptor-owner old))
                               (eq? abstract? (gm:descriptor-abstract? old))))))
        (gm:fail (car form) 'binding-conflict "class declaration conflicts with binding" name))
      (when (and abstract? (pair? slots)) (invalid "abstract types cannot have slots"))
      (for-each (lambda (entry)
                  (let ([accessor (car entry)])
                    (when (or (eq? accessor name)
                              (hashtable-contains? (namespace-imports space) accessor)
                              (and (top-level-bound? accessor env)
                                   (let ([g (top-level-value accessor env)])
                                     (not (and (gm:generic? g) (= 1 (gm:generic-arity g))
                                               (equal? (cons (namespace-name space) accessor)
                                                       (gm:generic-owner g)))))))
                      (gm:fail 'defclass 'binding-conflict "accessor conflicts with binding" accessor))))
        accessors)
      (let ([type (if (and defined? (not reset?)) old (gm:fresh-type (namespace-name space) name))])
        (gm:redefine-class! type abstract? parent slots)
        (define-top-level-value name type env)
        (hashtable-set! (namespace-definitions space) name "Class or abstract dispatch type.")
        (for-each (lambda (entry)
                    (let* ([name (car entry)] [key (cdr entry)]
                           [g (if (top-level-bound? name env) (top-level-value name env)
                                  (gm:fresh-generic (namespace-name space) name 1))])
                      (define-top-level-value name g env)
                      (hashtable-set! (namespace-definitions space) name "Slot accessor generic.")
                      (gm:add-method! g (list type) (lambda (object) (gm:slot-ref object key)))))
          accessors))))

  (define (generic-declaration! space form)
    (define (invalid message)
      (gm:fail (car form) 'invalid-declaration message form))
    (define (identifier? x)
      (and (symbol? x) (not (memv #\/ (string->list (symbol->string x))))))
    (define (parameters! parameters specialized?)
      (unless (list? parameters) (invalid "expected fixed parameter list"))
      (let ([names (map (lambda (p)
                         (cond [(symbol? p) p]
                               [(and specialized? (list? p) (= (length p) 2)
                                     (symbol? (car p)) (symbol? (cadr p))) (car p)]
                               [else (invalid "invalid parameter")])) parameters)])
        (let loop ([rest names])
          (unless (null? rest)
            (when (eq? (car rest) '&) (invalid "generic parameters have fixed arity"))
            (when (memq (car rest) (cdr rest)) (invalid "duplicate parameter"))
            (loop (cdr rest))))
        names))
    (define env (namespace-environment space))
    (define kind (car form))
    (unless (and (list? form) (>= (length form) 2))
      (invalid "malformed declaration"))
    (if (eq? kind 'defmethod)
        (let* ([qualifier (if (declaration-option? (cadr form) "around") (keyword "around") 'primary)]
               [form (if (eq? qualifier 'primary) form (cons 'defmethod (cddr form)))]
               [head (cadr form)])
          (unless (and (pair? head) (list? head) (symbol? (car head))
                       (>= (length form) 3))
            (invalid "expected (defmethod (generic parameters ...) body ...)"))
          (let* ([names (parameters! (cdr head) #t)]
                 [generic (eval (car head) env)])
            (unless (gm:generic? generic)
              (gm:fail kind 'not-generic "expected generic procedure" (car head)))
            (unless (= (length names) (gm:generic-arity generic))
              (gm:fail kind 'wrong-arity "method arity differs from generic" (car head)))
            (let* ([signature
                    (map (lambda (p)
                           (and (pair? p)
                                (let ([type (eval (cadr p) env)])
                                  (unless (gm:descriptor? type)
                                    (gm:fail kind 'invalid-specializer
                                      "expected type descriptor" (cadr p)))
                                  type))) (cdr head))]
                   [implementation
                    (compile-form
                      (expander-top (namespace-expander space)
                        (copy-source! form `(fn ,names ,@(cddr form))))
                      env)])
              (gm:add-method! generic signature implementation qualifier))))
        (if (memq kind '(defclass defabstract reset-class!))
            (class-declaration! space form)
        (let* ([reset? (eq? kind 'reset-generic!)]
               [head (cadr form)]
               [name (and (pair? head) (car head))])
          (unless (and (= (length form) 2) (identifier? name))
            (invalid "expected local declaration name"))
          (let* ([arity (length (parameters! (cdr head) #f))]
                 [owner (cons (namespace-name space) name)]
                 [defined? (hashtable-contains? (namespace-definitions space) name)]
                 [bound? (top-level-bound? name env)])
            (when (or (eq? (namespace-name space) 'otium.generic)
                      (hashtable-contains? (namespace-imports space) name)
                      (and reset? (not defined?))
                      (and (not reset?) (not defined?)
                           (or bound? (memq name (environment-symbols env)))))
              (gm:fail kind 'binding-conflict "declaration would replace another binding" name))
            (cond
              [(and defined? (not reset?))
               (let ([old (and bound? (top-level-value name env))])
                 (unless (and (gm:generic? old) (equal? owner (gm:generic-owner old))
                              (= arity (gm:generic-arity old)))
                   (gm:fail kind 'binding-conflict
                     "incompatible declaration; use explicit reset to replace it" name)))]
              [else
               (define-top-level-value name
                 (gm:fresh-generic (namespace-name space) name arity) env)
               (hashtable-set! (namespace-definitions space) name
                 "Generic procedure.")]))))))

  ;; Code read from a file carries Chez source objects, so compiled code knows
  ;; where each expression came from. Quoted data is left as it is.
  (define (annotate x)
    (if (current-source-positions) (annotate-form x) x))
  (define (annotate-form x)
    (if (pair? x)
        (let ([source (source-of x)])
          (when source (locate-call! x))
          (let ([inner (if (eq? (car x) 'quote) x (annotate-tail x))])
            (if source (make-annotation inner source x) inner)))
        x))
  ;; An application lowers to a macro whose last operand is the procedure
  ;; call; it takes the application's position.
  (define (locate-call! x)
    (let ([call (car (last-pair x))])
      (when (and (pair? call) (eq? (car call) call-function-name))
        (copy-source! x call))))
  (define (annotate-tail x)
    (if (pair? x) (cons (annotate-form (car x)) (annotate-tail (cdr x))) x))
  (define (compile-form form env) (eval (annotate form) env))

  (define (evaluate form)
    (ot:call-with-otium-control
      (lambda ()
        (ot:call-with-native-conditions
          (lambda () (with-expansion (lambda () (evaluate-top form))))))))

  (define (evaluate-top input)
    (let* ([space (current-namespace)] [env (namespace-environment space)]
           [expander (namespace-expander space)]
           [form (expander-head expander input #f)])
      (cond
        [(and (pair? form)
              (memq (car form) '(defclass defabstract defgeneric defmethod reset-class! reset-generic!)))
         (generic-declaration! space form)]
        [(and (pair? form) (eq? (car form) 'ns))
         (unless (and (list? form) (>= (length form) 2) (symbol? (cadr form)))
           (error 'ns "expected (ns name)" form))
         (when (and (namespace-loading? space) (not (eq? (cadr form) (namespace-name space))))
           (ot:fail "E0206" 'declared (cadr form) 'namespace (namespace-name space)))
         (current-namespace (find-namespace (cadr form)))
         (for-each (lambda (clause) (require-form (cdr clause))) (cddr form))]
        [(and (pair? form) (eq? (car form) 'require)) (require-form (cdr form))]
        [(and (pair? form) (eq? (car form) 'begin))
         (unless (list? form) (error 'begin "expected a proper list" form))
         (let loop ([forms (cdr form)])
           (cond [(null? forms) (void)]
                 [(null? (cdr forms)) (evaluate (car forms))]
                 [else (evaluate (car forms)) (loop (cdr forms))]))]
        [(and (pair? form) (eq? (car form) 'defonce))
         (unless (and (list? form) (<= 3 (length form) 4) (symbol? (cadr form)))
           (error 'defonce "expected (defonce name [docstring] expression)" form))
         (unless (hashtable-contains? (namespace-definitions space) (cadr form))
           (evaluate `(def ,@(cdr form))))]
        [(and (pair? form) (eq? (car form) 'defmacro))
         (let ([form (expander-top expander form)])
           (when (hashtable-contains? (namespace-imports space) (cadr form))
             (ot:fail "E0207" 'detail (format "definition of `~a` would replace an import" (cadr form))
               'name (cadr form)))
           (let ([transformer
                  (compile-form (copy-source! form `(fn ,(caddr form)
                           ,@(let ([body (cdddr form)])
                               (if (and (string? (car body)) (pair? (cdr body)))
                                   (cdr body) body))))
                        env)])
             (define-top-level-value (cadr form)
               (make-otium-macro transformer space (cadr form)) env)
             (hashtable-delete! (namespace-privates space) (cadr form))
             (hashtable-set! (namespace-definitions space) (cadr form) (definition-help form))))]
        [else
         (let* ([form (expander-top expander form)]
                [kind (and (pair? form) (memq (car form) '(def def-)) (car form))]
                [definition (and kind (if (pair? (cadr form)) (caadr form) (cadr form)))])
           (when (and definition (hashtable-contains? (namespace-imports space) definition))
             (ot:fail "E0207" 'detail (format "definition of `~a` would replace an import" definition)
               'name definition))
           (call-with-values
             (lambda () (compile-form form env))
             (lambda results
               (when definition
                 (if (eq? kind 'def-)
                     (hashtable-set! (namespace-privates space) definition #t)
                     (hashtable-delete! (namespace-privates space) definition))
                 (hashtable-set! (namespace-definitions space) definition (definition-help form)))
               (apply values results))))])))

  (define (definition-help form)
    (let ([body (if (eq? (car form) 'defmacro) (cdddr form) (cddr form))])
      (if (and (pair? body) (string? (car body)) (pair? (cdr body)))
          (car body) "")))

  (define (describe-binding space name)
    (let* ([import (hashtable-ref (namespace-imports space) name #f)]
           [owner (if import (find-namespace (car import)) space)]
           [member (if import (cdr import) name)]
           [local? (hashtable-contains? (namespace-definitions owner) member)]
           [env (namespace-environment owner)]
           [core (find-namespace 'otium.core)]
           [known? (or local? (top-level-bound? member env))]
           [text (binding-documentation owner member)])
      (cond
        [known?
         (string-append
           (symbol->string (namespace-name (if (or local? import) owner core)))
           "/" (symbol->string member) ": "
           (if (string=? text "") "no documentation" text))]
        [(not (string=? text "")) text]
        [else (void)])))

  (define (binding-documentation space name)
    (let* ([import (hashtable-ref (namespace-imports space) name #f)]
           [owner (if import (find-namespace (car import)) space)]
           [member (if import (cdr import) name)]
           [definitions (namespace-definitions owner)])
      (if (hashtable-contains? definitions member)
          (hashtable-ref definitions member "")
          (let ([core (find-namespace 'otium.core)])
            (if (hashtable-contains? (namespace-definitions core) member)
                (hashtable-ref (namespace-definitions core) member "")
                (runtime-documentation member))))))

  (define (repl-bindings)
    (let ([space (current-namespace)])
      (map (lambda (name)
             (cons (symbol->string name) (binding-documentation space name)))
           (append '(ns require defonce defmacro)
                   (environment-symbols (namespace-environment space))))))

  (define (repl)
    (when (and dev-active? (line-editor-available?)) (line-editor-service! dev-service!))
    (let* ([editing? (line-editor-available?)]
           [continuation? #f]
           [prompt (lambda ()
                     (let ([text (if continuation? "..> "
                                     (string-append
                                       (symbol->string (namespace-name (current-namespace)))
                                       "> "))])
                       (set! continuation? #t)
                       text))]
           [input (if editing? (make-line-input-port prompt repl-bindings) (current-input-port))])
      (when editing?
        (display "otium repl (chez) - ^C interrupts, ^D exits, ^X ^E opens $EDITOR, Tab completes, ,help for help\n"))
      (dynamic-wind
        (lambda () (void))
        (lambda ()
          (let loop ()
            (set! continuation? #f)
            (unless editing?
              (display (namespace-name (current-namespace)))
              (display "> ")
              (flush-output-port (current-output-port)))
            (let ([continue?
                   (guard (c [(and (ot:control-condition? c) (eq? (ot:control-kind c) 'exit))
                              (raise c)]
                             [else
                              (report-condition c (current-error-port))
                              (when editing?
                                (close-port input)
                                (set! input (make-line-input-port prompt repl-bindings)))
                              #t])
                     (let ([form (read-otium input "<repl>")])
                       (if (eof-object? form) #f
                           (begin
                             (call-with-values (lambda () (evaluate form))
                               (lambda values
                                 (for-each
                                   (lambda (v) (otium-write v) (newline))
                                   values)))
                             #t))))])
              (when continue? (loop)))))
        (lambda ()
          (when editing? (close-port input) (line-editor-close))))))

  ;; Keep frames for non-tail primitive calls, so errors locate the expression.
  (debug-level 2)
  (current-namespace (find-namespace 'user)))
