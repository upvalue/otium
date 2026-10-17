/* ✨ Browser embedding: one persistent C runtime per worker. */
#define OT_INTERNAL
#include "otium.h"
#include <emscripten.h>
#include <string.h>

static ots* web_state;

EM_JS(void, web_output, (const char* bytes, size_t length), {
  Module['onOutput'](UTF8ToString(bytes, length));
});

static void web_writer(void* userdata, const char* bytes, size_t length) {
  (void)userdata;
  web_output(bytes, length);
}

EMSCRIPTEN_KEEPALIVE int otium_web_init(int stress) {
  ot_destroy(web_state);
  ot_config config = ot_config_default();
  config.gc_stress = stress != 0;
  web_state = ot_create(&config);
  if (web_state == NULL) return 0;
  ot_set_writer(web_state, web_writer, NULL);
  return 1;
}

EMSCRIPTEN_KEEPALIVE int otium_web_eval(const char* source) {
  if (web_state == NULL) return 0;
  ot_clear_condition(web_state);
  otv result = ot_nil;
  OT_FRAME(web_state, &result);
  bool ok = ot_eval_src(web_state, source, strlen(source), "<playground>", &result);
  if (!ok) {
    int status;
    if (ot_exit_status(web_state, &status)) {
      web_output("Session exited. Reset to start again.\n", 37);
      OT_FRAME_POP(web_state);
      return 2;
    }
    result = ot_condition(web_state);
  }
  ot_repr_to(web_state, result, false, web_writer, NULL);
  web_output("\n", 1);
  OT_FRAME_POP(web_state);
  return ok ? 1 : 0;
}
