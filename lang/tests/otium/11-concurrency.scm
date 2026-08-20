; Cooperative processes, identities, isolation, and FIFO mailboxes.

(println "self-pid:" (pid? (self)) (type (self)) (alive? (self)))
(let ((left (make-ref)) (right (make-ref)))
  (println "refs:" (ref? left) (type left) (eq? left left) (eq? left right)))

(define (counter label)
  (println "count:" label 1)
  (yield)
  (println "count:" label 2))

(spawn counter :alpha)
(spawn counter :beta)

(define (receiver count)
  (let loop ((remaining count))
    (if (> remaining 0)
        (begin
          (println "message:" (receive))
          (loop (- remaining 1))))))

(define (sender target label)
  (let ((message (array label 1)))
    (println "send:" (send target message))
    (put! message 1 99))
  (yield)
  (send! target (list label 2)))

(define inbox (spawn receiver 4))
(spawn sender inbox :left)
(spawn sender inbox :right)
