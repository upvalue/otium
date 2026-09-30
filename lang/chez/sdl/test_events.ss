(import (chezscheme) (sdl3) (otium canvas))

(unless (= (ftype-sizeof SDL_Event) 128)
  (error 'events "unexpected SDL_Event ABI"))
(unless (= (ftype-pointer-address
             (ftype-&ref SDL_KeyboardEvent (scancode)
               (make-ftype-pointer SDL_KeyboardEvent 0))) 24)
  (error 'events "unexpected keyboard scancode offset"))

(with-canvas
  (case (machine-type)
    [(arm64osx tarm64osx a6osx ta6osx) "metal"]
    [else "opengl,opengles2,vulkan,direct3d11,direct3d12"])
  (lambda (canvas)
    (let* ([address (foreign-alloc (ftype-sizeof SDL_Event))]
           [event (make-ftype-pointer SDL_Event address)])
      (dynamic-wind (lambda () (void))
        (lambda ()
          (events! canvas)
          (do ([i 0 (+ i 1)]) [(= i (ftype-sizeof SDL_Event))]
            (foreign-set! 'unsigned-8 address i 0))
          (ftype-set! SDL_Event (type) event SDL_EVENT_KEY_DOWN)
          (ftype-set! SDL_Event (key scancode) event SDL_SCANCODE_R)
          (unless (and (SDL_PushEvent event) (eq? (events! canvas) 'reload))
            (error 'events "R did not request reload"))
          (ftype-set! SDL_Event (key scancode) event SDL_SCANCODE_ESCAPE)
          (unless (and (SDL_PushEvent event) (eq? (events! canvas) 'quit))
            (error 'events "Escape did not request quit"))
          (ftype-set! SDL_Event (type) event SDL_EVENT_WINDOW_CLOSE_REQUESTED)
          (unless (and (SDL_PushEvent event) (eq? (events! canvas) 'quit))
            (error 'events "window close did not request quit")))
        (lambda () (foreign-free address))))))
(display "PASS: SDL event layout, R reload, Escape and window close\n")
