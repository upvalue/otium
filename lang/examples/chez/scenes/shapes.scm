(ns scenes.shapes)
(chez/import (otium canvas))

(defonce state (vector 0 0.0))
(define revision "ORBIT / 01")

;; Save changes to this function while the demo is running.
(define (render! canvas elapsed dt)
  (vector-set! state 0 (+ 1 (vector-ref state 0)))
  (vector-set! state 1 (+ (vector-ref state 1) dt))
  (clear! canvas 13 19 32)
  (do ([x 0 (+ x 40)]) [(> x 960)] (line! canvas x 0 x 640 24 33 48))
  (do ([y 0 (+ y 40)]) [(> y 640)] (line! canvas 0 y 960 y 24 33 48))
  (let ([phase (vector-ref state 1)])
    (do ([i 0 (+ i 1)]) [(= i 24)]
      (let* ([angle (+ (* i (/ 6.283185 24)) (* phase 0.6))]
             [radius (+ 120 (* 55 (sin (+ phase (* i 0.4)))))]
             [x (+ 480 (* radius (cos angle)))]
             [y (+ 335 (* radius (sin angle)))])
        (line! canvas 480 335 x y 38 78 95)
        (rect! canvas (- x 9) (- y 9) 18 18 75 224 190))))
  (rect! canvas 466 321 28 28 255 186 95)
  (text! canvas 40 42 "OTIUM / LIVE CANVAS" 230 239 249)
  (text! canvas 40 68 "SDL3 HARDWARE RENDERING + CHEZ SCHEME" 121 147 175)
  (text! canvas 40 564 revision 75 224 190)
  (text! canvas 40 588 "EDIT scenes/shapes.scm AND SAVE | R RELOAD | ESC CLOSE" 151 169 192))
