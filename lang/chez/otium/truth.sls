(library (otium truth)
  (export (rename (ot-if if) (ot-and and) (ot-or or) (ot-cond cond)
                  (ot-when when) (ot-unless unless)
                  (ot-not not) (ot-filter filter) (ot-exists exists) (ot-for-all for-all)))
  (import (chezscheme) (prefix (otium values) val:))
  (define-syntax truth
    (syntax-rules () [(_ x) (let ([v x]) (and (not (eq? v #f)) (not (eq? v (void)))))]))
  (define-syntax ot-if
    (syntax-rules () [(_ test yes no) (if (truth test) yes no)]
                    [(_ test yes) (if (truth test) yes (void))]))
  (define-syntax ot-and
    (syntax-rules () [(_) #t] [(_ x) x]
      [(_ x xs ...) (let ([v x]) (if (truth v) (ot-and xs ...) v))]))
  (define-syntax ot-or
    (syntax-rules () [(_) #f] [(_ x) x]
      [(_ x xs ...) (let ([v x]) (if (truth v) v (ot-or xs ...)))]))
  (define-syntax ot-cond
    (syntax-rules (else =>)
      [(_) (void)] [(_ (else body ...)) (begin body ...)]
      [(_ (test => proc) clauses ...) (let ([v test]) (if (truth v) (proc v) (ot-cond clauses ...)))]
      [(_ (test) clauses ...) (let ([v test]) (if (truth v) v (ot-cond clauses ...)))]
      [(_ (test body ...) clauses ...) (if (truth test) (begin body ...) (ot-cond clauses ...))]))
  (define-syntax ot-when (syntax-rules () [(_ test body ...) (ot-if test (begin body ...))]))
  (define-syntax ot-unless (syntax-rules () [(_ test body ...) (ot-if test (void) (begin body ...))]))
  (define (ot-not x) (not (truth x)))
  (define (ot-filter f xs)
    (if (val:array? xs)
        (let ([out (val:array)])
          (val:collection-for-each (lambda (x) (when (truth (f x)) (val:push! out x))) xs)
          out)
        (filter (lambda (x) (truth (f x))) (if (val:nil? xs) '() xs))))
  (define (ot-exists f xs . more)
    (let loop ([lists (cons xs more)])
      (if (for-all null? lists) #f
          (let ([v (apply f (map car lists))])
            (if (truth v) v (loop (map cdr lists)))))))
  (define (ot-for-all f xs . more)
    (let loop ([lists (cons xs more)] [last #t])
      (if (for-all null? lists) last
          (let ([v (apply f (map car lists))])
            (if (truth v) (loop (map cdr lists) v) v))))))
