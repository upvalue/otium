(library (otium bytevectors)
  (export (rename (ot-bytevector bytevector) (ot-make-bytevector make-bytevector)
                  (ot-bytevector-copy bytevector-copy) (ot-bytevector-copy! bytevector-copy!)
                  (ot-utf8->string utf8->string) (ot-string->utf8 string->utf8))
          bytevector-append)
  (import (chezscheme) (prefix (otium conditions) ot:) (prefix (otium values) val:))

  ;; R7RS bytevector operations over Chez bytevectors. Chez's own versions
  ;; take R6RS arguments or accept signed fill bytes.
  (define (byte? x) (and (fixnum? x) (fx<= 0 x 255)))
  (define (ot-bytevector . bytes)
    (unless (for-all byte? bytes) (ot:error "bytevector: expected bytes from 0 to 255"))
    (u8-list->bytevector bytes))
  (define ot-make-bytevector
    (case-lambda
      [(k) (ot-make-bytevector k 0)]
      [(k fill)
       (unless (and (fixnum? k) (fx>= k 0))
         (ot:error "make-bytevector: expected a non-negative length"))
       (unless (byte? fill) (ot:error "make-bytevector: expected a fill byte from 0 to 255"))
       (make-bytevector k fill)]))
  ;; Resolve optional start and end against length, as C does.
  (define (span who size rest)
    (when (> (length rest) 2) (ot:error (format "~a: too many arguments" who)))
    (let* ([start (if (pair? rest) (car rest) 0)]
           [end (if (and (pair? rest) (pair? (cdr rest))) (cadr rest) size)])
      (unless (and (fixnum? start) (fx<= 0 start size)) (ot:error (format "~a: start out of range" who)))
      (unless (and (fixnum? end) (fx<= start end size)) (ot:error (format "~a: end out of range" who)))
      (values start end)))
  (define (check-bytevector who x)
    (unless (bytevector? x) (ot:error (format "~a: expected bytevector" who))))
  (define (ot-bytevector-copy b . rest)
    (check-bytevector 'bytevector-copy b)
    (let-values ([(start end) (span 'bytevector-copy (bytevector-length b) rest)])
      (let ([copy (make-bytevector (fx- end start))])
        (bytevector-copy! b start copy 0 (fx- end start))
        copy)))
  (define (ot-bytevector-copy! to at from . rest)
    (check-bytevector 'bytevector-copy! to)
    (check-bytevector 'bytevector-copy! from)
    (let-values ([(start end) (span 'bytevector-copy! (bytevector-length from) rest)])
      (unless (and (fixnum? at) (fx<= 0 at (bytevector-length to))
                   (fx<= (fx- end start) (fx- (bytevector-length to) at)))
        (ot:error "bytevector-copy!: destination out of range"))
      (bytevector-copy! from start to at (fx- end start))
      (void)))
  (define (bytevector-append . parts)
    (for-each (lambda (b) (check-bytevector 'bytevector-append b)) parts)
    (let ([out (make-bytevector (apply + (map bytevector-length parts)))])
      (let loop ([parts parts] [at 0])
        (if (null? parts) out
            (let ([n (bytevector-length (car parts))])
              (bytevector-copy! (car parts) 0 out at n)
              (loop (cdr parts) (fx+ at n)))))))

  ;; The length of the valid UTF-8 sequence at b[i..end), or 0. The same rules
  ;; as the C runtime, so both reject the same input.
  (define (sequence-length b i end)
    (let ([byte (bytevector-u8-ref b i)])
      (define (continuation? k) (fx= (fxand (bytevector-u8-ref b k) #xc0) #x80))
      (cond
        [(fx< byte #x80) 1]
        [else
         (let ([width (cond [(fx<= #xc2 byte #xdf) 2] [(fx<= #xe0 byte #xef) 3]
                            [(fx<= #xf0 byte #xf4) 4] [else 0])])
           (if (or (fx= width 0) (fx> width (fx- end i))) 0
               (let ([second (bytevector-u8-ref b (fx+ i 1))]
                     [low (case byte [(#xe0) #xa0] [(#xf0) #x90] [else #x80])]
                     [high (case byte [(#xed) #x9f] [(#xf4) #x8f] [else #xbf])])
                 (if (and (fx<= low second high)
                          (let loop ([k 2])
                            (or (fx= k width) (and (continuation? (fx+ i k)) (loop (fx+ k 1))))))
                     width 0))))])))
  (define (ot-utf8->string b . rest)
    (check-bytevector 'utf8->string b)
    (let-values ([(start end) (span 'utf8->string (bytevector-length b) rest)])
      (let loop ([i start])
        (when (fx< i end)
          (let ([n (sequence-length b i end)])
            (when (fx= n 0)
              (ot:error (val:table (val:keyword "type") 'decode-error
                                   (val:keyword "message")
                                   (format "utf8->string: invalid UTF-8 at byte ~a" i)
                                   (val:keyword "op") 'utf8->string
                                   (val:keyword "position") i)))
            (loop (fx+ i n)))))
      ;; Valid input decodes the same way with Chez's decoder.
      (utf8->string (if (and (fx= start 0) (fx= end (bytevector-length b))) b
                        (ot-bytevector-copy b start end)))))
  (define (ot-string->utf8 s . rest)
    (unless (string? s) (ot:error "string->utf8: expected string"))
    (let-values ([(start end) (span 'string->utf8 (string-length s) rest)])
      (string->utf8 (if (and (fx= start 0) (fx= end (string-length s))) s (substring s start end))))))
