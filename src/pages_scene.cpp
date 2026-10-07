// Secret scene: a polar bear on an iceberg. The sun's position along its arc is the
// 5-hour session usage; the iceberg melts as the weekly usage nears 100%. The only text
// is a faint weekly percentage in the bottom right. Full-screen, no header or nav.
#include <math.h>

#include "model.h"
#include "pages.h"

static const int WL0 = 150;       // waterline with no melt
static const int RISE_PX = 6;     // sea level rise at 100% weekly
static const float MIN_ICE = 0.2f;  // iceberg never melts below this fraction

static uint16_t mix(int r0, int g0, int b0, int r1, int g1, int b1, float t) {
  t = constrain(t, 0.0f, 1.0f);
  return C565((int)(r0 + (r1 - r0) * t), (int)(g0 + (g1 - g0) * t), (int)(b0 + (b1 - b0) * t));
}

// Sky colour at screen row y: pale blue day easing to a dusky orange as the session fills.
static uint16_t skyAt(int y, int wl, float dusk) {
  float t = constrain(y / (float)wl, 0.0f, 1.0f);
  int tr = 40 + (int)(30 * dusk), tg = 90 - (int)(45 * dusk), tb = 150 - (int)(50 * dusk);
  int hr = 150 + (int)(95 * dusk), hg = 205 - (int)(55 * dusk), hb = 230 - (int)(130 * dusk);
  return mix(tr, tg, tb, hr, hg, hb, t);
}

static void drawBear(Canvas& c, int x, int y, float s) {
  const uint16_t FUR = C565(245, 242, 232), SHADE = C565(205, 208, 212), DARK = C565(30, 30, 34);
  auto n = [&](float v) { return max(1, (int)lroundf(v * s)); };
  c.rect(x - n(9), y - n(6), n(4), n(6), SHADE);  // legs
  c.rect(x + n(6), y - n(6), n(4), n(6), SHADE);
  c.ellipse(x, y - n(9), n(13), n(7), FUR);       // body
  c.circle(x - n(14), y - n(14), n(5), FUR);       // head
  c.ellipse(x - n(19), y - n(12), n(3), n(2), FUR);  // snout
  c.circle(x - n(12), y - n(19), n(2), FUR);  // ear
  c.circle(x - n(21), y - n(13), 1, DARK);            // nose
  c.circle(x - n(16), y - n(15), 1, DARK);            // eye
}

void drawScene(Canvas& c, int64_t now) {
  const Window& sw = g_data.s;
  const Window& ww = g_data.w;
  float sp = (sw.valid && sw.resetsAt > now) ? constrain(sw.pct, 0, 100) / 100.0f : 0.0f;
  float wp = (ww.valid && ww.resetsAt > now) ? constrain(ww.pct, 0, 100) / 100.0f : 0.0f;
  float k = 1.0f - (1.0f - MIN_ICE) * wp;  // iceberg scale
  int wl = WL0 - (int)lroundf(RISE_PX * wp);
  uint32_t step = c.ms / 250;

  // Sky.
  float dusk = constrain((sp - 0.4f) / 0.6f, 0.0f, 1.0f);
  for (int y = 0; y < wl; y += 6) c.rect(0, y, 320, min(6, wl - y), skyAt(y, wl, dusk));

  // Sun: rises from the left horizon, peaks at 50%, sets into the sea at the limit.
  int sx = 36 + (int)lroundf(248 * sp);
  int sy = wl - 6 - (int)lroundf(sinf(M_PI * sp) * 96);
  uint16_t sun = mix(255, 222, 96, 255, 90, 50, dusk);
  c.circle(sx, sy, 22, mix(200, 225, 235, 235, 150, 105, dusk));  // soft halo
  c.circle(sx, sy, 19, mix(250, 235, 170, 255, 125, 75, dusk));
  c.circle(sx, sy, 15, sun);

  // Sea (covers the lower part of the sun at the ends of its arc).
  for (int y = wl; y < 240; y += 6)
    c.rect(0, y, 320, min(6, 240 - y), mix(30, 90, 118, 12, 36, 56, (y - wl) / (240.0f - wl)));
  for (int i = 0; i < 6; i++) {
    int x = (i * 53 + (int)(step % 8) * 3) % 300;
    c.rect(x, wl + 10 + i * 14, 14 + (i % 3) * 6, 1, mix(30, 90, 118, 150, 205, 230, 0.35f));
  }

  const int cx = 205;
  // Submerged ice.
  uint16_t deep = mix(30, 90, 118, 200, 235, 245, 0.45f);
  c.tri(cx - (int)(60 * k), wl, cx + (int)(60 * k), wl, cx - (int)(10 * k), wl + (int)(46 * k), deep);

  // Ice above water: a fan from the base centre over the silhouette.
  static const int8_t P[8][2] = {{-70, 0}, {-52, 22}, {-40, 30}, {0, 30},
                                 {14, 56}, {34, 44}, {52, 18}, {70, 0}};
  const uint16_t LIT = C565(236, 244, 250), SHD = C565(168, 196, 222);
  for (int i = 0; i < 7; i++) {
    c.tri(cx, wl, cx + (int)lroundf(P[i][0] * k), wl - (int)lroundf(P[i][1] * k),
          cx + (int)lroundf(P[i + 1][0] * k), wl - (int)lroundf(P[i + 1][1] * k),
          i <= 3 ? LIT : SHD);
  }
  c.rect(cx - (int)(72 * k), wl, (int)(144 * k), 1, C565(220, 238, 246));  // waterline foam

  // Bear on the plateau; it shrinks a little as the ice does so it still fits.
  drawBear(c, cx - (int)lroundf(20 * k), wl - (int)lroundf(30 * k), 0.6f + 0.4f * k);

  // Meltwater dripping off the edge once the week is well used.
  if (wp >= 0.6f && step % 6 < 4) {
    int dy = wl - (int)lroundf(16 * k * (4 - step % 6) / 4.0f);
    c.circle(cx + (int)lroundf(50 * k), dy, 2, LIT);
  }

  if (ww.valid) {
    char b[8];
    snprintf(b, sizeof b, "%d%%", (int)lroundf(wp * 100));
    c.text(F_SANS9, MUTED, b, 312, 236, BR_DATUM);
  }
}
