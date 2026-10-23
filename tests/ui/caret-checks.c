#include "../../native/ui/text.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static void check(bool ok, const char* message) {
  if (!ok) {
    fprintf(stderr, "%s: %s\n", message, SDL_GetError());
    exit(1);
  }
}

int main(void) {
  float positions[32];
  for (int scale = 1; scale <= 2; ++scale) {
    TTF_Text* text = otui_text_get("AV", 0, 32, -1, scale);
    check(otui_text_carets(text, positions, 3), "kerning carets");
    TTF_SubString second;
    check(TTF_GetTextSubString(text, 1, &second), "shaped V");
    check(positions[1] == second.rect.x, "caret follows shaped V origin");
    float shaped = positions[1];
    text = otui_text_get("A", 0, 32, -1, scale);
    check(otui_text_carets(text, positions, 2), "standalone A");
    check(shaped < positions[1], "kerning differs from prefix measurement");

    text = otui_text_get("office", 0, 32, -1, scale);
    check(otui_text_carets(text, positions, 7), "ligature carets");
    TTF_SubString ligature;
    check(TTF_GetTextSubString(text, 1, &ligature), "ffi cluster");
    check(ligature.length == 3, "font shapes ffi as a ligature");
    check(positions[1] == ligature.rect.x && positions[2] > positions[1] &&
              positions[3] > positions[2] && positions[3] < positions[4],
          "interior ligature carets stay in shaped cluster");

    text = otui_text_get("éλA", 0, 32, -1, scale);
    check(otui_text_carets(text, positions, 4), "UTF-8 codepoint carets");
    check(TTF_GetTextSubString(text, 4, &second), "UTF-8 byte offset");
    check(positions[2] == second.rect.x, "byte offsets become codepoint indices");
    check(!otui_text_carets(text, positions, 3), "undersized buffer rejected");
    // Spaces must keep their advances even at the end of an unwrapped line.
    const char* spaced[] = {"   ", "a  ", " a b ", "é  λ "};
    for (int sample = 0; sample < 4; ++sample) {
      text = otui_text_get(spaced[sample], 1, 18, -1, scale);
      int count = (int)SDL_utf8strlen(spaced[sample]);
      check(otui_text_carets(text, positions, count + 1), "whitespace carets");
      for (int j = 1; j <= count; ++j)
        check(positions[j] > positions[j - 1], "spaces advance the caret");
      check(positions[count] == otui_text_size(text, 0), "trailing caret includes spaces");
    }
    text = otui_text_get("", 0, 32, -1, scale);
    check(otui_text_carets(text, positions, 1) && positions[0] == 0, "empty caret");
  }
  otui_text_clear();
  puts("UI shaped caret checks passed");
  return 0;
}
