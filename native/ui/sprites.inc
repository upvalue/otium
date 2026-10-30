// Coverage masks for characters that should fill their terminal cell exactly:
// box drawing (U+2500-257F), block elements (U+2580-259F), braille
// (U+2800-28FF) and the powerline triangles (U+E0B0-E0B3). Included by
// renderer.c; masks are w by h bytes, one byte of coverage per pixel.

static bool sprite_supported(Uint32 cp) {
  return (cp >= 0x2500 && cp <= 0x259f) || (cp >= 0x2800 && cp <= 0x28ff) ||
         (cp >= 0xe0b0 && cp <= 0xe0b3);
}

static void sprite_fill(Uint8* m, int w, int h, int x0, int y0, int x1, int y1, Uint8 a) {
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > w) x1 = w;
  if (y1 > h) y1 = h;
  for (int y = y0; y < y1; ++y)
    for (int x = x0; x < x1; ++x)
      if (m[y * w + x] < a) m[y * w + x] = a;
}

// Fill pixels by a predicate on their position, sampled 4x4 for antialiasing.
typedef bool (*SpriteTest)(float x, float y, const float* k);

static void sprite_shape(Uint8* m, int w, int h, SpriteTest inside, const float* k) {
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      int hits = 0;
      for (int sy = 0; sy < 4; ++sy)
        for (int sx = 0; sx < 4; ++sx) hits += inside(x + (sx + 0.5f) / 4, y + (sy + 0.5f) / 4, k);
      Uint8 a = (Uint8)(hits * 255 / 16);
      if (m[y * w + x] < a) m[y * w + x] = a;
    }
}

// k: centre x, y, radius, half thickness, and the quadrant's signs.
static bool sprite_in_arc(float x, float y, const float* k) {
  float dx = x - k[0], dy = y - k[1];
  if (dx * k[4] < 0 || dy * k[5] < 0) return false;
  float d = sqrtf(dx * dx + dy * dy) - k[2];
  return d >= -k[3] && d <= k[3];
}

// k: a line through (x0, y0) and (x1, y1), and half thickness.
static bool sprite_in_line(float x, float y, const float* k) {
  float dx = k[2] - k[0], dy = k[3] - k[1];
  float len = sqrtf(dx * dx + dy * dy);
  return fabsf((x - k[0]) * dy - (y - k[1]) * dx) / len <= k[4];
}

// k: w, h, direction (1 points right, -1 left), and half thickness for an
// outline or 0 for solid.
static bool sprite_in_triangle(float x, float y, const float* k) {
  float u = k[2] > 0 ? x / k[0] : 1 - x / k[0];
  float v = fabsf(2 * y / k[1] - 1);
  if (!k[3]) return u <= 1 - v;
  // Distance to the two edges from the far corners to the tip.
  float edge = fabsf(u - (1 - v)) * k[0] / sqrtf(1 + (k[0] * 2 / k[1]) * (k[0] * 2 / k[1]));
  return edge <= k[3] && u <= 1;
}

static bool sprite_in_circle(float x, float y, const float* k) {
  float dx = x - k[0], dy = y - k[1];
  return dx * dx + dy * dy <= k[2] * k[2];
}

