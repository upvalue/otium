"""Collector checks that need particular collector settings."""
from harness import case

# Every collection checked against a separate trace, which covers evacuation:
# a reachable object left forwarded means a slot the collector didn't update.
VERIFY = {'OTIUM_GC_VERIFY': '1'}

case('gc collections verified', 'tests/gc/mutation.ot', options=['--c.heap-init=1048576'],
     env=VERIFY, stdout=':ok')
case('gc collections verified vm', 'tests/gc/mutation.ot',
     options=['--c.vm', '--c.heap-init=1048576'], env=VERIFY, stdout=':ok')
