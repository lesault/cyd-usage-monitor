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

void fmtDur(int64_t sec, char* b, size_t n) {
  if (sec < 0) sec = 0;
  int d = sec / 86400, h = (sec % 86400) / 3600, m = (sec % 3600) / 60, s = sec % 60;
  if (d > 0) snprintf(b, n, "%dd %dh", d, h);
  else if (h > 0) snprintf(b, n, "%dh %02dm", h, m);
  else if (m >= 10) snprintf(b, n, "%dm", m);
  else if (m > 0) snprintf(b, n, "%dm %02ds", m, s);
  else snprintf(b, n, "%ds", s);
}

void fmtAgo(uint32_t sec, char* b, size_t n) {
  if (sec < 60) strlcpy(b, "just now", n);
  else if (sec < 3600) snprintf(b, n, "%dm ago", (int)(sec / 60));
  else if (sec < 86400) snprintf(b, n, "%dh ago", (int)(sec / 3600));
  else snprintf(b, n, "%dd ago", (int)(sec / 86400));
}

void fmtClock(int64_t epoch, bool withDay, char* b, size_t n) {
  time_t lt = (time_t)(epoch + g_data.tz);
  struct tm t;
  gmtime_r(&lt, &t);
  static const char* D[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  char tb[12];
  if (g_h24) snprintf(tb, sizeof tb, "%02d:%02d", t.tm_hour, t.tm_min);
  else snprintf(tb, sizeof tb, "%d:%02d%s", t.tm_hour % 12 == 0 ? 12 : t.tm_hour % 12, t.tm_min,
                t.tm_hour < 12 ? "am" : "pm");
  if (withDay) snprintf(b, n, "%s %s", D[t.tm_wday], tb);
  else strlcpy(b, tb, n);
}

void fmtReset(int64_t epoch, int64_t now, char* b, size_t n) {
  bool same = (epoch + g_data.tz) / 86400 == (now + g_data.tz) / 86400;
  fmtClock(epoch, !same, b, n);
}