// Arms of U+2500-257F, packed 2 bits each: up, right, down, left. 0 none,
// 1 light, 2 heavy, 3 double. Zero entries are dashes, arcs and diagonals.
#define ARMS(u, r, d, l) ((u) << 6 | (r) << 4 | (d) << 2 | (l))
static const Uint8 box_arms[128] = {
    ARMS(0, 1, 0, 1), ARMS(0, 2, 0, 2), ARMS(1, 0, 1, 0), ARMS(2, 0, 2, 0),  // 2500
    0, 0, 0, 0, 0, 0, 0, 0,                                                  // 2504 dashes
    ARMS(0, 1, 1, 0), ARMS(0, 2, 1, 0), ARMS(0, 1, 2, 0), ARMS(0, 2, 2, 0),  // 250C
    ARMS(0, 0, 1, 1), ARMS(0, 0, 1, 2), ARMS(0, 0, 2, 1), ARMS(0, 0, 2, 2),  // 2510
    ARMS(1, 1, 0, 0), ARMS(1, 2, 0, 0), ARMS(2, 1, 0, 0), ARMS(2, 2, 0, 0),  // 2514
    ARMS(1, 0, 0, 1), ARMS(1, 0, 0, 2), ARMS(2, 0, 0, 1), ARMS(2, 0, 0, 2),  // 2518
    ARMS(1, 1, 1, 0), ARMS(1, 2, 1, 0), ARMS(2, 1, 1, 0), ARMS(1, 1, 2, 0),  // 251C
    ARMS(2, 1, 2, 0), ARMS(2, 2, 1, 0), ARMS(1, 2, 2, 0), ARMS(2, 2, 2, 0),  // 2520
    ARMS(1, 0, 1, 1), ARMS(1, 0, 1, 2), ARMS(2, 0, 1, 1), ARMS(1, 0, 2, 1),  // 2524
    ARMS(2, 0, 2, 1), ARMS(2, 0, 1, 2), ARMS(1, 0, 2, 2), ARMS(2, 0, 2, 2),  // 2528
    ARMS(0, 1, 1, 1), ARMS(0, 1, 1, 2), ARMS(0, 2, 1, 1), ARMS(0, 2, 1, 2),  // 252C
    ARMS(0, 1, 2, 1), ARMS(0, 1, 2, 2), ARMS(0, 2, 2, 1), ARMS(0, 2, 2, 2),  // 2530
    ARMS(1, 1, 0, 1), ARMS(1, 1, 0, 2), ARMS(1, 2, 0, 1), ARMS(1, 2, 0, 2),  // 2534
    ARMS(2, 1, 0, 1), ARMS(2, 1, 0, 2), ARMS(2, 2, 0, 1), ARMS(2, 2, 0, 2),  // 2538
    ARMS(1, 1, 1, 1), ARMS(1, 1, 1, 2), ARMS(1, 2, 1, 1), ARMS(1, 2, 1, 2),  // 253C
    ARMS(2, 1, 1, 1), ARMS(1, 1, 2, 1), ARMS(2, 1, 2, 1), ARMS(2, 1, 1, 2),  // 2540
    ARMS(2, 2, 1, 1), ARMS(1, 1, 2, 2), ARMS(1, 2, 2, 1), ARMS(2, 2, 1, 2),  // 2544
    ARMS(1, 2, 2, 2), ARMS(2, 1, 2, 2), ARMS(2, 2, 2, 1), ARMS(2, 2, 2, 2),  // 2548
    0, 0, 0, 0,                                                              // 254C dashes
    ARMS(0, 3, 0, 3), ARMS(3, 0, 3, 0), ARMS(0, 3, 1, 0), ARMS(0, 1, 3, 0),  // 2550
    ARMS(0, 3, 3, 0), ARMS(0, 0, 1, 3), ARMS(0, 0, 3, 1), ARMS(0, 0, 3, 3),  // 2554
    ARMS(1, 3, 0, 0), ARMS(3, 1, 0, 0), ARMS(3, 3, 0, 0), ARMS(1, 0, 0, 3),  // 2558
    ARMS(3, 0, 0, 1), ARMS(3, 0, 0, 3), ARMS(1, 3, 1, 0), ARMS(3, 1, 3, 0),  // 255C
    ARMS(3, 3, 3, 0), ARMS(1, 0, 1, 3), ARMS(3, 0, 3, 1), ARMS(3, 0, 3, 3),  // 2560
    ARMS(0, 3, 1, 3), ARMS(0, 1, 3, 1), ARMS(0, 3, 3, 3), ARMS(1, 3, 0, 3),  // 2564
    ARMS(3, 1, 0, 1), ARMS(3, 3, 0, 3), ARMS(1, 3, 1, 3), ARMS(3, 1, 3, 1),  // 2568
    ARMS(3, 3, 3, 3), 0, 0, 0,                                               // 256C, arcs
    0, 0, 0, 0,                                                              // 2570 arc, diagonals
    ARMS(0, 0, 0, 1), ARMS(1, 0, 0, 0), ARMS(0, 1, 0, 0), ARMS(0, 0, 1, 0),  // 2574
    ARMS(0, 0, 0, 2), ARMS(2, 0, 0, 0), ARMS(0, 2, 0, 0), ARMS(0, 0, 2, 0),  // 2578
    ARMS(0, 2, 0, 1), ARMS(1, 0, 2, 0), ARMS(0, 1, 0, 2), ARMS(2, 0, 1, 0),  // 257C
};

// Strokes across one axis of the cell: where a light/heavy stroke or the two
// strokes of a double line sit, for a cell span of n pixels.
typedef struct {
  int a0, a1;          // A light or heavy stroke.
  int d0, d1, e0, e1;  // A double line's near and far strokes.
} Strokes;

