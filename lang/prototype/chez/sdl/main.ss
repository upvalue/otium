(import (chezscheme) (otium runtime) (otium canvas))

(define scene "shapes")
(define scene-root #f)
(define frame-limit #f)
(define capture-path #f)
(define status-path #f)
(define renderer-name
  (case (machine-type)
    [(arm64osx tarm64osx a6osx ta6osx) "metal"]
    [else "opengl,opengles2,vulkan,direct3d11,direct3d12"]))
(let parse ([args (cdr (command-line))])
  (unless (null? args)
    (cond
      [(member (car args) '("shapes" "particles")) (set! scene (car args)) (parse (cdr args))]
      [(equal? (car args) "--help")
       (display "Usage: sdl-demo [shapes|particles] [--frames N] [--capture file.bmp]\n")
       (display "                [--scene-root DIR] [--status FILE] [--renderer NAME]\n")
       (display "Save the scene file to reload; R retries; Escape or close exits.\n")
       (exit 0)]
      [(and (pair? (cdr args))
            (member (car args) '("--scene-root" "--frames" "--capture" "--status" "--renderer")))
       (case (string->symbol (car args))
         [(--scene-root) (set! scene-root (cadr args))]
         [(--capture) (set! capture-path (cadr args))]
         [(--status) (set! status-path (cadr args))]
         [(--renderer) (set! renderer-name (cadr args))]
         [(--frames)
          (set! frame-limit (string->number (cadr args)))
          (unless (and (integer? frame-limit) (exact? frame-limit) (> frame-limit 0))
            (error 'sdl-demo "--frames must be a positive integer"))])
       (parse (cddr args))]
      [else (error 'sdl-demo "invalid arguments; use --help" args)])))

(when (and capture-path (not frame-limit))
  (error 'sdl-demo "--capture requires --frames"))

(define namespace (string->symbol (string-append "scenes." scene)))
(define source-path (string-append scene-root "/scenes/" scene ".scm"))
(load-path (list scene-root))
(evaluate `(require ,namespace :as scene))
(define render (evaluate '(live 'scene/render!)))
(define scene-state (namespace-ref namespace 'state))
(define (source-text) (call-with-input-file source-path get-string-all))
(define observed (source-text))
(define reloads 0)
(define reload-errors 0)
(define render-errors 0)
(define render-broken? #f)
(define message "Watching source")
(define (report-condition label condition)
  (fprintf (current-error-port) "~a: " label)
  (display-condition condition (current-error-port))
  (newline (current-error-port)))

(define (reload!)
  (guard (c [else (set! reload-errors (+ reload-errors 1))
                  (set! message "RELOAD ERROR / fix source and save")
                  (report-condition 'reload c)])
    (require-namespace namespace #t)
    (unless (eq? scene-state (namespace-ref namespace 'state))
      (error 'reload "scene replaced persistent state; use defonce"))
    (set! reloads (+ reloads 1))
    (set! render-broken? #f)
    (set! message "Reloaded / state preserved")
    (printf "reload ~a, scene ticks ~a\n" reloads (vector-ref scene-state 0))
    (flush-output-port (current-output-port))))

(define (write-status canvas frames)
  (when status-path
    (let ([temporary (string-append status-path ".tmp")])
      (when (file-exists? temporary) (delete-file temporary))
      (call-with-output-file temporary
        (lambda (port)
          (write `((backend ,(canvas-backend canvas))
                   (window ,(canvas-window canvas)) (renderer ,(canvas-renderer canvas))
                   (frames ,frames) (scene-ticks ,(vector-ref scene-state 0))
                   (reloads ,reloads) (reload-errors ,reload-errors)
                   (render-errors ,render-errors)
                   (revision ,(namespace-ref namespace 'revision))) port)
          (newline port)))
      (rename-file temporary status-path))))

(with-canvas renderer-name
  (lambda (canvas)
    (printf "SDL renderer: ~a; watching ~a\n" (canvas-backend canvas) source-path)
    (flush-output-port (current-output-port))
    (title! canvas (string-append "Otium / " scene " / " (canvas-backend canvas)))
    (let ([started (seconds)] [last-poll 0.0])
      (let loop ([frames 0] [previous (seconds)])
        (let* ([now (seconds)] [action (events! canvas)])
          (unless (eq? action 'quit)
            ;; Poll between frames on the SDL thread; no graphics calls race reload.
            (when (or (eq? action 'reload) (> (- now last-poll) 0.2))
              (set! last-poll now)
              (guard (c [else (set! message "SOURCE ERROR / waiting for file")])
                (let ([text (source-text)])
                  (when (or (eq? action 'reload) (not (equal? text observed)))
                    (set! observed text)
                    (reload!)))))
            (unless render-broken?
              (guard (c [else (set! render-errors (+ render-errors 1))
                              (set! render-broken? #t)
                              (set! message "RENDER ERROR / fix source and save")
                              (report-condition 'render c)])
                (render canvas (- now started) (min 0.1 (- now previous)))))
            (when render-broken? (clear! canvas 45 17 28))
            (text! canvas 40 612
              (format "~a | reloads ~a | frame ~a" message reloads frames) 230 196 133)
            (let ([completed (+ frames 1)])
              ;; Readback must happen before presentation invalidates the backbuffer.
              (when (and capture-path frame-limit (= completed frame-limit))
                (capture! canvas capture-path))
              (present! canvas)
              (write-status canvas completed)
              (unless (and frame-limit (>= completed frame-limit))
                ;; Also cap frames if VSync is unavailable or the window is occluded.
                (let ([remaining (- 0.016 (- (seconds) now))])
                  (when (> remaining 0)
                    (sleep (make-time 'time-duration (exact (round (* remaining 1e9))) 0))))
                (loop completed now)))))))
    (printf "Closed SDL window; reloads ~a, reload errors ~a, render errors ~a\n"
      reloads reload-errors render-errors)))
