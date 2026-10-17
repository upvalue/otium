;; Source reading uses ordinary libc calls. No native helper or SDL wrapper is
;; needed; host-read is the only reader adapter.
(def host-libc (ffi/open-library host-libc-names))
(def host-fopen (ffi/bind host-libc "fopen" '(utf8 utf8) 'pointer))
(def host-fread (ffi/bind host-libc "fread" '(pointer size-t size-t pointer) 'size-t))
(def host-fclose (ffi/bind host-libc "fclose" '(pointer) 'int))
(def (source-text path)
  (let ((file (host-fopen path "rb")))
    (if (ffi/null? file) (error "Cannot open scene" path) #f)
    (unwind-protect
      (let ((bytes (ffi/alloc 'u8 1048577)))
        (unwind-protect
          (let ((count (host-fread bytes 1 1048576 file)))
            (if (= count 1048576) (error "Scene exceeds 1 MiB") #f)
            (ffi/read-utf8 bytes (+ count 1)))
          (ffi/free! bytes)))
      (host-fclose file))))
(def scene-observed #f)
(def scene-reloads 0)
(def scene-reload-errors 0)
(def scene-render-errors 0)
(def scene-broken? #f)
(def scene-ticks 0)
(def scene-phase 0.0)
(def (render! elapsed dt) (clear! 45 17 28))
(def (reload-scene! force?)
  (try
    (let ((source (source-text scene-path)))
      (if (or force? (not (equal? source scene-observed)))
          (begin
            (set! scene-observed source)
            (eval (host-read (string-append "(begin\n" source "\n)")))
            (set! scene-reloads (+ scene-reloads 1)) (set! scene-broken? #f)
            (display "reload ") (display scene-reloads) (newline)) #f))
    (catch ((fn (c) #t) c)
      (set! scene-reload-errors (+ scene-reload-errors 1))
      (display "RELOAD ERROR / fix source and save: ") (display c) (newline))))
(def (render-scene! elapsed dt)
  (if scene-broken? (clear! 45 17 28)
      (try (render! elapsed dt)
        (catch ((fn (c) #t) c)
          (set! scene-broken? #t) (set! scene-render-errors (+ scene-render-errors 1))
          (display "RENDER ERROR / fix source and save: ") (display c) (newline)))))

;; Status is optional and published atomically, after each completed frame.
(def host-fputs (ffi/bind host-libc "fputs" '(utf8 pointer) 'int))
(def host-rename (ffi/bind host-libc "rename" '(utf8 utf8) 'int))
(def (frame-status! frames window renderer)
  (if status-path
      (let ((temporary (string-append status-path ".tmp")))
        (let ((file (host-fopen temporary "wb")))
          (if (ffi/null? file) (error "Cannot write SDL status") #f)
          (unwind-protect
            (let ((result (host-fputs
                    (string-append (number->string frames) " "
                      (number->string scene-ticks) " "
                      (number->string scene-reloads) " "
                      (number->string scene-reload-errors) " "
                      (number->string scene-render-errors) " "
                      (if (ffi/pointer=? window sdl-window) "1" "0") " "
                      (if (ffi/pointer=? renderer sdl-renderer) "1" "0") "\n") file)))
              (if (< result 0) (error "Cannot write SDL status") #f))
            (host-fclose file)))
        (if (= 0 (host-rename temporary status-path)) #t
            (error "Cannot publish SDL status"))) #f))
