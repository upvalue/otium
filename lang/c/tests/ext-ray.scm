(require 'ray)

(if (equal? ray/white 0xffffffff)
    nil
    (error "ray companion source did not load"))
(if (equal? (ray/rgb 1 2 3) 0x010203ff)
    nil
    (error "ray rgb packing is incorrect"))
(if (ray/window-ready?)
    (error "ray smoke test unexpectedly has a window")
    nil)

(if (and (procedure? ray/draw-codepoint)
         (procedure? ray/default-font)
         (procedure? ray/draw-render-texture-pro)
         (procedure? ray/next-key-pressed)
         (procedure? ray/random-value)
         (procedure? ray/take-screenshot!)
         (procedure? ray/env)
         (procedure? ray/file-exists?))
    nil
    (error "ray roguelike primitives were not registered"))

(ray/set-random-seed! 12345)
(define roll (ray/random-value 4 9))
(if (and (>= roll 4) (<= roll 9))
    nil
    (error "ray random-value returned an out-of-range result"))

(if (nil? (ray/env "OT_RAY_TEST_UNSET_VARIABLE"))
    nil
    (error "ray env returned a value for an unset variable"))
(if (ray/file-exists? "definitely/not/a/real/file.xyz")
    (error "ray file-exists? reported a missing file as present")
    nil)

; Without RAY_FRAMES the harness never stops a run on its own.
(if (ray/harness-continue?)
    nil
    (error "ray harness stopped a run with no frame budget"))
(if (nil? (ray/harness-next-input!))
    nil
    (error "ray harness produced input with RAY_INPUT unset"))
