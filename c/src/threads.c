#include "internal.h"

/* Green threads: many threads of Otium code on one native thread. See
 * scratch/green-threads.md. */

void oti_visit_threads(ot_state* rt, oti_root_visitor visit, void* context) {
  (void)rt;
  (void)visit;
  (void)context;
}
