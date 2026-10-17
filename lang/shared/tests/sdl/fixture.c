/* An independent event producer: the next poll overwrites text backing bytes. */
#include <SDL3/SDL.h>
#include <string.h>
static char text[32];
static unsigned polls;
bool fixture_poll(SDL_Event *event) {
    memset(event, 0, sizeof(*event));
    strcpy(text, polls++ ? "changed" : "first text");
    event->type = SDL_EVENT_TEXT_INPUT;
    event->text.timestamp = 123;
    event->text.text = text;
    return true;
}
