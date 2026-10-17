(require otium.cli)
(def command
  (otium.cli/compile
    '(command greet
       (summary "Greet someone.")
       (version "0.1.0")
       (argument name (help "Person to greet"))
       (option count (names "-n" "--count") (type integer)
         (range 1 100) (default 1) (metavar "N") (help "Number of greetings"))
       (examples ("greet --count 3 Alice" "Three greetings")))))
(def (main args)
  (let loop ((left (otium.cli/ref args 'count)))
    (if (= left 0) 0
        (begin (display (string-append "Hello, " (otium.cli/ref args 'name) "!\n"))
               (loop (- left 1))))))
(otium.cli/run command main)
