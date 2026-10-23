"""Module search order, suffixes, failed and circular loads, and reload."""
from harness import case

case('modules', 'tests/modules/checks.ot', paths=['tests/modules/first', 'tests/modules/second'])
