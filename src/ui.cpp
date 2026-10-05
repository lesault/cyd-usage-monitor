#include "ui.h"

#include <Preferences.h>
#include <TFT_eSPI.h>

#include "model.h"

// ---- Claude palette (warm dark) ---------------------------------------------
#define C565(r, g, b) ((uint16_t)((((r)&0xF8) << 8) | (((g)&0xFC) << 3) | ((b) >> 3)))
static const uint16_t BG = C565(20, 19, 17);      // warm near-black
static const uint16_t CARD = C565(38, 36, 31);
static const uint16_t TRACK = C565(62, 59, 51);
static const uint16_t IVORY = C565(240, 238, 230);
static const uint16_t DIM = C565(163, 159, 148);
static const uint16_t CLAY = C565(217, 119, 87);  // Claude orange
static const uint16_t AMBER = C565(224, 168, 84);
static const uint16_t RED = C565(229, 96, 77);
static const uint16_t OLIVE = C565(138, 166, 106);

static uint16_t levelColor(float p) { return p < 60 ? CLAY : (p < 85 ? AMBER : RED); }

#ifndef GFXFF
#define GFXFF 1  // font id meaning "use the current free font"
#endif

// ---- fonts (GFX free fonts: serif for headings like Claude's branding) ------
#define F_SANS9 (&FreeSans9pt7b)
#define F_SANS12 (&FreeSans12pt7b)
#define F_SERIF12 (&FreeSerifBold12pt7b)
#define F_SANSB12 (&FreeSansBold12pt7b)
#define F_SANSB18 (&FreeSansBold18pt7b)
#define F_SANSB24 (&FreeSansBold24pt7b)

// ---- layout ------------------------------------------------------------------
static const int HEADER_H = 30;
static const int NAV_Y = 222;
static const int CARD_H = 92;
static const int CARD1_Y = 30, CARD2_Y = 128;

// ---- state -----------------------------------------------------------------
enum Page { P_HOME, P_FORECAST, P_INFO, P_SETTINGS, PAGE_COUNT };
static const int NAV_PAGES = 3;  // pages reachable by tapping; settings is a long-press

static TFT_eSPI tft;
static TFT_eSprite spr(&tft);
static bool spriteOk = false;

static Page page = P_HOME;
static bool clockMode = false;  // show reset clock time instead of countdown
static bool dirty = true;
static bool dimmed = false;
static uint32_t lastTouch = 0, lastRender = 0;

static uint8_t brightness = 80;  // percent, 10..100 in steps of 10
static uint8_t dimIdx = 2;
static bool h24 = true;
static const uint8_t DIM_MIN[] = {0, 1, 5, 15};
static const uint8_t DIM_LEVEL = 10;

static void setBacklight(uint8_t v) { ledcWrite(0, v); }
static uint8_t brightnessDuty() { return brightness * 255 / 100; }

static void loadPrefs() {
  Preferences p;
  p.begin("cyd", false);
  brightness = constrain(p.getUChar("brp", brightness) / 10 * 10, 10, 100);
  dimIdx = p.getUChar("dim", dimIdx) % 4;
  h24 = p.getBool("h24", h24);
  p.end();
}

static void savePrefs() {
  Preferences p;
  p.begin("cyd", false);
  p.putUChar("brp", brightness);
  p.putUChar("dim", dimIdx);
  p.putBool("h24", h24);
  p.end();
}

void uiMarkDirty() { dirty = true; }

// ---- drawing wrapper: draws into a horizontal band of the screen -----------
struct Canvas {
  TFT_eSprite& s;
  int dy;  // screen y of the band's first row
  void rect(int x, int y, int w, int h, uint16_t c) { s.fillRect(x, y - dy, w, h, c); }
  void rrect(int x, int y, int w, int h, int r, uint16_t c) {
    s.fillRoundRect(x, y - dy, w, h, r, c);
  }
  void outline(int x, int y, int w, int h, int r, uint16_t c) {
    s.drawRoundRect(x, y - dy, w, h, r, c);
  }
  void circle(int x, int y, int r, uint16_t c) { s.fillCircle(x, y - dy, r, c); }
  // Text in a free font. Use baseline datums (BL/BR/BC) for predictable placement.
  int text(const GFXfont* f, uint16_t col, const char* t, int x, int y,
           uint8_t datum = BL_DATUM) {
    s.setFreeFont(f);
    s.setTextColor(col);
    s.setTextDatum(datum);
    return s.drawString(t, x, y - dy, GFXFF);
  }
  int width(const GFXfont* f, const char* t) {
    s.setFreeFont(f);
    return s.textWidth(t, GFXFF);
  }
  void wedge(float x0, float y0, float x1, float y1, float r0, float r1, uint16_t col,
             uint16_t bg) {
    s.drawWedgeLine(x0, y0 - dy, x1, y1 - dy, r0, r1, col, bg);
  }
};

