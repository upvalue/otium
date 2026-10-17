(library (otium truth)
  (export (rename (ot-if if) (ot-and and) (ot-or or) (ot-cond cond)
                  (ot-when when) (ot-unless unless)
                  (ot-not not)))
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
)
