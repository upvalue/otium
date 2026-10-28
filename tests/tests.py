"""Otium check programs."""
from harness import case

case('optional parameters', 'tests/optional/checks.ot')
case('array equality', 'tests/equality/arrays.ot')
case('expander', 'tests/expander/checks.ot')
case('classes', 'tests/classes/checks.ot')
case('records', 'tests/records/checks.ot', paths=['tests/records/fixtures'])
case('match', 'tests/match/checks.ot')
case('ui', 'tests/ui/checks.ot')
case('ui calculator', 'tests/ui/calculator-checks.ot', paths=['examples'])
case('ui editing', 'tests/ui/editing-checks.ot', paths=['examples'])
case('ui policy', 'tests/ui/policy-checks.ot')
case('otcl', 'tests/otcl.ot', paths=['examples/otcl'])
case('format', 'tests/format/checks.ot')
case('format corpus', 'tests/format/corpus.ot',
     arguments=['lib', 'examples', 'benchmarks'], timeout=120)
