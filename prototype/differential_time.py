#!/usr/bin/env python3
"""Compare clock contracts without comparing readings taken at different times."""
import differential

differential.CASES = [
    '(println (list (int? (current-jiffy)) (jiffies-per-second) (float? (current-second)) (> (current-second) 0)))',
    '(let ((before (current-jiffy)) (after (current-jiffy))) (println (<= before after)))',
    '(require otium.time :as clock) (let ((before (clock/seconds)) (after (clock/seconds))) (println (<= before after)))',
]
differential.INVALID = ['(current-jiffy 0)', '(current-second 0)', '(jiffies-per-second 0)']
if __name__ == '__main__':
    differential.main()
