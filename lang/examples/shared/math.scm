;; The launcher establishes ffi and math-library-names.
(define math-library (ffi/open-library math-library-names))
(define native-cos (ffi/bind math-library "cos" '(double) 'double))
(define native-hypot (ffi/bind math-library "hypot" '(double double) 'double))
(define native-modf (ffi/bind math-library "modf" '(double pointer) 'double))

(display (native-cos 0.0)) (newline)
(display (native-hypot 3.0 4.0)) (newline)
(let ((integral (ffi/alloc 'double 1)))
  (unwind-protect
    (begin
      (display (native-modf 12.75 integral)) (newline)
      (display (ffi/ref integral 'double 0)) (newline))
    (ffi/free! integral)))
