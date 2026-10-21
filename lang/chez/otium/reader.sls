(library (otium reader)
  (export read-otium read-string read-tokens reader-incomplete? public-bindings
          make-source-positions current-source-positions source-of copy-source!
          reader-location? reader-location-code reader-location-line reader-location-column
          reader-location-start reader-location-end)
  (import (chezscheme) (only (otium values) keyword))

  (define-condition-type &reader-incomplete &error
    make-reader-incomplete reader-incomplete?)
  ;; Attached to read errors: the diagnostic code, and with source positions
  ;; on, where the problem starts and the character offset where reading stopped.
  (define-condition-type &reader-location &condition
    make-reader-location reader-location?
    (code reader-location-code) (line reader-location-line) (column reader-location-column)
    (start reader-location-start) (end reader-location-end))

  ;; Tool mode: when set, the reader records each token with its source
  ;; position. Ordinary reads leave it unset and allocate nothing extra.
  (define-record-type token-log
    (fields (mutable line) (mutable column) (mutable newlines)
            (mutable mark) (mutable tokens)))
  (define current-token-log (make-parameter #f))

  ;; Source mode: when set, the reader records a Chez source object for every
  ;; list it reads, keyed weakly by the pair, so the runtime can annotate code
  ;; before compiling it. Position counters persist across reads of one file.
  (define-record-type source-positions
    (fields sfd table (mutable offset) (mutable line) (mutable column))
    (protocol (lambda (new) (lambda (sfd) (new sfd (make-weak-eq-hashtable) 0 1 1)))))
  (define current-source-positions (make-parameter #f))
  (define (source-of x)
    (let ([positions (current-source-positions)])
      (and positions (pair? x) (eq-hashtable-ref (source-positions-table positions) x #f))))
  ;; Give to the position of from, unless to already has one. Returns to.
  (define (copy-source! from to)
    (let ([positions (current-source-positions)])
      (when (and positions (pair? to) (pair? from))
        (let ([table (source-positions-table positions)])
          (unless (eq-hashtable-contains? table to)
            (let ([source (eq-hashtable-ref table from #f)])
              (when source (eq-hashtable-set! table to source))))))
      to))

  (define (read-otium port . source)
    (define consumed 0)
    (define name (if (null? source) (port-name port) (car source)))
    (define log (current-token-log))
    (define positions (current-source-positions))
    (define (here)
      (and positions
           (list (source-positions-offset positions) (source-positions-line positions)
                 (source-positions-column positions))))
    (define (fail message incomplete? code . start)
      (raise (condition (make-who-condition 'read)
               (let ([start (if (and (pair? start) (car start)) (car start) (here))])
                 (if start
                     (make-reader-location code (cadr start) (caddr start) (car start)
                       (source-positions-offset positions))
                     (make-reader-location code #f #f #f #f)))
               (make-message-condition message)
               (make-irritants-condition (list name (if (port-has-port-position? port)
                                                       (port-position port) consumed)))
               (if incomplete? (make-reader-incomplete) (make-error)))))
    (define (peek) (peek-char port))
    (define (take)
      (let ([c (read-char port)])
        (unless (eof-object? c)
          (set! consumed (+ consumed 1))
          (when positions
            (source-positions-offset-set! positions (+ (source-positions-offset positions) 1))
            (if (or (eqv? c #\newline)
                    (and (eqv? c #\return) (not (eqv? (peek) #\newline))))
                (begin (source-positions-line-set! positions (+ (source-positions-line positions) 1))
                       (source-positions-column-set! positions 1))
                (source-positions-column-set! positions (+ (source-positions-column positions) 1))))
          (when log
            (if (or (eqv? c #\newline)
                    (and (eqv? c #\return) (not (eqv? (peek) #\newline))))
                (begin (token-log-line-set! log (+ (token-log-line log) 1))
                       (token-log-column-set! log 0)
                       (token-log-newlines-set! log (+ (token-log-newlines log) 1)))
                (token-log-column-set! log (+ (token-log-column log) 1)))))
        c))
    (define (start-token)
      (when log
        (token-log-mark-set! log
          (list (port-position port) (token-log-line log) (token-log-column log)
                (token-log-newlines log)))))
    (define (end-token kind)
      (when log
        (let ([mark (token-log-mark log)])
          (token-log-tokens-set! log
            (cons (list kind (car mark) (port-position port) (cadr mark) (caddr mark)
                        (cadddr mark))
                  (token-log-tokens log)))
          (token-log-newlines-set! log 0))))
    (define (delimiter? c)
      (or (eof-object? c) (memv c '(#\space #\tab #\newline #\return #\page #\vtab
                                  #\( #\) #\[ #\] #\{ #\} #\; #\" #\, #\' #\`))))
    (define (space)
      (let loop ()
        (let ([c (peek)])
          (cond [(and (char? c) (memv c '(#\space #\tab #\newline #\return #\page #\vtab)))
                 (take) (loop)]
                [(eqv? c #\;)
                 (start-token)
                 (let comment ()
                   (let ([c (peek)])
                     (unless (or (eof-object? c) (memv c '(#\newline #\return)))
                       (take)
                       (comment))))
                 (end-token 'comment)
                 (loop)]))))
    (define (hex c)
      (cond [(char<=? #\0 c #\9) (- (char->integer c) 48)]
            [(char<=? #\a c #\f) (+ 10 (- (char->integer c) 97))]
            [(char<=? #\A c #\F) (+ 10 (- (char->integer c) 65))]
            [else #f]))
    (define (string-literal)
      (define start (here))
      (start-token)
      (take)
      (let loop ([out '()])
        (let ([c (take)])
          (cond [(eof-object? c) (fail "unterminated string" #t "E0002" start)]
                [(eqv? c #\") (end-token 'string) (list->string (reverse out))]
                [(eqv? c #\return)
                 (when (eqv? (peek) #\newline) (take))
                 (loop (cons #\newline out))]
                [(eqv? c #\\)
                 (let* ([c (take)]
                        [escaped
                         (case c
                           [(#\n) #\newline] [(#\r) #\return] [(#\t) #\tab]
                           [(#\0) #\nul] [(#\e) #\esc] [(#\" #\\) c]
                           [(#\x)
                            (let digits ([n 0] [count 0])
                              (let ([c (take)])
                                (cond [(eof-object? c) (fail "unterminated hex escape" #t "E0002" start)]
                                      [(eqv? c #\;)
                                       (when (or (= count 0) (> n #x10ffff) (<= #xd800 n #xdfff))
                                         (fail "invalid Unicode scalar" #f "E0006"))
                                       (integer->char n)]
                                      [(and (hex c) (<= n #x10ffff))
                                       (digits (+ (* n 16) (hex c)) (+ count 1))]
                                      [else (fail "invalid hex escape" #f "E0006")])))]
                           [else (fail "unknown string escape" (eof-object? c) "E0006")])])
                   (loop (cons escaped out)))]
                [else (loop (cons c out))]))))
    (define (atom)
      (define token-start (here))
      (start-token)
      (let* ([text (list->string
                     (let loop ([out '()])
                       (if (delimiter? (peek)) (reverse out) (loop (cons (take) out)))))]
             [n (string-length text)])
        (define (at i) (and (< i n) (string-ref text i)))
        (define (digit? c) (and c (char<=? #\0 c #\9)))
        (define start (if (memv (at 0) '(#\+ #\-)) 1 0))
        (define (digits i)
          (if (digit? (at i)) (digits (+ i 1)) i))
        (end-token 'atom)
        (cond [(= n 0) (fail "expected value" #f "E0010" token-start)]
              [(string=? text "nil") (void)]
              [(member text '("#t" "#true")) #t]
              [(member text '("#f" "#false")) #f]
              [(eqv? (at 0) #\#) (fail "reserved # token" #f "E0005" token-start)]
              [(eqv? (at 0) #\:)
               (when (= n 1) (fail "empty keyword" #f "E0008" token-start))
               (keyword (substring text 1 n))]
              [(or (digit? (at start)) (and (eqv? (at start) #\.) (< (+ start 1) n)))
               (if (and (eqv? (at start) #\0) (memv (at (+ start 1)) '(#\x #\X)))
                   (let ([begin (+ start 2)])
                     (unless (and (< begin n)
                                  (for-all hex (string->list (substring text begin n))))
                       (fail "invalid number" #f "E0004" token-start))
                     (* (if (eqv? (at 0) #\-) -1 1)
                        (string->number (substring text begin n) 16)))
                   (let* ([end (digits start)]
                          [fraction? (eqv? (at end) #\.)]
                          [fraction-end (if fraction? (digits (+ end 1)) end)]
                          [exponent? (memv (at fraction-end) '(#\e #\E))]
                          [exponent-start (+ fraction-end 1)]
                          [exponent-start (if (and exponent? (memv (at exponent-start) '(#\+ #\-)))
                                              (+ exponent-start 1) exponent-start)]
                          [finish (if exponent? (digits exponent-start) fraction-end)])
                     (unless (and (= finish n)
                                  (or (> end start) (and fraction? (> fraction-end (+ end 1))))
                                  (or (not exponent?) (> finish exponent-start)))
                       (fail "invalid number" #f "E0004" token-start))
                     (or (string->number text) (fail "invalid number" #f "E0004" token-start))))]
              [else (string->symbol text)])))
    (define (delimiter kind)
      (start-token)
      (take)
      (end-token kind))
    (define (sequence closing prefix)
      (define start (here))
      (delimiter 'open)
      (let loop ([out (if prefix (list prefix) '())])
        (space)
        (cond [(eof-object? (peek)) (fail "unclosed form" #t "E0001" start)]
              [(eqv? (peek) closing) (delimiter 'close) (reverse out)]
              [else
               (let ([x (form)])
                 (if (eq? x (string->symbol "."))
                     (begin
                       (when (null? out) (fail "dot without a preceding value" #f "E0007"))
                       (space)
                       (when (eof-object? (peek)) (fail "unclosed dotted pair" #t "E0001" start))
                       (let ([tail (form)])
                         (space)
                         (unless (eqv? (peek) closing)
                           (fail "content after dotted tail" (eof-object? (peek)) "E0007"))
                         (delimiter 'close)
                         (fold-left (lambda (tail x) (cons x tail)) tail out)))
                     (loop (cons x out))))])))
    (define (abbreviation name)
      (space)
      (when (eof-object? (peek)) (fail "quote without a value" #t "E0009"))
      (list name (form)))
    (define (form)
      (space)
      (if positions
          (let* ([offset (source-positions-offset positions)]
                 [line (source-positions-line positions)]
                 [column (source-positions-column positions)]
                 [x (datum)])
            (when (pair? x)
              (eq-hashtable-set! (source-positions-table positions) x
                (make-source-object (source-positions-sfd positions) offset
                  (source-positions-offset positions) line column)))
            x)
          (datum)))
    (define (datum)
      (case (peek)
        [(#\() (sequence #\) #f)] [(#\[) (sequence #\] 'array)]
        [(#\{) (sequence #\} 'table)] [(#\") (string-literal)]
        [(#\' ) (delimiter 'prefix) (abbreviation 'quote)]
        [(#\`) (delimiter 'prefix) (abbreviation 'quasiquote)]
        [(#\,) (start-token) (take)
         (abbreviation (if (eqv? (peek) #\@)
                           (begin (take) (end-token 'prefix) 'unquote-splicing)
                           (begin (end-token 'prefix) 'unquote)))]
        [(#\) #\] #\}) (let ([start (here)]) (take) (fail "unexpected closing delimiter" #f "E0003" start))]
        [else (if (eof-object? (peek)) (peek) (atom))]))
    (form))

  (define (read-string text)
    (define (fail message)
      (raise (condition (make-who-condition 'read-string) (make-message-condition message)
               (make-reader-location "E0010" #f #f #f #f) (make-error))))
    (let* ([port (open-string-input-port text)] [value (read-otium port "<string>")])
      (when (eof-object? value) (fail "empty input"))
      (unless (eof-object? (read-otium port "<string>")) (fail "trailing datum"))
      value))

  ;; Read every form in text, returning its tokens in source order. Each token
  ;; is (kind text line column newlines): kind is :open, :close, :atom, :string,
  ;; :comment or :prefix; lines and columns count from zero; newlines counts the
  ;; line breaks between this token and the previous one. Reading still
  ;; validates the source, so text that fails read-string fails here too.
  (define (read-tokens text)
    (let ([port (open-string-input-port text)] [log (make-token-log 0 0 0 #f '())])
      (parameterize ([current-token-log log])
        (let loop ()
          (unless (eof-object? (read-otium port "<string>")) (loop))))
      (map (lambda (token)
             (list (keyword (symbol->string (car token)))
                   (substring text (cadr token) (caddr token))
                   (list-ref token 3) (list-ref token 4) (list-ref token 5)))
           (reverse (token-log-tokens log)))))

  ;; The otium.reader namespace, used by the formatter and linter.
  (define public-bindings (list (cons 'tokens read-tokens))))
