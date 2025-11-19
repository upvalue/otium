// prog-spacedemo.cpp - DOS Space Demo port
#include "ot/lib/frame-manager.hpp"
#include "ot/lib/gfx-util.hpp"
#include "ot/lib/math.hpp"
#include "ot/lib/messages.hpp"
#include "ot/user/gen/graphics-client.hpp"
#include "ot/user/local-storage.hpp"
#include "ot/user/prog.h"
#include "ot/user/string.hpp"
#include "ot/user/user.hpp"

// Constants
static const int DEMO_WIDTH = 1024;
static const int DEMO_HEIGHT = 700;
static const int MAX_BACKGROUND_STARS = 100; // More stars for larger screen
static const int MAX_DEBRIS = 50;            // More debris for larger screen
static const int STREAK_FRAMES = 90;

// Color constants (BGRA format: 0xAARRGGBB)
static const uint32_t COLOR_BLACK = 0xFF000000;
static const uint32_t COLOR_WHITE = 0xFFFFFFFF;

// Star system names (from demo.js)
static const char *STAR_NAMES[] = {
    "Gliese 581",       "Gliese 876",   "Gliese 832",   "Gliese 667C",      "Gliese 163",       "Gliese 357",
    "Gliese 180",       "Gliese 682",   "Gliese 674",   "Gliese 436",       "Lacaille 9352",    "Lacaille 8760",
    "Lalande 21185",    "Luyten 726-8", "Luyten 789-6", "Groombridge 34",   "Groombridge 1618", "Kapteyn's Star",
    "Barnard's Star",   "Wolf 359",     "Ross 128",     "Ross 154",         "Ross 248",         "Ross 614",
    "Teegarden's Star", "Struve 2398",  "Kruger 60",    "61 Cygni",         "82 Eridani",       "36 Ophiuchi",
    "70 Ophiuchi",      "Stein 2051",   "TRAPPIST-1",   "Proxima Centauri", "Epsilon Eridani",  "Tau Ceti",
    "40 Eridani",       "Wolf 1061",    "Kepler-442",   "Kepler-452"};

static const int NUM_STAR_NAMES = sizeof(STAR_NAMES) / sizeof(STAR_NAMES[0]);

// Star types
enum StarType { STAR_YELLOW = 0, STAR_BLUE = 1, STAR_RED = 2 };

// Background star
struct BgStar {
  int x, y;
  uint32_t color;
};

// Debris particle
struct Debris {
  float x, y, z;
  float speed;
  uint32_t color;
  bool active;
};

// Central star
struct Star {
  float x, y, z;
  float speed;
  StarType type;
  int name_index;
  bool active;
};

// Simple PRNG (xorshift32)
static uint32_t rng_state = 0x12345678;

uint32_t xorshift32() {
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 17;
  rng_state ^= rng_state << 5;
  return rng_state;
}

// Random float between 0 and 1
float randf() { return (float)(xorshift32() % 10000) / 10000.0f; }

// Get gradient color for star type
void get_star_colors(StarType type, uint32_t &center, uint32_t &edge) {
  switch (type) {
  case STAR_YELLOW:
    center = 0xFFD0FFFF; // Yellow-white
    edge = 0xFF0080E0;   // Orange
    break;
  case STAR_BLUE:
    center = 0xFFFFD0A0; // Light blue
    edge = 0xFFA07020;   // Dark blue
    break;
  case STAR_RED:
    center = 0xFFC0D0FF; // Pink-red
    edge = 0xFF2010E0;   // Dark red
    break;
  }
}

// Get debris color (brownish/grayish tones)
uint32_t get_debris_color(int index) {
  uint32_t base_colors[] = {
      0xFF545D62, // Gray-brown
      0xFF656575, // Light gray
      0xFF79788C, // Blue-gray
      0xFF897989, // Brown-gray
      0xFFA49BAF, // Light brown
      0xFFB1A3BB, // Tan
      0xFFC9B6CC, // Light tan
      0xFFD6D3EA, // Very light gray
  };
  return base_colors[index % 8];
}

// SpaceDemo-specific storage
struct SpaceDemoStorage : public LocalStorage {
  BgStar bg_stars[MAX_BACKGROUND_STARS];
  Debris debris[MAX_DEBRIS];
  Star central_star;
  int cycle;
  int hyperspace_cycle_time;
  uint32_t *saved_screen;

