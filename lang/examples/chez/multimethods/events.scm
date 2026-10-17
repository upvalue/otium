(ns demo.multimethods)
(require otium.generic :as g)

(define-type Button)
(define-type PointerDown)
(define-generic (handle-event! widget event))

(define-method (handle-event! widget event)
  'ignored)

(define-method (handle-event! (widget Button) event)
  'button-default)

(define-method (handle-event! (widget Button) (event PointerDown))
  (g/field-set! widget 'presses (+ 1 (g/field-ref widget 'presses 0)))
  (g/field-ref widget 'label))

(def button (g/make Button 'label "7"))
(display (handle-event! button (g/make PointerDown)))
(newline)
(display (g/field-ref button 'presses))
(newline)

(define-generic (combine left right))
(define-method (combine (left g/Number) (right g/Number))
  (+ left right))
(define-method (combine (left g/String) (right g/String))
  (string-append left right))
(display (combine 3 4))
(newline)
(display (combine "calculator" " prototype"))
(newline)