static Strokes sprite_strokes(int n, int weight, int light) {
  int t = weight == 2 ? light * 2 : light;
  Strokes s;
  s.a0 = (n - t) / 2;
  s.a1 = s.a0 + t;
  s.d0 = (n - 3 * light) / 2;
  s.d1 = s.d0 + light;
  s.e1 = s.d0 + 3 * light;
  s.e0 = s.e1 - light;
  return s;
}

// Draw one arm. Horizontal arms run along x, between the cell edge on their
// side (-1 left or up, 1 right or down) and the lines crossing them: the arms
// before (up or left) and after (down or right) them on the other axis.
static void sprite_arm(Uint8* m, int w, int h, bool horizontal, int side, int weight, int before,
                       int after, int light) {
  int n = horizontal ? w : h;
  Strokes own = sprite_strokes(horizontal ? h : w, weight, light);
  int perp = before > after ? before : after;
  Strokes p = sprite_strokes(n, perp, light);
  for (int i = 0; i < (weight == 3 ? 2 : 1); ++i) {
    // How far the stroke reaches, as the first and last pixel past the centre
    // it covers when coming from the left (near) or the right (far).
    int near, far;
    if (!perp) {
      // Straight on, or a half line: meet the opposite arm at the centre.
      near = (n + 1) / 2;
      far = n / 2;
    } else if (perp != 3) {
      // Into a single stroke: cover it.
      near = p.a1;
      far = p.a0;
    } else if (weight != 3) {
      // A single stroke into a double line: reach its far stroke.
      near = p.e1;
      far = p.d0;
    } else if (i == 0 ? before : after) {
      // Double into double, on the side a crossing line leaves from: the
      // inner stroke turns at the near crossing stroke.
      near = p.d1;
      far = p.e0;
    } else {
      // The outer stroke turns at the far one.
      near = p.e1;
      far = p.d0;
    }
    int c0 = weight == 3 ? (i == 0 ? own.d0 : own.e0) : own.a0;
    int c1 = weight == 3 ? (i == 0 ? own.d1 : own.e1) : own.a1;
    int s0 = side < 0 ? 0 : far, s1 = side < 0 ? near : n;
    if (horizontal) sprite_fill(m, w, h, s0, c0, s1, c1, 255);
    else sprite_fill(m, w, h, c0, s0, c1, s1, 255);
  }
}

static void sprite_box(Uint8* m, int w, int h, Uint8 arms, int light) {
  int up = arms >> 6 & 3, right = arms >> 4 & 3, down = arms >> 2 & 3, left = arms & 3;
  if (left) sprite_arm(m, w, h, true, -1, left, up, down, light);
  if (right) sprite_arm(m, w, h, true, 1, right, up, down, light);
  if (up) sprite_arm(m, w, h, false, -1, up, left, right, light);
  if (down) sprite_arm(m, w, h, false, 1, down, left, right, light);
}

static void sprite_dashes(Uint8* m, int w, int h, bool horizontal, int weight, int count, int light) {
  int n = horizontal ? w : h;
  Strokes s = sprite_strokes(horizontal ? h : w, weight, light);
  for (int i = 0; i < count; ++i) {
    int a = n * i / count, b = n * (i + 1) / count;
    int gap = (b - a) / 3 > 0 ? (b - a) / 3 : 1;
    // Centre each dash in its share of the cell, so dashes repeat evenly.
    a += gap / 2;
    b -= gap - gap / 2;
    if (horizontal) sprite_fill(m, w, h, a, s.a0, b, s.a1, 255);
    else sprite_fill(m, w, h, s.a0, a, s.a1, b, 255);
  }
}

