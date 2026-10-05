// Home, Forecast and Info: the Claude usage pages.
#include "model.h"
#include "pages.h"

static const int CARD_H = 92;
static const int CARD1_Y = 30, CARD2_Y = 128;

// ---- Home ------------------------------------------------------------------
static void drawCard(Canvas& c, int y, const char* label, const char* sub, const Window& w,
                     int64_t span, int64_t now) {
  c.rrect(4, y, 312, CARD_H, 10, CARD);
  int lw = c.width(F_SANSB12, label);
  c.text(F_SANSB12, IVORY, label, 14, y + 25);
  c.text(F_SANS9, DIM, sub, 14 + lw + 8, y + 25);

  if (!w.valid) {
    c.text(F_SANS12, DIM, g_data.haveState ? "No rate-limit data" : "Waiting for Claude Code",
           14, y + 62);
    return;
  }
  bool expired = w.resetsAt <= now;
  float pct = expired ? 0 : w.pct;
  uint16_t col = levelColor(pct);

  if (pct >= 95 && (millis() / 500) % 2) c.outline(4, y, 312, CARD_H, 10, RED);

  char num[8];
  snprintf(num, sizeof num, "%d", (int)lroundf(pct));
  int nw = c.text(F_SANSB24, pct < 60 ? IVORY : col, num, 14, y + 70);
  c.text(F_SANSB18, DIM, "%", 14 + nw + 3, y + 70);

  char a[24], b[28], dur[24], clk[24];
  if (expired) {
    strcpy(a, "Reset");
    strcpy(b, "awaiting new data");
  } else {
    fmtDur(w.resetsAt - now, dur, sizeof dur);
    fmtReset(w.resetsAt, now, clk, sizeof clk);
    if (g_clockMode) {
      strlcpy(a, clk, sizeof a);
      snprintf(b, sizeof b, "in %s", dur);
    } else {
      strlcpy(a, dur, sizeof a);
      snprintf(b, sizeof b, "at %s", clk);
    }
  }
  c.text(F_SANS9, DIM, g_clockMode ? "resets at" : "resets in", 306, y + 25, BR_DATUM);
  c.text(F_SANSB12, IVORY, a, 306, y + 52, BR_DATUM);
  c.text(F_SANS9, DIM, b, 306, y + 70, BR_DATUM);

  const int bx = 14, bw = 292, by = y + 76, bh = 10;
  drawBar(c, bx, by, bw, bh, pct, col);
  if (!expired) {  // pace marker: how much of the window has elapsed
    float el = constrain((now - (w.resetsAt - span)) / (float)span, 0.0f, 1.0f);
    c.rrect(bx + (int)(bw * el) - 1, by - 3, 3, bh + 6, 1, IVORY);
  }
}

void drawHome(Canvas& c, int64_t now) {
  drawCard(c, CARD1_Y, "Session", "5 hours", g_data.s, 5 * 3600, now);
  drawCard(c, CARD2_Y, "Weekly", "7 days", g_data.w, 7 * 86400, now);
}

// ---- Forecast --------------------------------------------------------------
// Two thin bars: time elapsed in the window (grey) over usage (coloured).
static void drawPaceBars(Canvas& c, int y, float elapsedFrac, float pct) {
  c.rrect(14, y, 292, 4, 2, TRACK);
  c.rrect(14, y, max(4, (int)(292 * elapsedFrac)), 4, 2, DIM);
  drawBar(c, 14, y + 7, 292, 6, pct, levelColor(pct));
}

