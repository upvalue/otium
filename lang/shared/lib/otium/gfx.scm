(ns otium.gfx)
(require (otium.ui.native :as native))
(require (otium.sdl :as sdl))
(defclass Context renderer clip (focused? :init #f) (pressed? :init #f) (disabled? :init #f))
(def (rect g bounds color . radius)
  "Record a rounded rectangle, clipped to the current widget's bounds."
  (let ((clip (g :clip)))
    (sdl/check (native/gpu-rect (g :renderer)
      (bounds :x) (bounds :y) (bounds :width) (bounds :height)
      (if (null? radius) 0 (car radius)) color
      (clip :x) (clip :y) (clip :width) (clip :height)))))

(def (font-index font)
  (cond ((eq? font :ui) 0) ((eq? font :mono) 1)
        (else (error "Unknown UI font" font))))
(def (text g bounds string font size color wrap)
  "Record shaped text in logical pixels, wrapping at the bounds when enabled."
  (let ((clip (g :clip)))
    (sdl/check (native/gpu-text (g :renderer) string (font-index font) size
      (if wrap (bounds :width) -1) (bounds :x) (bounds :y) color
      (clip :x) (clip :y) (clip :width) (clip :height)))))
