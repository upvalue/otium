; Allocation-free color helpers and commonly used Raylib constants.
(define (rgba r g b a) (+ (* r 16777216) (* g 65536) (* b 256) a))
(define (rgb r g b) (rgba r g b 255))

(define white 0xffffffff)
(define black 0x000000ff)
(define red 0xe62937ff)
(define green 0x00e430ff)
(define blue 0x0079f1ff)
(define yellow 0xfdf900ff)
(define orange 0xffa100ff)
(define purple 0xc87affff)
(define raywhite 0xf5f5f5ff)
(define blank 0x00000000)

(define flag-fullscreen 0x00000002)
(define flag-window-resizable 0x00000004)
(define flag-window-undecorated 0x00000008)
(define flag-window-transparent 0x00000010)
(define flag-msaa-4x 0x00000020)
(define flag-vsync 0x00000040)
(define flag-window-always-run 0x00000100)
(define flag-window-highdpi 0x00002000)

(define key-space 32)
(define key-escape 256)
(define key-enter 257)
(define key-tab 258)
(define key-backspace 259)
(define key-delete 261)
(define key-right 262)
(define key-left 263)
(define key-down 264)
(define key-up 265)
(define key-a 65)
(define key-b 66)
(define key-c 67)
(define key-d 68)
(define key-e 69)
(define key-f 70)
(define key-g 71)
(define key-h 72)
(define key-i 73)
(define key-j 74)
(define key-k 75)
(define key-l 76)
(define key-m 77)
(define key-n 78)
(define key-o 79)
(define key-p 80)
(define key-q 81)
(define key-r 82)
(define key-s 83)
(define key-t 84)
(define key-u 85)
(define key-v 86)
(define key-w 87)
(define key-x 88)
(define key-y 89)
(define key-z 90)
(define key-period 46)
(define key-comma 44)
(define key-f11 300)

(define mouse-left 0)
(define mouse-right 1)
(define mouse-middle 2)

(define filter-point 0)
(define filter-bilinear 1)

; Unattended-run harness for agents and CI. RAY_FRAMES bounds the run to a
; frame count, RAY_SCREENSHOT saves a capture of the final frame, and
; RAY_INPUT feeds a comma-separated token per frame to (harness-next-input!).
; Call (harness-continue?) once per frame after all drawing but before
; end-drawing, so the capture reads the finished back buffer:
;
;   (while (and running (not (window-should-close?)))
;     ...update and draw...
;     (set! running (harness-continue?))
;     (end-drawing))
(define harness-frames
  (if (nil? (env "RAY_FRAMES")) nil (string->number (env "RAY_FRAMES"))))
(define harness-screenshot (env "RAY_SCREENSHOT"))
(define harness-input
  (if (nil? (env "RAY_INPUT")) (array) (string-split (env "RAY_INPUT") ",")))
(define harness-input-index 0)
(define harness-frame 0)

(define (harness-next-input!)
  (let ((token (get harness-input harness-input-index nil)))
    (if (nil? token)
        nil
        (begin
          (set! harness-input-index (+ harness-input-index 1))
          token))))

(define (harness-continue?)
  (set! harness-frame (+ harness-frame 1))
  (if (nil? harness-frames)
      #t
      (if (< harness-frame harness-frames)
          #t
          (begin
            (if (nil? harness-screenshot)
                nil
                (take-screenshot! harness-screenshot))
            #f))))
