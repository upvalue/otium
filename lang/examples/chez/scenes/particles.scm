(ns scenes.particles)
(chez/import (otium canvas))

(defonce state (vector 0 0.0))
(define revision "FLOW / 01")

;; The phase and frame count survive changes to the renderer.
(define (render! canvas elapsed dt)
  (vector-set! state 0 (+ 1 (vector-ref state 0)))
  (vector-set! state 1 (+ (vector-ref state 1) dt))
  (clear! canvas 12 16 29)
  (let ([phase (vector-ref state 1)])
    (do ([row 0 (+ row 1)]) [(= row 18)]
      (do ([column 0 (+ column 1)]) [(= column 36)]
        (let* ([wave (sin (+ (* column 0.22) (* row 0.30) (* phase 1.3)))]
               [x (+ 48 (* column 24) (* 7 (cos (+ phase (* row 0.4)))))]
               [y (+ 140 (* row 21) (* 18 wave))]
               [size (+ 3 (* 5 (+ 1 wave)))])
          (rect! canvas x y size size
            (+ 60 (exact (round (* 70 (+ 1 wave)))))
            (+ 110 (exact (round (* 50 (+ 1 wave))))) 235)))))
  (text! canvas 40 42 "OTIUM / PARTICLE FIELD" 230 239 249)
  (text! canvas 40 68 "648 MOVING RECTS / STATE SURVIVES RELOAD" 121 147 175)
  (text! canvas 40 564 revision 129 171 245)
  (text! canvas 40 588 "EDIT scenes/particles.scm AND SAVE | R RELOAD | ESC CLOSE" 151 169 192))