  SpaceDemoStorage() {
    // Don't need TLSF allocator, just basic storage
    process_storage_init(1);
    saved_screen = nullptr;
  }
};

// Initialize background stars
void init_background_stars(SpaceDemoStorage *s) {
  for (int i = 0; i < MAX_BACKGROUND_STARS; i++) {
    s->bg_stars[i].x = xorshift32() % DEMO_WIDTH;
    s->bg_stars[i].y = xorshift32() % DEMO_HEIGHT;
    // Purple/gray shades
    uint8_t brightness = 64 + (xorshift32() % 8) * 8;
    s->bg_stars[i].color = 0xFF000000 | (brightness << 16) | (brightness << 8) | (brightness + 32);
  }
}

// Draw background stars
void draw_background_stars(SpaceDemoStorage *s, gfx::GfxUtil &gfx, int offset_x, int offset_y) {
  for (int i = 0; i < MAX_BACKGROUND_STARS; i++) {
    gfx.put_pixel(s->bg_stars[i].x + offset_x, s->bg_stars[i].y + offset_y, s->bg_stars[i].color);
  }
}

// Reset debris particle
void reset_debris(Debris *d) {
  float angle = randf() * 3.14159f * 2.0f;
  // Scale radius for larger screen (original: 60-150, scaled: 210-525)
  float radius = randf() * 210.0f + 315.0f;
  d->x = ou_cosf(angle) * radius;
  d->y = ou_sinf(angle) * radius;
  d->z = randf() * 800.0f + 200.0f;
  d->speed = (randf() * 3.0f + 1.0f) * 0.8f;
  d->color = get_debris_color(xorshift32());
  d->active = true;
}

// Initialize debris
void init_debris(SpaceDemoStorage *s) {
  for (int i = 0; i < MAX_DEBRIS; i++) {
    reset_debris(&s->debris[i]);
  }
}

// Update and draw debris
void update_debris(SpaceDemoStorage *s, gfx::GfxUtil &gfx, int offset_x, int offset_y) {
  for (int i = 0; i < MAX_DEBRIS; i++) {
    Debris *d = &s->debris[i];
    if (!d->active)
      continue;

    d->z -= d->speed;

    if (d->z < 1.0f) {
      reset_debris(d);
    }

    // 3D to 2D projection
    float scale = 256.0f / d->z;
    int screen_x = (int)(d->x * scale) + DEMO_WIDTH / 2;
    int screen_y = (int)(d->y * scale) + DEMO_HEIGHT / 2;

    if (screen_x >= 0 && screen_x < DEMO_WIDTH && screen_y >= 0 && screen_y < DEMO_HEIGHT) {
      // Brightness based on distance (closer = brighter)
      float brightness = (1000.0f - d->z) / 1000.0f;
      if (brightness > 1.0f)
        brightness = 1.0f;

      uint32_t color = gfx::GfxUtil::interpolate_color(COLOR_BLACK, d->color, brightness);

      gfx.put_pixel(screen_x + offset_x, screen_y + offset_y, color);

      // Double pixel for close debris
      if (d->z < 300.0f) {
        gfx.put_pixel(screen_x + 1 + offset_x, screen_y + offset_y, color);
      }
    }
  }
}

// Initialize hyperspace timer
int init_hyperspace_timer() { return 300 + (xorshift32() % 301); }

// Initialize central star
void init_star(SpaceDemoStorage *s) {
  float angle = randf() * 3.14159f * 2.0f;
  // Scale radius for larger screen (original: 50-120, scaled: 175-420)
  float radius = randf() * 245.0f + 175.0f;
  s->central_star.x = ou_cosf(angle) * radius;
  s->central_star.y = ou_sinf(angle) * radius;
  s->central_star.z = 1000.0f;

  float arrival_time = (float)s->hyperspace_cycle_time * 0.9f;
  s->central_star.speed = 999.0f / arrival_time;

  s->central_star.name_index = xorshift32() % NUM_STAR_NAMES;
  s->central_star.type = (StarType)(s->central_star.name_index % 3);
  s->central_star.active = true;
}

