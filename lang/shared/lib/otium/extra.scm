(defmacro -> (x . forms)
  "(-> value form ...)\nThread value into the first argument position of each form in turn; a bare name is called with the value."
  (let ((acc x) (s forms))
    (while (pair? s)
      (let ((f (car s)))
        (set! acc (if (pair? f)
                      (cons (car f) (cons acc (cdr f)))
                      (list f acc))))
      (set! s (cdr s)))
    acc))

(defmacro ->> (x . forms)
  "(->> value form ...)\nThread value into the last argument position of each form in turn."
  (let ((acc x) (s forms))
    (while (pair? s)
      (let ((f (car s)))
        (set! acc (if (pair? f)
                      (append f (list acc))
                      (list f acc))))
      (set! s (cdr s)))
    acc))


(def (group-by keyfn seq)
  "Table of key -> array of elements, in first-seen key order."
  (let ((t (table)))
    (for-each (fn (x)
                (let ((k (keyfn x)))
                  (let ((a (get t k)))
                    (when (nil? a)
                      (set! a (array))
                      (put! t k a))
                    (push! a x))))
              seq)
    t))

(def (frequencies seq)
  "(frequencies sequence)\nReturn a table mapping each distinct element to its occurrence count."
  (let ((t (table)))
    (for-each (fn (x) (put! t x (+ 1 (get t x 0)))) seq)
    t))


(def (partial f . pre)
  "(partial procedure argument ...)\nReturn a procedure that prepends the supplied arguments to its later arguments."
  (fn args (apply f (append pre args))))

(def (comp . fs)
  "Right-to-left composition; (comp) is identity."
  (cond ((null? fs) identity)
        ((null? (cdr fs)) (car fs))
        (else
         (let ((inner (apply comp (cdr fs))))
           (fn args ((car fs) (apply inner args)))))))