static void sprite_draw(Uint32 cp, int w, int h, Uint8* m) {
  int light = h / 16 > 1 ? h / 16 : 1;
  if (cp >= 0x2500 && cp <= 0x257f) {
    Uint8 arms = box_arms[cp - 0x2500];
    if (arms) {
      sprite_box(m, w, h, arms, light);
      return;
    }
    switch (cp) {
      case 0x2504: case 0x2505: sprite_dashes(m, w, h, true, cp - 0x2503, 3, light); return;
      case 0x2506: case 0x2507: sprite_dashes(m, w, h, false, cp - 0x2505, 3, light); return;
      case 0x2508: case 0x2509: sprite_dashes(m, w, h, true, cp - 0x2507, 4, light); return;
      case 0x250a: case 0x250b: sprite_dashes(m, w, h, false, cp - 0x2509, 4, light); return;
      case 0x254c: case 0x254d: sprite_dashes(m, w, h, true, cp - 0x254b, 2, light); return;
      case 0x254e: case 0x254f: sprite_dashes(m, w, h, false, cp - 0x254d, 2, light); return;
      default: break;
    }
    Strokes sx = sprite_strokes(w, 1, light), sy = sprite_strokes(h, 1, light);
    float xc = (sx.a0 + sx.a1) / 2.0f, yc = (sy.a0 + sy.a1) / 2.0f, half = light / 2.0f;
    if (cp >= 0x256d && cp <= 0x2570) {
      // Rounded corners: a quarter circle joined to straight runs to the edges.
      float r = fminf(fminf(xc, w - xc), fminf(yc, h - yc));
      int right = cp == 0x256d || cp == 0x2570, down = cp == 0x256d || cp == 0x256e;
      float k[6] = {right ? xc + r : xc - r, down ? yc + r : yc - r, r, half,
                    right ? -1.0f : 1.0f, down ? -1.0f : 1.0f};
      sprite_shape(m, w, h, sprite_in_arc, k);
      int ky = (int)lroundf(k[1]), kx = (int)lroundf(k[0]);
      if (down) sprite_fill(m, w, h, sx.a0, ky, sx.a1, h, 255);
      else sprite_fill(m, w, h, sx.a0, 0, sx.a1, ky, 255);
      if (right) sprite_fill(m, w, h, kx, sy.a0, w, sy.a1, 255);
      else sprite_fill(m, w, h, 0, sy.a0, kx, sy.a1, 255);
      return;
    }
    // Diagonals.
    if (cp == 0x2571 || cp == 0x2573) sprite_shape(m, w, h, sprite_in_line, (float[]){w, 0, 0, h, half});
    if (cp == 0x2572 || cp == 0x2573) sprite_shape(m, w, h, sprite_in_line, (float[]){0, 0, w, h, half});
    return;
  }
  if (cp >= 0x2580 && cp <= 0x259f) {
    int hx = w / 2, hy = h / 2;
    if (cp == 0x2580) sprite_fill(m, w, h, 0, 0, w, hy, 255);
    else if (cp <= 0x2588) sprite_fill(m, w, h, 0, h - (int)lroundf(h * (cp - 0x2580) / 8.0f), w, h, 255);
    else if (cp <= 0x258f) sprite_fill(m, w, h, 0, 0, (int)lroundf(w * (0x2590 - cp) / 8.0f), h, 255);
    else if (cp == 0x2590) sprite_fill(m, w, h, hx, 0, w, h, 255);
    else if (cp <= 0x2593) sprite_fill(m, w, h, 0, 0, w, h, (Uint8)(64 * (cp - 0x2590)));
    else if (cp == 0x2594) sprite_fill(m, w, h, 0, 0, w, (int)lroundf(h / 8.0f), 255);
    else if (cp == 0x2595) sprite_fill(m, w, h, w - (int)lroundf(w / 8.0f), 0, w, h, 255);
    else {
      // Quadrants, as bits for upper left, upper right, lower left, lower right.
      static const Uint8 quads[10] = {4, 8, 1, 13, 9, 7, 11, 2, 6, 14};
      Uint8 q = quads[cp - 0x2596];
      if (q & 1) sprite_fill(m, w, h, 0, 0, hx, hy, 255);
      if (q & 2) sprite_fill(m, w, h, hx, 0, w, hy, 255);
      if (q & 4) sprite_fill(m, w, h, 0, hy, hx, h, 255);
      if (q & 8) sprite_fill(m, w, h, hx, hy, w, h, 255);
    }
    return;
  }
  if (cp >= 0x2800 && cp <= 0x28ff) {
    // Dots 1-3 and 7 down the left column, 4-6 and 8 down the right.
    static const int dot_x[8] = {0, 0, 0, 1, 1, 1, 0, 1}, dot_y[8] = {0, 1, 2, 0, 1, 2, 3, 3};
    float r = fminf(w / 4.0f, h / 8.0f) * 0.8f;
    for (int i = 0; i < 8; ++i)
      if ((cp - 0x2800) >> i & 1) {
        float k[3] = {w * (1 + 2 * dot_x[i]) / 4.0f, h * (1 + 2 * dot_y[i]) / 8.0f, r};
        sprite_shape(m, w, h, sprite_in_circle, k);
      }
    return;
  }
  // Powerline: solid triangles, then outlined chevrons.
  float k[4] = {(float)w, (float)h, cp == 0xe0b0 || cp == 0xe0b1 ? 1.0f : -1.0f,
                cp == 0xe0b1 || cp == 0xe0b3 ? light / 2.0f : 0};
  sprite_shape(m, w, h, sprite_in_triangle, k);
}