// Update and draw central star
void update_star(SpaceDemoStorage *s, gfx::GfxUtil &gfx, int offset_x, int offset_y) {
  if (!s->central_star.active)
    return;

  Star *star = &s->central_star;
  star->z -= star->speed;
  if (star->z < 1.0f) {
    star->z = 1.0f;
  }

  // 3D to 2D projection
  float scale = 256.0f / star->z;
  int screen_x = (int)(star->x * scale) + DEMO_WIDTH / 2;
  int screen_y = (int)(star->y * scale) + DEMO_HEIGHT / 2;

  // Size grows as star approaches (scaled for larger screen: 105-245)
  int size = (int)(140.0f * (1000.0f - star->z) / 1000.0f) + 105;

  // Get gradient colors for star type
  uint32_t center_color, edge_color;
  get_star_colors(star->type, center_color, edge_color);

  // Draw gradient circle centered in demo area
  gfx.draw_gradient_circle(screen_x + offset_x, screen_y + offset_y, size, center_color, edge_color);
}

// Hyperspace warp effect
void hyperspace_warp(SpaceDemoStorage *s, gfx::GfxUtil &gfx, GraphicsClient &client, int offset_x, int offset_y) {
  // saved_screen should already be allocated at startup

  // Save current screen (just the demo area)
  uint32_t *fb = gfx.framebuffer();
  int fb_width = gfx.width();
  for (int y = 0; y < DEMO_HEIGHT; y++) {
    for (int x = 0; x < DEMO_WIDTH; x++) {
      s->saved_screen[y * DEMO_WIDTH + x] = fb[(y + offset_y) * fb_width + (x + offset_x)];
    }
  }

  graphics::FrameManager fm(60); // Run warp at 60 FPS for smoothness
  int center_x = DEMO_WIDTH / 2;
  int center_y = DEMO_HEIGHT / 2;

  // Streak animation (90 frames)
  for (int frame = 0; frame < STREAK_FRAMES; frame++) {
    if (fm.begin_frame()) {
      float streak_amount = (float)frame / (float)STREAK_FRAMES;

      // Clear demo area
      gfx.fill_rect(offset_x, offset_y, DEMO_WIDTH, DEMO_HEIGHT, COLOR_BLACK);

      // Draw streaks
      for (int sy = 0; sy < DEMO_HEIGHT; sy++) {
        for (int sx = 0; sx < DEMO_WIDTH; sx++) {
          uint32_t color = s->saved_screen[sy * DEMO_WIDTH + sx];
          if (color == COLOR_BLACK)
            continue;

          int dx = sx - center_x;
          int dy = sy - center_y;

          int streak_length = (int)(streak_amount * 30.0f);

          for (int i = 0; i <= streak_length; i++) {
            float t = 1.0f + ((float)i / 10.0f) * streak_amount;
            int draw_x = (int)(center_x + dx * t);
            int draw_y = (int)(center_y + dy * t);

            if (draw_x >= 0 && draw_x < DEMO_WIDTH && draw_y >= 0 && draw_y < DEMO_HEIGHT) {
              uint32_t draw_color = color;

              // Fade outer portions
              if (i > streak_length / 2) {
                int fade_amount = (i - streak_length / 2) / 3;
                // Darken color
                uint8_t r = (color >> 16) & 0xFF;
                uint8_t g = (color >> 8) & 0xFF;
                uint8_t b = color & 0xFF;
                r = r > fade_amount * 10 ? r - fade_amount * 10 : 0;
                g = g > fade_amount * 10 ? g - fade_amount * 10 : 0;
                b = b > fade_amount * 10 ? b - fade_amount * 10 : 0;
                draw_color = 0xFF000000 | (r << 16) | (g << 8) | b;
              }

              gfx.put_pixel(draw_x + offset_x, draw_y + offset_y, draw_color);
            }
          }
        }
      }

      client.flush();
      fm.end_frame();
    }
    ou_yield();
  }

  // Fade to darkness (15 frames)
  for (int fade = 0; fade < 15; fade++) {
    if (fm.begin_frame()) {
      uint32_t fade_color = fade < 8 ? 0xFF010101 : COLOR_BLACK;
      gfx.fill_rect(offset_x, offset_y, DEMO_WIDTH, DEMO_HEIGHT, fade_color);
      client.flush();
      fm.end_frame();
    }
    ou_yield();
  }

  // Hold darkness (10 frames)
  for (int hold = 0; hold < 10; hold++) {
    if (fm.begin_frame()) {
      gfx.fill_rect(offset_x, offset_y, DEMO_WIDTH, DEMO_HEIGHT, COLOR_BLACK);
      client.flush();
      fm.end_frame();
    }
    ou_yield();
  }

  // Reset star and debris
  init_star(s);
  init_debris(s);
}