void drawForecast(Canvas& c, int64_t now) {
  struct Spec { const Window* w; const char* name; int64_t span; int y; };
  const Spec specs[2] = {{&g_data.s, "Session", 5 * 3600, CARD1_Y},
                         {&g_data.w, "Weekly", 7 * 86400, CARD2_Y}};
  for (const Spec& sp : specs) {
    const Window& w = *sp.w;
    int y = sp.y;
    c.rrect(4, y, 312, CARD_H, 10, CARD);
    c.text(F_SANSB12, IVORY, sp.name, 14, y + 25);
    if (!w.valid || w.resetsAt <= now) {
      c.text(F_SANS12, DIM, w.valid ? "Window has reset" : "No rate-limit data", 14, y + 62);
      continue;
    }
    float el = constrain((now - (w.resetsAt - sp.span)) / (float)sp.span, 0.0f, 1.0f);
    float hrs = el * sp.span / 3600.0f;
    char m[32] = "", sub[48] = "", side[20] = "", d[24], clk[24];
    uint16_t col = IVORY;

    if (sp.span == 5 * 3600) {
      if (w.pct >= 100) {
        strcpy(m, "Limit reached");
        col = RED;
        fmtDur(w.resetsAt - now, d, sizeof d);
        snprintf(sub, sizeof sub, "resets in %s", d);
      } else if (hrs < 0.1f || w.pct < 1) {
        strcpy(m, "Too early to tell");
        col = DIM;
        strcpy(sub, "needs a little more usage");
      } else {
        float rate = w.pct / hrs;  // % per hour
        snprintf(side, sizeof side, "%.0f%% per hour", rate);
        int64_t limitAt = now + (int64_t)((100 - w.pct) / rate * 3600);
        if (limitAt < w.resetsAt) {
          fmtReset(limitAt, now, clk, sizeof clk);
          fmtDur(limitAt - now, d, sizeof d);
          snprintf(m, sizeof m, "Limit ~ %s", clk);
          snprintf(sub, sizeof sub, "in %s at this pace", d);
          col = limitAt - now < 3600 ? RED : AMBER;
        } else {
          strcpy(m, "Safe until reset");
          col = OLIVE;
          snprintf(sub, sizeof sub, "projected %d%% by reset", (int)lroundf(w.pct / el));
        }
      }
    } else {
      if (el < 0.03f || w.pct < 1) {
        strcpy(m, "Too early to tell");
        col = DIM;
        strcpy(sub, "needs more of the week");
      } else {
        float proj = w.pct / el;
        col = proj >= 100 ? RED : (proj >= 85 ? AMBER : OLIVE);
        snprintf(m, sizeof m, "Projected %d%%", (int)lroundf(proj));
        float diff = w.pct - el * 100;
        snprintf(sub, sizeof sub, "%d%% %s pace, %d%% of week gone", (int)lroundf(fabsf(diff)),
                 diff > 0 ? "over" : "under", (int)lroundf(el * 100));
        snprintf(side, sizeof side, "%.0f%% per day", w.pct / (el * 7));
      }
    }
    if (side[0]) c.text(F_SANS9, DIM, side, 306, y + 25, BR_DATUM);
    c.text(F_SANSB12, col, m, 14, y + 53);
    c.text(F_SANS9, DIM, sub, 14, y + 70);
    drawPaceBars(c, y + 76, el, w.pct);
  }
}

// ---- Session info ----------------------------------------------------------
void drawInfo(Canvas& c, int64_t) {
  c.rrect(4, 30, 312, 190, 10, CARD);
  const char* labels[] = {"Model", "Context", "Cost", "Duration", "Lines"};
  char v[5][28];
  strlcpy(v[0], g_data.model[0] ? g_data.model : "--", sizeof v[0]);
  if (g_data.ctx >= 0) snprintf(v[1], sizeof v[1], "%d%%", g_data.ctx);
  else strcpy(v[1], "--");
  if (g_data.cost >= 0) snprintf(v[2], sizeof v[2], "$%.2f", g_data.cost);
  else strcpy(v[2], "--");
  if (g_data.dur >= 0) fmtDur(g_data.dur, v[3], sizeof v[3]);
  else strcpy(v[3], "--");
  if (g_data.la >= 0 && g_data.lr >= 0)
    snprintf(v[4], sizeof v[4], "+%d / -%d", g_data.la, g_data.lr);
  else strcpy(v[4], "--");

  for (int i = 0; i < 5; i++) {
    int y = 36 + i * 36;
    c.text(F_SANS9, DIM, labels[i], 18, y + 28);
    c.text(F_SANSB12, IVORY, v[i], 118, y + 28);
    if (i == 1 && g_data.ctx >= 0)
      drawBar(c, 214, y + 12, 92, 8, g_data.ctx, levelColor(g_data.ctx));
    if (i < 4) c.rect(16, y + 34, 288, 1, TRACK);
  }
}
