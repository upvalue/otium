(ns otium.gfx)
(require (otium.ui.native :as native))
(require (otium.sdl :as sdl))
(defclass Context renderer clip)
(def (rect g bounds color . radius)
  "Record a rounded rectangle, clipped to the current widget's bounds."
  (let ((clip (g :clip)))
    (sdl/check (native/gpu-rect (g :renderer)
      (bounds :x) (bounds :y) (bounds :width) (bounds :height)
      (if (null? radius) 0 (car radius)) color
      (clip :x) (clip :y) (clip :width) (clip :height)))))
