(require 'raylib)

(raylib/init-window 800 450 "Otium + Raylib")
(raylib/set-target-fps! 60)

(unwind-protect
  (let ((x 100) (dx 3))
    (while (not (raylib/window-should-close?))
      (set! x (+ x dx))
      (if (or (< x 20) (> x 780)) (set! dx (- dx)) nil)
      (raylib/begin-drawing)
      (raylib/clear-background raylib/raywhite)
      (raylib/draw-text "Otium + Raylib" 20 20 24 raylib/black)
      (raylib/draw-circle x 225 20 raylib/red)
      (raylib/end-drawing)))
  (raylib/close-window))
