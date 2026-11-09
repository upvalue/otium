# ✨ Testing

Tests check that your code still behaves as expected after a change. With
`otium.test`, a test is an ordinary Otium program. Load the namespace you want
to test, compare results with expected values, and print a summary.

## Write checks

`check` takes the expected value first and the
actual value second. You can put a label before them to identify a failure. It
compares values with `equal?`, so collections are compared structurally.

This complete test program tests `otium.json`:

```otium
(ns guide.json-test)
(require otium.json :as json)
(require otium.test :refer (check fails? report))

(check "integer" 42 (json/parse "42"))
(check "array" [1 2 3] (json/parse "[1,2,3]"))
(check "false" #f (json/parse "false"))

(report "json")
```

For your own code, use a separate test namespace and require your application
namespace. Call its functions in the checks. See [Namespaces](namespaces.md) for
module loading.

Checks run as the program evaluates them. Passing checks are silent.
`report` prints the number of successful checks
so far; its string argument names the summary.

## Run the program

Save the program in an `.ot` file. From the repository root, run `./otium`
followed by the saved file's path. If your application modules need an
additional load path, add `-L` followed by the directory containing them.

The program above exits successfully and prints:

```text
json: 3 checks passed
```

Checks share a count across the process. `report` prints it without resetting
it. Running the test program again in a fresh process starts a new count.

## Check expected errors

`fails?` returns `#t` when the operation raises
a condition and `#f` when it returns normally. Give it a zero-argument function
so the operation runs inside the error check.

Insert this before the report:

```otium
(check "reject trailing comma"
       #t
       (fails? (fn () (json/parse "[1,]"))))
```

The summary now reads `json: 4 checks passed`.

This checks that a condition was raised, not its type or message. For more
specific assertions, catch the condition and inspect it; see [Errors and
recovery](errors.md).

## Read a failure

Changing the first check's expected value to `41` deliberately makes it fail:

```otium
(check "integer" 41 (json/parse "42"))
```

The output is:

```text
FAIL integer
  expected: 41
  actual:   42
```

The label identifies the check. `expected` is the value you supplied; `actual`
is the result of the expression.

A failed check immediately exits with status `1`, so later checks and the final
report do not run. Inspect the expectation and the function under test. Correct
the mistake, then rerun the program.

<!-- generated from testing.md.spec sha256:7326b87015edd641e8f6d45e9f2ba93a037af18b1c39c7d8dcb9610e8b873b78 -->
