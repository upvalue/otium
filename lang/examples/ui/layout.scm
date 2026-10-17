(ns ui.layout)
(require (otium.ui :as ui))
(require (otium.gfx :as gfx))
;; A fixed-size custom widget: measure in Otium, lay out in Yoga, paint on SDL_GPU.
(defclass Swatch :is ui/Widget color (width :init 80) (height :init 56))
(defmethod (ui/measure (w Swatch) limits) (ui/size (w :width) (w :height)))
(defmethod (ui/paint (w Swatch) g bounds) (gfx/rect g bounds (w :color) 12))
(def (swatch key color width height)
  (ui/new Swatch {:key key :color color :width width :height height}))
(defclass Toggle :is ui/Widget color (selected :init #f :state #t))
(defmethod (ui/build (w Toggle))
  (ui/button {:on-press (fn () (set! (w :selected) (not (w :selected))))}
    (swatch :fill (if (w :selected) 0x67d9c0ff (w :color)) 96 88)))
(def (view)
  (ui/column {:grow 1 :padding 24 :gap 16 :background 0x101827ff}
    (ui/row {:height 72 :padding 12 :gap 12 :background 0x202d43ff :radius 16}
      (swatch :logo 0x67d9c0ff 48 48)
      (ui/column {:grow 1 :gap 8}
        (swatch :title 0xdce5f2ff 180 16)
        (swatch :subtitle 0x71849fff 120 10)))
    (ui/row {:grow 1 :gap 16}
      (ui/column {:width 144 :padding 12 :gap 12 :background 0x202d43ff :radius 16}
        (swatch :nav1 0x67d9c0ff 120 32)
        (swatch :nav2 0x34465fff 120 32)
        (swatch :nav3 0x34465fff 120 32))
      (ui/column {:grow 1 :padding 16 :gap 16 :background 0x202d43ff :radius 16}
        (ui/row {:gap 12}
          (ui/new Toggle {:key :card1 :color 0x709cffff})
          (ui/new Toggle {:key :card2 :color 0xecac74ff})
          (ui/new Toggle {:key :card3 :color 0xcc91e6ff}))
        (swatch :bar 0x67d9c0ff 160 24)
        (swatch :detail 0x34465fff 220 56)))
    (swatch :footer 0x34465fff 240 12)))
