(import (chezscheme) (otium strings))

(define checks 0)
(define (check expected actual)
  (unless (equal? expected actual) (error 'check "unexpected result" expected actual))
  (set! checks (+ checks 1)))

(do ([i 0 (+ i 1)]) ((= i 128))
  (let ([c (integer->char i)])
    (check #f (string-contains-ascii? "" c))
    (check #f (string-contains-ascii? "é🙂Āįž" c))
    (check #t (string-contains-ascii? (string c #\é #\🙂) c))
    (check #t (string-contains-ascii? (string #\é c #\🙂) c))
    (check #t (string-contains-ascii? (string #\é #\🙂 c) c))))

(for-each
  (lambda (needle)
    (check #t
      (guard (c [(error? c) #t])
        (string-contains-ascii? "" needle)
        #f)))
  (list (integer->char 128) #\é #\🙂 0 "a" #f))
(check #t
  (guard (c [(error? c) #t])
    (string-contains-ascii? #f #\nul)
    #f))

(printf "strings: ~a checks passed\n" checks)