void spacedemo_main() {
  oprintf("SPACEDEMO: Starting DOS Space Demo\n");

  // Get the storage page and initialize SpaceDemoStorage
  void *storage_page = ou_get_storage().as_ptr();
  SpaceDemoStorage *s = new (storage_page) SpaceDemoStorage();

  // Yield to let graphics driver initialize
  ou_yield();

  // Look up graphics driver
  Pid gfx_pid = ou_proc_lookup("graphics");
  if (gfx_pid == PID_NONE) {
    oprintf("SPACEDEMO: Failed to find graphics driver\n");
    ou_exit();
  }

  GraphicsClient client(gfx_pid);

  // Get framebuffer info
  auto fb_result = client.get_framebuffer();
  if (fb_result.is_err()) {
    oprintf("SPACEDEMO: Failed to get framebuffer: %d\n", fb_result.error());
    ou_exit();
  }

  auto fb_info = fb_result.value();
  uint32_t *fb = (uint32_t *)fb_info.fb_ptr;
  int width = (int)fb_info.width;
  int height = (int)fb_info.height;

  oprintf("SPACEDEMO: Framebuffer %dx%d, demo rendering at %dx%d\n", width, height, DEMO_WIDTH, DEMO_HEIGHT);

  // Calculate centering offsets (will be 0,0 if demo fills screen exactly)
  int offset_x = (width - DEMO_WIDTH) / 2;
  int offset_y = (height - DEMO_HEIGHT) / 2;

  // Create graphics utility wrapper
  gfx::GfxUtil gfx(fb, width, height);

  // Allocate saved screen buffer upfront using ou_alloc_page
  // Need 1024x700x4 = 2,867,200 bytes = 700 pages (assuming 4KB pages)
  int total_bytes = DEMO_WIDTH * DEMO_HEIGHT * 4;
  int num_pages = (total_bytes + 4095) / 4096; // Round up

  s->saved_screen = (uint32_t *)ou_alloc_page();
  if (s->saved_screen == nullptr) {
    oprintf("SPACEDEMO: Failed to allocate saved screen buffer (page 1)\n");
    ou_exit();
  }

  // Allocate additional pages for the buffer
  for (int i = 1; i < num_pages; i++) {
    void *extra_page = ou_alloc_page();
    if (extra_page == nullptr) {
      oprintf("SPACEDEMO: Failed to allocate saved screen buffer (page %d/%d)\n", i + 1, num_pages);
      ou_exit();
    }
  }

  oprintf("SPACEDEMO: Allocated saved screen buffer (%d KB)\n", (DEMO_WIDTH * DEMO_HEIGHT * 4) / 1024);

  // Initialize demo state
  init_background_stars(s);
  init_debris(s);
  s->hyperspace_cycle_time = init_hyperspace_timer();
  init_star(s);
  s->cycle = 0;

  // Run demo at 60 FPS
  graphics::FrameManager fm(60);

  while (true) {
    if (fm.begin_frame()) {
      // Clear entire screen to black
      gfx.clear(COLOR_BLACK);

      // Draw demo content centered
      draw_background_stars(s, gfx, offset_x, offset_y);
      update_star(s, gfx, offset_x, offset_y);
      update_debris(s, gfx, offset_x, offset_y);

      // Draw star system name or jump warning
      if (s->cycle >= s->hyperspace_cycle_time - 60) {
        gfx.draw_text(offset_x + 20, offset_y + DEMO_HEIGHT - 30, "JUMP ENGAGED", 0xFFAA6654, 3);
      } else {
        gfx.draw_text(offset_x + 20, offset_y + DEMO_HEIGHT - 30, STAR_NAMES[s->central_star.name_index], 0xFFAA6654,
                      3);
      }

      // Flush to display
      client.flush();
      fm.end_frame();

      s->cycle++;

      // Check for hyperspace
      if (s->cycle >= s->hyperspace_cycle_time) {
        hyperspace_warp(s, gfx, client, offset_x, offset_y);
        s->cycle = 0;
        s->hyperspace_cycle_time = init_hyperspace_timer();
      }
    }

    // Always yield to cooperate with other processes
    ou_yield();
  }

  ou_exit();
}
