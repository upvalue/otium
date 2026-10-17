(ns ui.quit-script)
(require (otium.sdl :as sdl))
(require (otium.ffi :as ffi))
(def (step! tree frame)
  (when (= frame 2)
    ;; SDL_Event is a 128-byte union; u64 storage supplies its alignment.
    (let ((event (ffi/alloc 'u64 16))
          (push (ffi/bind sdl/library "SDL_PushEvent" '(pointer) 'bool)))
      (unwind-protect
        (begin
          (ffi/set! event 'u32 0 256)
          (sdl/check (push event)))
        (ffi/free! event)))))
