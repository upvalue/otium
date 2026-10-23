(library (otium strings)
  (export string-contains-ascii?)
  (import (chezscheme))

  ;; The text may contain Unicode. Only the needle must be ASCII, so a
  ;; UTF-8 implementation can search bytes without decoding characters.
  (define (string-contains-ascii? text needle)
    (unless (string? text)
      (error 'string-contains-ascii? "expected string" text))
    (unless (and (char? needle) (< (char->integer needle) 128))
      (error 'string-contains-ascii? "expected ASCII character" needle))
    (let ([n (string-length text)])
      (let loop ([i 0])
        (and (< i n)
             (or (char=? (string-ref text i) needle)
                 (loop (+ i 1))))))))
