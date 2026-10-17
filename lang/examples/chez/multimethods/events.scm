(ns demo.multimethods)
(require otium.generic :as g)

(defclass Button label (presses :init 0))
(defclass PointerDown)
(defgeneric (handle-event! widget event))

(defmethod (handle-event! widget event)
  'ignored)

(defmethod (handle-event! (widget Button) event)
  'button-default)

(defmethod (handle-event! (widget Button) (event PointerDown))
  (set! (widget :presses) (+ 1 (widget :presses)))
  (widget :label))

(def button (g/make Button :label "7"))
(display (handle-event! button (g/make PointerDown)))
(newline)
(display (button :presses))
(newline)

(defgeneric (combine left right))
(defmethod (combine (left g/Number) (right g/Number))
  (+ left right))
(defmethod (combine (left g/String) (right g/String))
  (string-append left right))
(display (combine 3 4))
(newline)
(display (combine "calculator" " prototype"))
(newline)
