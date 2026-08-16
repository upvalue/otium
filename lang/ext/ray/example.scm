(require 'ray)

(ray/init-window 800 450 "Otium + Raylib")
(ray/set-target-fps! 60)

(unwind-protect
  (let ((x 100) (dx 3) (running #t))
    (while (and running (not (ray/window-should-close?)))
      (set! x (+ x dx))
      (if (or (< x 20) (> x 780)) (set! dx (- dx)) nil)
      (ray/begin-drawing)
      (ray/clear-background ray/raywhite)
      (ray/draw-text "Otium + Raylib" 20 20 24 ray/black)
      (ray/draw-circle x 225 20 ray/red)
      (set! running (ray/harness-continue?))
      (ray/end-drawing)))
  (ray/close-window))
