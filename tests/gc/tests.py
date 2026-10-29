"""Collector checks that need particular collector settings."""
from harness import case

# Generational collection with every minor collection checked against a full
# trace, in a heap small enough for minor collections to run often.
VERIFY = {'OTIUM_GC_GENERATIONAL': '1', 'OTIUM_GC_VERIFY': '1'}

case('gc write barriers', 'tests/gc/mutation.ot', options=['--c.heap-init=1048576'], env=VERIFY,
     stdout=':ok')
case('gc write barriers vm', 'tests/gc/mutation.ot', options=['--c.vm', '--c.heap-init=1048576'],
     env=VERIFY, stdout=':ok')