// Claude-style spark: an irregular burst of tapered rays.
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

// ---- formatting ------------------------------------------------------------
static void fmtDur(int64_t sec, char* b, size_t n) {
  if (sec < 0) sec = 0;
  int d = sec / 86400, h = (sec % 86400) / 3600, m = (sec % 3600) / 60, s = sec % 60;
  if (d > 0) snprintf(b, n, "%dd %dh", d, h);
  else if (h > 0) snprintf(b, n, "%dh %02dm", h, m);
  else if (m >= 10) snprintf(b, n, "%dm", m);
  else if (m > 0) snprintf(b, n, "%dm %02ds", m, s);
  else snprintf(b, n, "%ds", s);
}

static void fmtAgo(uint32_t sec, char* b, size_t n) {
  if (sec < 60) strlcpy(b, "just now", n);
  else if (sec < 3600) snprintf(b, n, "%dm ago", (int)(sec / 60));
  else if (sec < 86400) snprintf(b, n, "%dh ago", (int)(sec / 3600));
  else snprintf(b, n, "%dd ago", (int)(sec / 86400));
}

static void fmtClock(int64_t epoch, bool withDay, char* b, size_t n) {
  time_t lt = (time_t)(epoch + g_data.tz);
  struct tm t;
  gmtime_r(&lt, &t);
  static const char* D[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  char tb[12];
  if (h24) snprintf(tb, sizeof tb, "%02d:%02d", t.tm_hour, t.tm_min);
  else snprintf(tb, sizeof tb, "%d:%02d%s", t.tm_hour % 12 == 0 ? 12 : t.tm_hour % 12, t.tm_min,
                t.tm_hour < 12 ? "am" : "pm");
  if (withDay) snprintf(b, n, "%s %s", D[t.tm_wday], tb);
  else strlcpy(b, tb, n);
}

// Clock string for a reset time; the weekday is only shown when it isn't today.
static void fmtReset(int64_t epoch, int64_t now, char* b, size_t n) {
  bool same = (epoch + g_data.tz) / 86400 == (now + g_data.tz) / 86400;
  fmtClock(epoch, !same, b, n);
}

// ---- shared pieces ---------------------------------------------------------
static void drawBar(Canvas& c, int x, int y, int w, int h, float pct, uint16_t col) {
  int r = h / 2;
  c.rrect(x, y, w, h, r, TRACK);
  int fw = (int)(w * constrain(pct, 0, 100) / 100.0f);
  if (fw > 0) c.rrect(x, y, max(fw, h), h, r, col);
}

static void drawHeader(Canvas& c) {
  drawSpark(c, 20, 15, 11, CLAY, BG);
  c.text(F_SERIF12, IVORY, "Claude", 38, 22);

  const char* label;
  uint16_t dot;
  char tmp[24];
  if (!linkAlive()) {
    label = "No link";
    dot = RED;
  } else if (!g_data.haveState) {
    label = "Waiting for Claude";
    dot = AMBER;
  } else if (dataAge() <= 90) {
    label = "Live";
    dot = OLIVE;
  } else {
    char a[16];
    fmtAgo(dataAge(), a, sizeof a);
    snprintf(tmp, sizeof tmp, "Updated %s", a);
    label = tmp;
    dot = dataAge() > 3600 ? AMBER : DIM;
  }
  int w = c.width(F_SANS9, label);
  c.text(F_SANS9, DIM, label, 312, 20, BR_DATUM);
  c.circle(312 - w - 10, 11, 4, dot);
}

static void drawNav(Canvas& c) {
  char b[24];
  if (g_data.haveTime) fmtClock(nowEpoch(), false, b, sizeof b);
  else strcpy(b, "--:--");
  c.text(F_SANS9, DIM, b, 12, 237);

  for (int i = 0; i < NAV_PAGES; i++) {
    int x = 160 + (i - 1) * 14;
    c.circle(x, 231, i == (int)page ? 4 : 3, i == (int)page ? CLAY : TRACK);
  }
  if (g_data.model[0]) c.text(F_SANS9, DIM, g_data.model, 310, 237, BR_DATUM);
}

static void drawNoLink(Canvas& c) {
  drawSpark(c, 160, 84, 30, CLAY, BG);
  c.text(F_SANSB18, IVORY, "Waiting for bridge", 160, 150, BC_DATUM);
  c.text(F_SANS9, DIM, "run host/install.sh on the Mac", 160, 176, BC_DATUM);
}

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
    if (clockMode) {
      strlcpy(a, clk, sizeof a);
      snprintf(b, sizeof b, "in %s", dur);
    } else {
      strlcpy(a, dur, sizeof a);
      snprintf(b, sizeof b, "at %s", clk);
    }
  }
  c.text(F_SANS9, DIM, clockMode ? "resets at" : "resets in", 306, y + 25, BR_DATUM);
  c.text(F_SANSB12, IVORY, a, 306, y + 52, BR_DATUM);
  c.text(F_SANS9, DIM, b, 306, y + 70, BR_DATUM);

  const int bx = 14, bw = 292, by = y + 76, bh = 10;
  drawBar(c, bx, by, bw, bh, pct, col);
  if (!expired) {  // pace marker: how much of the window has elapsed
    float el = constrain((now - (w.resetsAt - span)) / (float)span, 0.0f, 1.0f);
    c.rrect(bx + (int)(bw * el) - 1, by - 3, 3, bh + 6, 1, IVORY);
  }
}

