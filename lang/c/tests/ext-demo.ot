(require 'demo)

(def inline (demo/counter 41))
(if (foreign? inline) nil (error "demo inline counter is not foreign"))
(demo/counter-inc! inline)
(if (= (demo/counter-value inline) 42)
    nil
    (error "demo inline counter returned the wrong value"))

(def owned (demo/owned-counter 9))
(demo/counter-inc! owned)
(if (= (demo/counter-value owned) 10)
    nil
    (error "demo owned counter returned the wrong value"))
(demo/release-counter! owned)

(def released-errors #f)
(try (demo/counter-value owned)
  (catch ((fn (condition) #t) condition) (set! released-errors #t)))
(if released-errors nil (error "released demo counter remained usable"))
