(require 'raylib)

(if (equal? raylib/white 0xffffffff)
    nil
    (error "raylib companion source did not load"))
(if (equal? (raylib/rgb 1 2 3) 0x010203ff)
    nil
    (error "raylib rgb packing is incorrect"))
(if (raylib/window-ready?)
    (error "raylib smoke test unexpectedly has a window")
    nil)

(if (and (procedure? raylib/draw-codepoint)
         (procedure? raylib/default-font)
         (procedure? raylib/draw-render-texture-pro)
         (procedure? raylib/next-key-pressed)
         (procedure? raylib/random-value))
    nil
    (error "raylib roguelike primitives were not registered"))

(raylib/set-random-seed! 12345)
(define roll (raylib/random-value 4 9))
(if (and (>= roll 4) (<= roll 9))
    nil
    (error "raylib random-value returned an out-of-range result"))