static void drawHome(Canvas& c, int64_t now) {
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

static void drawForecast(Canvas& c, int64_t now) {
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
static void drawInfo(Canvas& c, int64_t) {
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

// ---- Settings --------------------------------------------------------------
struct Rect { int x, y, w, h; };
static const Rect R_BR_MINUS = {176, 44, 40, 34}, R_BR_PLUS = {268, 44, 40, 34};
static const Rect R_DIM_PREV = {176, 88, 40, 34}, R_DIM_NEXT = {268, 88, 40, 34};
static const Rect R_CLOCK = {176, 132, 132, 34};
static const Rect R_RECAL = {12, 186, 148, 44}, R_BACK = {172, 186, 136, 44};

static bool inRect(const Rect& r, int x, int y) {
  return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void drawBtn(Canvas& c, const Rect& r, const char* t, uint16_t fill = CARD,
                    uint16_t ink = IVORY) {
  c.rrect(r.x, r.y, r.w, r.h, 8, fill);
  c.text(F_SANS12, ink, t, r.x + r.w / 2, r.y + r.h / 2 + 1, MC_DATUM);
}

static void drawSettings(Canvas& c) {
  drawSpark(c, 24, 25, 12, CLAY, BG);
  c.text(F_SANSB18, IVORY, "Settings", 44, 37);
  c.text(F_SANS12, IVORY, "Brightness", 12, 67);
  c.text(F_SANS12, IVORY, "Auto-dim", 12, 111);
  c.text(F_SANS12, IVORY, "Clock", 12, 155);

  char b[16];
  drawBtn(c, R_BR_MINUS, "-");
  drawBtn(c, R_BR_PLUS, "+");
  snprintf(b, sizeof b, "%d%%", brightness);
  c.text(F_SANS12, IVORY, b, 242, 62, MC_DATUM);

  drawBtn(c, R_DIM_PREV, "<");
  drawBtn(c, R_DIM_NEXT, ">");
  if (DIM_MIN[dimIdx]) snprintf(b, sizeof b, "%dm", DIM_MIN[dimIdx]);
  else strcpy(b, "Off");
  c.text(F_SANS12, IVORY, b, 242, 106, MC_DATUM);

  drawBtn(c, R_CLOCK, h24 ? "24-hour" : "12-hour");
  drawBtn(c, R_RECAL, "Recalibrate");
  drawBtn(c, R_BACK, "Back", CLAY, BG);
}

// ---- render ----------------------------------------------------------------
static void drawPage(Canvas& c) {
  if (page == P_SETTINGS) {
    drawSettings(c);
    return;
  }
  drawHeader(c);
  if (!g_data.haveTime) {
    drawNoLink(c);
  } else {
    int64_t now = nowEpoch();
    if (page == P_HOME) drawHome(c, now);
    else if (page == P_FORECAST) drawForecast(c, now);
    else drawInfo(c, now);
  }
  drawNav(c);
}

static void render() {
  if (!spriteOk) return;
  for (int band = 0; band < 2; band++) {
    Canvas c{spr, band * 120};
    spr.fillSprite(BG);
    drawPage(c);
    spr.pushSprite(0, band * 120);
  }
}

// ---- calibration -----------------------------------------------------------
static void calibrate() {
  const int scr[3][2] = {{20, 20}, {300, 40}, {160, 220}};
  int raw[3][2];
  for (;;) {
    for (int i = 0; i < 3; i++) {
      tft.fillScreen(BG);
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(IVORY);
      tft.drawString("Touch the crosshair", 160, 110, 2);
      int x = scr[i][0], y = scr[i][1];
      tft.drawFastHLine(x - 12, y, 25, CLAY);
      tft.drawFastVLine(x, y - 12, 25, CLAY);
      tft.drawCircle(x, y, 6, CLAY);
      touchRawWait(raw[i][0], raw[i][1]);
    }
    if (touchSetCal(scr, raw)) break;
  }
  touchSaveCal();
  lastTouch = millis();
  dirty = true;
}

// ---- touch -----------------------------------------------------------------
static void wake() {
  dimmed = false;
  setBacklight(brightnessDuty());
}

void uiHandleTouch(const TouchResult& t) {
  lastTouch = millis();
  dirty = true;
  if (dimmed) {  // first touch only wakes the screen
    wake();
    return;
  }

  if (page == P_SETTINGS) {
    if (t.ev != TE_TAP) return;
    if (inRect(R_BR_MINUS, t.x, t.y)) brightness = max(10, brightness - 10);
    else if (inRect(R_BR_PLUS, t.x, t.y)) brightness = min(100, brightness + 10);
    else if (inRect(R_DIM_PREV, t.x, t.y)) dimIdx = (dimIdx + 3) % 4;
    else if (inRect(R_DIM_NEXT, t.x, t.y)) dimIdx = (dimIdx + 1) % 4;
    else if (inRect(R_CLOCK, t.x, t.y)) h24 = !h24;
    else if (inRect(R_RECAL, t.x, t.y)) calibrate();
    else if (inRect(R_BACK, t.x, t.y)) page = P_HOME;
    else return;
    setBacklight(brightnessDuty());
    savePrefs();
    return;
  }

  if (t.ev == TE_LONG) {
    page = P_SETTINGS;
  } else if (t.y >= NAV_Y && t.x < 90) {
    clockMode = !clockMode;
  } else if (t.x < 160) {
    page = (Page)((page + NAV_PAGES - 1) % NAV_PAGES);
  } else {
    page = (Page)((page + 1) % NAV_PAGES);
  }
}

// ---- lifecycle -------------------------------------------------------------
void uiBegin() {
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(BG);
  ledcSetup(0, 5000, 8);
  ledcAttachPin(TFT_BL, 0);
  loadPrefs();
  setBacklight(brightnessDuty());

  spr.setColorDepth(16);
  spriteOk = spr.createSprite(320, 120) != nullptr;
  if (!spriteOk) Serial.println("sprite alloc failed");

  touchBegin();
  if (!touchLoadCal()) calibrate();
  lastTouch = millis();
}

void uiTick() {
  uint32_t now = millis();
  if (!dimmed && page != P_SETTINGS && DIM_MIN[dimIdx] &&
      now - lastTouch > DIM_MIN[dimIdx] * 60000UL) {
    dimmed = true;
    setBacklight(DIM_LEVEL);
  }
  if (page != P_HOME && now - lastTouch > (page == P_SETTINGS ? 60000UL : 30000UL)) {
    page = P_HOME;
    dirty = true;
  }
  if (dirty || now - lastRender >= 500) {
    render();
    lastRender = now;
    dirty = false;
  }
}
