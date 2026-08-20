; Messages are copied into bounded FIFO mailboxes. A receive with no message
; blocks only that process; sending a message makes it runnable again.

(define (receiver count)
  (let loop ((remaining count))
    (if (> remaining 0)
        (begin
          (println "received" (receive))
          (loop (- remaining 1))))))

(define (sender target label)
  (send! target (array label 1))
  (yield)
  (send! target (list label 2)))

(define inbox (spawn receiver 4))
(spawn sender inbox :alpha)
(spawn sender inbox :beta)
