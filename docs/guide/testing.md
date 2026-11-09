# ✨ Testing

Tests let you check that your functions still behave as expected after a change.
[otium.test](/reference/otium.test/) provides value comparisons, checks for
expected errors, and a summary of passing checks. Tests are ordinary Otium
expressions: they run when you run the file containing them.

## Write checks

[`check`](/reference/otium.test/#check) takes the expected value first and the
actual value second. It compares them with `equal?`, so you can check
collections as well as individual values. An optional first argument labels the
check to make failures easier to identify.

This complete test program checks a function that halves a number:

```otium
(ns guide.testing)
(require otium.test :refer (check fails? report))

(def (halve n) (/ n 2))

(check 3 (halve 6))
(check "fractional result" 1.5 (halve 3))
(check "several inputs" '(0 1 2) (map halve '(0 2 4)))

(report "halve")
```

Successful checks are silent. [`report`](/reference/otium.test/#report) prints
the number of checks that have passed:

```text
halve: 3 checks passed
```

For your own code, keep the functions in their application namespace and put the
checks in a separate test file. Require the application namespace with an alias,
then call its functions through that alias in your checks. See
[Namespaces](namespaces.md) for loading modules and using aliases.

## Check expected errors

[`fails?`](/reference/otium.test/#fails?) returns `#t` when the operation raises
a condition, or `#f` when it completes normally. Pass a zero-argument function
so the operation runs inside `fails?`.

Add this check before the report in the program above:

```otium
(check "rejects text" #t (fails? (fn () (halve "six"))))
```

The report now prints:

```text
halve: 4 checks passed
```

This checks only that a condition was raised, not which condition. When the kind
or message matters, catch the condition and check its contents; see [Errors and
recovery](errors.md).

## Run tests

Save your test program in a `.ot` file and pass its path to `./otium`, just as
you would any other Otium program. If your application modules need an
additional load directory, add `-L` followed by that directory. See [How to
run](../how-to-run.md) for setup.

Put `report` after your checks. Its count is shared across checks using
`otium.test` in the same process. The name you pass is a label, not a test
selector. Run the file in a fresh process for a fresh count.

## Read a failure

A failed check prints its label, expected value, and actual value, then exits
with status `1`. Later checks and the final report do not run.

Replacing the first check above with this deliberately incorrect expectation:

```otium
(check "halving six" 4 (halve 6))
```

produces:

```text
FAIL halving six
  expected: 4
  actual:   3
```

The label identifies the case. `expected` is the value supplied by the test;
`actual` is what the expression returned. Here the function is correct and the
expectation should be `3`. Fix the expectation or the function as appropriate,
then rerun the file to reach the remaining checks.

<!-- generated from testing.md.spec sha256:7326b87015edd641e8f6d45e9f2ba93a037af18b1c39c7d8dcb9610e8b873b78 -->
