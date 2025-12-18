// prog-spacedemo.cpp - DOS Space Demo port
#include "ot/lib/app-framework.hpp"
#include "ot/lib/frame-manager.hpp"
#include "ot/lib/keyboard-utils.hpp"
#include "ot/lib/math.hpp"
#include "ot/lib/messages.hpp"
#include "ot/user/gen/graphics-client.hpp"
#include "ot/user/gen/keyboard-client.hpp"
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

// Get retro palette colors for star type (limited color bands)
void get_star_palette(StarType type, uint32_t palette[8]) {
  switch (type) {
  case STAR_YELLOW:
    // Yellow/orange star - warm tones
    palette[0] = 0xFF002A62; // Dark orange
    palette[1] = 0xFF093B75;
    palette[2] = 0xFF124F85;
    palette[3] = 0xFF20659E;
    palette[4] = 0xFF2E88BA;
    palette[5] = 0xFF39AAD1;
    palette[6] = 0xFF4BD2E8;
    palette[7] = 0xFF4FF6FF; // Bright yellow-white
    break;
  case STAR_BLUE:
    // Blue star - cool tones
    palette[0] = 0xFF401B00; // Dark blue
    palette[1] = 0xFF5F3103;
    palette[2] = 0xFF7C4807;
    palette[3] = 0xFFA25D10;
    palette[4] = 0xFFC07614;
    palette[5] = 0xFFEA9740;
    palette[6] = 0xFFF1B155;
    palette[7] = 0xFFFFCC6D; // Bright cyan
    break;
  case STAR_RED:
    // Red/pink star
    palette[0] = 0xFF26095F; // Dark red
    palette[1] = 0xFF34246E;
    palette[2] = 0xFF474690;
    palette[3] = 0xFF5F60A7;
    palette[4] = 0xFF647DBD;
    palette[5] = 0xFF7097CE;
    palette[6] = 0xFF7CB6ED;
    palette[7] = 0xFF7FD4ED; // Bright pink
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
  KeyboardClient kbdc;

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
void draw_background_stars(SpaceDemoStorage *s, app::Framework &gfx, int offset_x, int offset_y) {
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
void update_debris(SpaceDemoStorage *s, app::Framework &gfx, int offset_x, int offset_y) {
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

      uint32_t color = app::Framework::interpolate_color(COLOR_BLACK, d->color, brightness);

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

// Draw retro palette-based circle with discrete color bands
void draw_palette_circle(app::Framework &gfx, int cx, int cy, int radius, uint32_t palette[8]) {
  // Draw filled circle with 8 discrete color bands
  int radius_sq = radius * radius;
  for (int y = -radius; y <= radius; y++) {
    for (int x = -radius; x <= radius; x++) {
      int dist_sq = x * x + y * y;
      if (dist_sq <= radius_sq) {
        // Map squared distance to one of 8 color bands (approximate)
        int band = (int)((float)dist_sq / (float)radius_sq * 7.99f);
        if (band > 7)
          band = 7;
        if (band < 0)
          band = 0;

        // Invert so center is brightest (palette[7]) and edge is darkest (palette[0])
        uint32_t color = palette[7 - band];

        int px = cx + x;
        int py = cy + y;
        if (px >= 0 && px < gfx.width() && py >= 0 && py < gfx.height()) {
          gfx.put_pixel(px, py, color);
        }
      }
    }
  }
}

// Update and draw central star
void update_star(SpaceDemoStorage *s, app::Framework &gfx, int offset_x, int offset_y) {
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

  // Get palette colors for star type
  uint32_t palette[8];
  get_star_palette(star->type, palette);

  // Draw retro palette-based circle centered in demo area
  draw_palette_circle(gfx, screen_x + offset_x, screen_y + offset_y, size, palette);
}

// Hyperspace warp effect
void hyperspace_warp(SpaceDemoStorage *s, app::Framework &gfx, GraphicsClient &client, int offset_x, int offset_y) {
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
  for (int frame = 0; frame < STREAK_FRAMES;) {
    if (fm.begin_frame()) {
      float streak_amount = (float)frame / (float)STREAK_FRAMES;

      // Clear demo area
      gfx.fill_rect(offset_x, offset_y, DEMO_WIDTH, DEMO_HEIGHT, COLOR_BLACK);

      // Draw streaks
      for (int sy = 0; sy < DEMO_HEIGHT; sy++) {
        for (int sx = 0; sx < DEMO_WIDTH; sx++) {
          uint32_t color = s->saved_screen[sy * DEMO_WIDTH + sx];
          // Skip pure black pixels (background)
          uint8_t r = (color >> 16) & 0xFF;
          uint8_t g = (color >> 8) & 0xFF;
          uint8_t b = color & 0xFF;
          if (r == 0 && g == 0 && b == 0)
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
      frame++;
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

  // Look up keyboard driver
  Pid kbd_pid = ou_proc_lookup("keyboard");
  if (kbd_pid == PID_NONE) {
    oprintf("SPACEDEMO: Failed to find keyboard driver\n");
    ou_exit();
  }

  GraphicsClient client(gfx_pid);
  s->kbdc.set_pid(kbd_pid);

  // Register with graphics driver
  auto reg_result = client.register_app("spacedemo");
  if (reg_result.is_err()) {
    oprintf("SPACEDEMO: Failed to register with graphics driver: %d\n", reg_result.error());
    ou_exit();
  }
  oprintf("SPACEDEMO: Registered as app %lu\n", reg_result.value());

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
  app::Framework gfx(fb, width, height);

  // Allocate saved screen buffer upfront using ou_alloc_page
  // Need 1024x700x4 = 2,867,200 bytes = 700 pages (assuming 4KB pages)
  int total_bytes = DEMO_WIDTH * DEMO_HEIGHT * 4;
  int num_pages = (total_bytes + 4095) / 4096; // Round up

  // Allocate pages contiguously by calling ou_alloc_page multiple times
  // and verifying they are contiguous (on most systems they should be)
  s->saved_screen = (uint32_t *)ou_alloc_page();
  if (s->saved_screen == nullptr) {
    oprintf("SPACEDEMO: Failed to allocate saved screen buffer (page 1)\n");
    ou_exit();
  }

  uint8_t *expected_addr = (uint8_t *)s->saved_screen + 4096;
  for (int i = 1; i < num_pages; i++) {
    void *extra_page = ou_alloc_page();
    if (extra_page == nullptr) {
      oprintf("SPACEDEMO: Failed to allocate saved screen buffer (page %d/%d)\n", i + 1, num_pages);
      ou_exit();
    }
    // Warn if pages aren't contiguous (they should be in practice)
    if (extra_page != expected_addr) {
      oprintf("SPACEDEMO: Warning - page %d not contiguous (expected %p, got %p)\n", i + 1, expected_addr, extra_page);
    }
    expected_addr += 4096;
  }

  oprintf("SPACEDEMO: Allocated saved screen buffer (%d KB, %d pages)\n", total_bytes / 1024, num_pages);

  // Initialize demo state
  init_background_stars(s);
  init_debris(s);
  s->hyperspace_cycle_time = init_hyperspace_timer();
  init_star(s);
  s->cycle = 0;

  // Run demo at 60 FPS
  graphics::FrameManager fm(60);

  bool running = true;
  while (running) {
    // Check if we should render (are we the active app?)
    auto should = client.should_render();
    if (should.is_err() || should.value() == 0) {
      // Not active, just yield
      ou_yield();
      continue;
    }

    if (fm.begin_frame()) {
      // Poll keyboard
      auto key_result = s->kbdc.poll_key();
      if (key_result.is_ok()) {
        auto key_data = key_result.value();
        if (key_data.has_key) {
          // Pass key to graphics server for global hotkeys (Alt+1-9 app switching)
          gfx.pass_key_to_server(client, key_data.code, key_data.flags);

          // Check for Alt+Q to quit
          if ((key_data.flags & KEY_FLAG_ALT) && (key_data.code == KEY_Q)) {
            oprintf("SPACEDEMO: Alt+Q pressed, exiting\n");
            running = false;
          }
        }
      }
      // Clear entire screen to black
      gfx.clear(COLOR_BLACK);

      // Draw demo content centered
      draw_background_stars(s, gfx, offset_x, offset_y);
      update_star(s, gfx, offset_x, offset_y);
      update_debris(s, gfx, offset_x, offset_y);

      // Draw star system name or jump warning (using blit16 for retro aesthetic)
      if (s->cycle >= s->hyperspace_cycle_time - 60) {
        gfx.draw_blit16_text(offset_x + 20, offset_y + DEMO_HEIGHT - 30, "JUMP ENGAGED", 0xFFAA6654, 3);
      } else {
        gfx.draw_blit16_text(offset_x + 20, offset_y + DEMO_HEIGHT - 30, STAR_NAMES[s->central_star.name_index],
                             0xFFAA6654, 3);
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

  // Unregister before exit
  client.unregister_app();

  oprintf("SPACEDEMO: Exiting\n");
  ou_exit();
}
