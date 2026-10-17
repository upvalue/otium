(ns otium.ui.output)
(require (otium.ffi :as ffi))
;; File output uses libc until the shared file library has a writer.
(defonce libc (ffi/open-library '("/usr/lib/libSystem.B.dylib" "libc.so.6" "libc.so")))
(def- fopen (ffi/bind libc "fopen" '(utf8 utf8) 'pointer))
(def- fputs (ffi/bind libc "fputs" '(utf8 pointer) 'int))
(def- fclose (ffi/bind libc "fclose" '(pointer) 'int))
(def (write-text! path text)
  (let ((file (fopen path "wb")))
    (when (ffi/null? file) (error "Cannot open output file" path))
    (unwind-protect
      (when (< (fputs text file) 0) (error "Cannot write output file" path))
      (when (not (= (fclose file) 0)) (error "Cannot close output file" path)))))
