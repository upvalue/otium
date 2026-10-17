(def revision "FLOW / 01")
(def (render! elapsed dt)
  (set! scene-ticks (+ scene-ticks 1))
  (set! scene-phase (+ scene-phase dt))
  (clear! 12 16 29)
  (let rows ((row 0))
    (if (= row 18) #f
        (begin
          (let columns ((column 0))
            (if (= column 36) #f
                (let ((wave (sin (+ (* column 0.22) (* row 0.30) (* scene-phase 1.3)))))
                  (let ((x (+ 48 (* column 24) (* 7 (cos (+ scene-phase (* row 0.4))))))
                       (y (+ 140 (* row 21) (* 18 wave)))
                       (size (+ 3 (* 5 (+ 1 wave)))))
                    (rect! x y size size
                      (+ 60 (exact (round (* 70 (+ 1 wave)))))
                      (+ 110 (exact (round (* 50 (+ 1 wave))))) 235)
                    (columns (+ column 1))))))
          (rows (+ row 1)))))
  (text! 40 42 "OTIUM / PARTICLE FIELD" 230 239 249)
  (text! 40 68 "648 MOVING RECTS / STATE SURVIVES RELOAD" 121 147 175)
  (text! 40 564 revision 129 171 245)
  (text! 40 588 "EDIT examples/shared/scenes/particles.scm | R RELOAD | ESC CLOSE" 151 169 192))
