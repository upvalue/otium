#!/usr/bin/env python3
"""Compare portable numeric operations with Chez; bounded overflow has separate tests."""
import differential

differential.CASES = [
    "(println (list (quotient -7 2) (remainder -7 2) (modulo -7 2) (abs -4) (abs -4.0)))",
    "(println (list (min 1 2.0) (max 2 1.0) (min 2.0 1) (max 1.0 2)))",
    "(println (list (min 9007199254740993 9007199254740992) (max 9007199254740992 9007199254740993)))",
    "(println (list (floor -2.7) (ceiling -2.7) (truncate -2.7) (round -2.5) (round 2.5)))",
    "(println (list (int? (/ 1)) (float? (/ 1)) (/ 6 2) (/ 7 2) (/ 24 3 4) (/ 3 2 1.5)))",
    "(println (list (integer? 2.0) (integer? 2.5) (integer? #f) (exact? 2) (inexact? 2.0)))",
    "(println (list (sqrt 4) (sqrt 2) (= (sqrt -1) (sqrt -1))))",
    "(println (list (exp 0.0) (log 1.0) (sin 0.0) (cos 0.0) (tan 0.0) (asin 0.0) (acos 1.0) (atan 0.0)))",
    "(println (list (atan 0.0 -1.0) (expt 2 10) (expt 2.0 10) (expt 2.0 -2)))",
    "(println (list (exact 2.0) (inexact 2) (exact? (exact 2.0)) (inexact? (inexact 2))))",
    "(println (list (bitwise-and) (bitwise-ior) (bitwise-xor) (bitwise-and 14 11 -1) (bitwise-ior 8 4 3) (bitwise-xor 7 3 1)))",
    "(println (list (bitwise-not 0) (bitwise-not -1) (bitwise-arithmetic-shift -3 2) (bitwise-arithmetic-shift -3 -1) (bitwise-arithmetic-shift 3 -1000)))",
    "(println (list (min -0.5 -1) (max -0.5 -1) (min -1 -0.5) (max -1 -0.5)))",
]
differential.CASES += [
    "(println (list 1.0 -0.0 1000000.0 1e10 1e20 1e-4 1e-8 1.4142135623730951))",
    "(println (list (number->string 1.0) (number->string 1e20) (number->string 1e-8)))",
]
differential.INVALID = ["(quotient 1 0)", "(min)", "(max 1 #f)", "(floor 1e30)",
    "(round (sqrt -1))", "(bitwise-and 1.0)", "(bitwise-not)", "(bitwise-arithmetic-shift 1 1.0)", "(atan 1 2 3)"]
if __name__ == '__main__':
    differential.main()
