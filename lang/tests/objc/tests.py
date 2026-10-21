"""Objective-C messaging through otium.objc, on macOS only."""
import sys

from harness import case, defaults

defaults(suite='native', needs=['ffi'])

if sys.platform == 'darwin':
    case('objc', 'tests/objc/objc.ot', stdout='objc: 9 checks passed\n')
