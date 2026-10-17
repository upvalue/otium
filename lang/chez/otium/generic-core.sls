(library (otium generic-core)
  (export public-bindings fail descriptor? descriptor-owner fresh-type
          generic? generic-owner generic-arity fresh-generic add-method!
          dispatch-type make field-ref field-set! field-delete! field-has?
          field-keys typed-map? generic-methods remove-method!)
  (import (chezscheme))

  (define (fail who code message . details)
    (apply error who message code details))

  (define-record-type (descriptor new-descriptor descriptor?)
    (opaque #t)
    (fields name owner))
  (define (fresh-type owner name)
    (new-descriptor
      (string->symbol (string-append (symbol->string owner) "/" (symbol->string name)))
      (cons owner name)))
  (define (builtin name) (fresh-type 'otium.generic name))
  (define Number (builtin 'Number))
  (define Boolean (builtin 'Boolean))
  (define Character (builtin 'Character))
  (define String (builtin 'String))
  (define Symbol (builtin 'Symbol))
  (define Null (builtin 'Null))
  (define Pair (builtin 'Pair))
  (define Vector (builtin 'Vector))
  (define Bytevector (builtin 'Bytevector))
  (define Hashtable (builtin 'Hashtable))
  (define Procedure (builtin 'Procedure))
  (define TypeDescriptor (builtin 'TypeDescriptor))
  (define Other (builtin 'Other))

  (define-record-type (typed-map new-map typed-map?)
    (opaque #t)
    (fields descriptor entries))
  (define (type-name type)
    (unless (descriptor? type)
      (fail 'type-name 'invalid-specializer "expected type descriptor" type))
    (descriptor-name type))
  (define (dispatch-type value)
    (cond [(typed-map? value) (typed-map-descriptor value)]
          [(number? value) Number] [(boolean? value) Boolean]
          [(char? value) Character] [(string? value) String]
          [(symbol? value) Symbol] [(null? value) Null]
          [(pair? value) Pair] [(vector? value) Vector]
          [(bytevector? value) Bytevector] [(hashtable? value) Hashtable]
          [(procedure? value) Procedure] [(descriptor? value) TypeDescriptor]
          [else Other]))
  (define (make type . fields)
    (unless (and (descriptor? type)
                 (not (eq? (car (descriptor-owner type)) 'otium.generic)))
      (fail 'make 'invalid-specializer "expected user-defined type" type))
    (unless (and (even? (length fields))
                 (let loop ([xs fields])
                   (or (null? xs) (and (symbol? (car xs)) (loop (cddr xs))))))
      (fail 'make 'invalid-field-operation "expected symbol/value pairs"))
    (let ([entries (make-eq-hashtable)])
      (let loop ([xs fields])
        (unless (null? xs)
          (hashtable-set! entries (car xs) (cadr xs))
          (loop (cddr xs))))
      (new-map type entries)))
  (define (entries who object key)
    (unless (and (typed-map? object) (symbol? key))
      (fail who 'invalid-field-operation "expected typed map and symbol key"))
    (typed-map-entries object))
  (define absent (list 'absent))
  (define field-ref
    (case-lambda
      [(object key)
       (let ([value (hashtable-ref (entries 'field-ref object key) key absent)])
         (if (eq? value absent)
             (fail 'field-ref 'missing-field "field is absent" key)
             value))]
      [(object key default)
       (hashtable-ref (entries 'field-ref object key) key default)]))
  (define (field-set! object key value)
    (hashtable-set! (entries 'field-set! object key) key value)
    value)
  (define (field-has? object key)
    (hashtable-contains? (entries 'field-has? object key) key))
  (define (field-delete! object key)
    (let* ([table (entries 'field-delete! object key)]
           [present? (hashtable-contains? table key)])
      (hashtable-delete! table key)
      present?))
  (define (field-keys object)
    (unless (typed-map? object)
      (fail 'field-keys 'invalid-field-operation "expected typed map"))
    (vector->list (hashtable-keys (typed-map-entries object))))

  (define-record-type generic-state
    (fields owner arity (mutable methods)))
  ;; Methods may capture their own generic; ephemerons allow those cycles to die.
  (define generics (make-ephemeron-eq-hashtable))
  (define (generic? value) (hashtable-contains? generics value))
  (define (state who generic)
    (or (hashtable-ref generics generic #f)
        (fail who 'not-generic "expected generic procedure")))
  (define (generic-owner g) (generic-state-owner (state 'generic-owner g)))
  (define (generic-arity g) (generic-state-arity (state 'generic-arity g)))
  (define (same-signature? a b)
    (and (= (length a) (length b)) (for-all eq? a b)))
  (define (signature! who s signature)
    (unless (and (list? signature)
                 (= (length signature) (generic-state-arity s))
                 (for-all (lambda (x) (or (not x) (descriptor? x))) signature))
      (fail who 'invalid-signature "expected descriptor/wildcard signature" signature)))
  (define (applicable? signature types)
    (for-all (lambda (specializer type) (or (not specializer) (eq? specializer type)))
      signature types))
  (define (more-specific? a b)
    (and (for-all (lambda (x y) (or (eq? x y) (and x (not y)))) a b)
         (exists (lambda (x y) (and x (not y))) a b)))
  (define (signature-names signature)
    (map (lambda (x) (and x (descriptor-name x))) signature))
  (define (select-method s types)
    (let* ([applicable
            (filter (lambda (method) (applicable? (car method) types))
              (generic-state-methods s))]
           [best
            (filter (lambda (method)
                      (not (exists (lambda (other)
                                     (more-specific? (car other) (car method)))
                             applicable))) applicable)])
      (cond [(null? best)
             (fail 'generic-call 'missing-method "no applicable method"
               (generic-state-owner s) (signature-names types))]
            [(pair? (cdr best))
             (fail 'generic-call 'ambiguous-method
               "ambiguous methods; inspect generic-methods and use remove-method! to remove stale signatures"
               (generic-state-owner s) (signature-names types)
               (map (lambda (m) (signature-names (car m))) best))]
            [else (cdar best)])))
  (define (fresh-generic owner name arity)
    (let* ([s (make-generic-state (cons owner name) arity '())]
           [g (lambda args
                (unless (= (length args) arity)
                  (fail 'generic-call 'wrong-arity "incorrect generic arity"
                    (generic-state-owner s) arity (length args)))
                (let ([method (select-method s (map dispatch-type args))])
                  (apply method args)))])
      (hashtable-set! generics g s)
      g))
  (define (add-method! generic signature implementation)
    (let ([s (state 'define-method generic)])
      (signature! 'define-method s signature)
      (unless (procedure? implementation)
        (fail 'define-method 'invalid-declaration "expected implementation procedure"))
      (generic-state-methods-set! s
        (cons (cons (list-copy signature) implementation)
          (filter (lambda (m) (not (same-signature? (car m) signature)))
            (generic-state-methods s)))))
    (void))
  (define (generic-methods generic)
    (map (lambda (method) (list-copy (car method)))
      (generic-state-methods (state 'generic-methods generic))))
  (define (remove-method! generic signature)
    (let ([s (state 'remove-method! generic)])
      (signature! 'remove-method! s signature)
      (let* ([old (generic-state-methods s)]
             [new (filter (lambda (m) (not (same-signature? (car m) signature))) old)])
        (generic-state-methods-set! s new)
        (< (length new) (length old)))))
  (define public-bindings
    (list
      (cons 'make make) (cons 'typed-map? typed-map?)
      (cons 'type-descriptor? descriptor?) (cons 'type-name type-name)
      (cons 'dispatch-type dispatch-type) (cons 'field-ref field-ref)
      (cons 'field-set! field-set!) (cons 'field-has? field-has?)
      (cons 'field-delete! field-delete!) (cons 'field-keys field-keys)
      (cons 'generic? generic?) (cons 'generic-methods generic-methods)
      (cons 'remove-method! remove-method!)
      (cons 'Number Number) (cons 'Boolean Boolean) (cons 'Character Character)
      (cons 'String String) (cons 'Symbol Symbol) (cons 'Null Null)
      (cons 'Pair Pair) (cons 'Vector Vector) (cons 'Bytevector Bytevector)
      (cons 'Hashtable Hashtable) (cons 'Procedure Procedure)
      (cons 'TypeDescriptor TypeDescriptor) (cons 'Other Other))))
