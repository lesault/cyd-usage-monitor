#include "ui_common.h"

#include "model.h"

uint16_t levelColor(float p) { return p < 60 ? ACCENT : (p < 85 ? AMBER : RED); }

// Claude look: an irregular burst of tapered rays.
#ifdef BRAND_CLAUDE
static void drawSpark(Canvas& c, int cx, int cy, float r, uint16_t col, uint16_t bg) {
  static const float len[12] = {1.00f, 0.72f, 0.92f, 0.66f, 1.00f, 0.78f,
                                0.94f, 0.68f, 0.98f, 0.74f, 0.90f, 0.70f};
  for (int i = 0; i < 12; i++) {
    float a = i * (TWO_PI / 12) + 0.12f;
    float l = r * len[i];
    c.wedge(cx + cosf(a) * r * 0.12f, cy + sinf(a) * r * 0.12f, cx + cosf(a) * l,
            cy + sinf(a) * l, r * 0.06f, r * 0.17f, col, bg);
  }
}

#endif

void drawMark(Canvas& c, int cx, int cy, float r, uint16_t col, uint16_t bg) {
#ifdef BRAND_CLAUDE
  drawSpark(c, cx, cy, r, col, bg);
#else
  // Neutral look: an open gauge ring (gap at the bottom) with a needle.
  c.arc(cx, cy, (int)r, (int)(r * 0.72f), 40, 320, col, bg);
  c.wedge(cx, cy, cx + r * 0.46f, cy - r * 0.46f, r * 0.07f, r * 0.14f, col, bg);
  c.circle(cx, cy, max(2, (int)(r * 0.16f)), col);
#endif
}

void drawBar(Canvas& c, int x, int y, int w, int h, float pct, uint16_t col) {
  int r = h / 2;
  c.rrect(x, y, w, h, r, TRACK);
  int fw = (int)(w * constrain(pct, 0, 100) / 100.0f);
  if (fw > 0) c.rrect(x, y, max(fw, h), h, r, col);
}
