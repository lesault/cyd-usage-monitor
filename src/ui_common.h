// Shared look-and-feel for all pages: palette, fonts, Canvas, small helpers.
#pragma once
#include <TFT_eSPI.h>

// ---- branding ---------------------------------------------------------------
// Default build is neutral (cool teal on dark grey, gauge mark). Build the
// `cyd-claude` environment (-DBRAND_CLAUDE=1) for the Claude-style look: warm
// palette, orange spark mark and "Claude" wordmark. -DBRAND_NAME='"Text"' overrides
// the header text of either look.
#define C565(r, g, b) ((uint16_t)((((r)&0xF8) << 8) | (((g)&0xFC) << 3) | ((b) >> 3)))
#ifdef BRAND_CLAUDE
static const uint16_t BG = C565(20, 19, 17);      // warm near-black
static const uint16_t CARD = C565(38, 36, 31);
static const uint16_t TRACK = C565(62, 59, 51);
static const uint16_t MUTED = C565(110, 106, 96);
static const uint16_t IVORY = C565(240, 238, 230);  // primary text
static const uint16_t DIM = C565(163, 159, 148);    // secondary text
static const uint16_t ACCENT = C565(217, 119, 87);  // orange
#define F_BRAND (&FreeSerifBold12pt7b)
#ifndef BRAND_NAME
#define BRAND_NAME "Claude"
#endif
#else
static const uint16_t BG = C565(16, 18, 22);      // cool near-black
static const uint16_t CARD = C565(30, 34, 41);
static const uint16_t TRACK = C565(52, 58, 68);
static const uint16_t MUTED = C565(100, 108, 120);
static const uint16_t IVORY = C565(236, 240, 245);  // primary text
static const uint16_t DIM = C565(150, 158, 170);    // secondary text
static const uint16_t ACCENT = C565(56, 189, 205);  // teal
#define F_BRAND (&FreeSansBold12pt7b)
#ifndef BRAND_NAME
#define BRAND_NAME "Usage"
#endif
#endif
static const uint16_t AMBER = C565(224, 168, 84);
static const uint16_t RED = C565(229, 96, 77);
static const uint16_t OLIVE = C565(138, 166, 106);

#ifndef GFXFF
#define GFXFF 1  // font id meaning "use the current free font"
#endif

// GFX free fonts: bold sans for data (the Claude look also uses a serif wordmark).
#define F_SANS9 (&FreeSans9pt7b)
#define F_SANS12 (&FreeSans12pt7b)
#define F_SANSB12 (&FreeSansBold12pt7b)
#define F_SANSB18 (&FreeSansBold18pt7b)
#define F_SANSB24 (&FreeSansBold24pt7b)

// Draws into a horizontal band of the screen (the sprite is half the screen high).
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
  // Free-font text. Use baseline datums (BL/BR/BC) for predictable placement.
  int text(const GFXfont* f, uint16_t col, const char* t, int x, int y, uint8_t datum = BL_DATUM,
           uint8_t size = 1) {
    s.setFreeFont(f);
    s.setTextSize(size);
    s.setTextColor(col);
    s.setTextDatum(datum);
    int w = s.drawString(t, x, y - dy, GFXFF);
    s.setTextSize(1);
    return w;
  }
  int width(const GFXfont* f, const char* t, uint8_t size = 1) {
    s.setFreeFont(f);
    s.setTextSize(size);
    int w = s.textWidth(t, GFXFF);
    s.setTextSize(1);
    return w;
  }
  void arc(int x, int y, int r, int ir, int start, int end, uint16_t fg, uint16_t bg) {
    s.drawSmoothArc(x, y - dy, r, ir, start, end, fg, bg, true);
  }
  void wedge(float x0, float y0, float x1, float y1, float r0, float r1, uint16_t col,
             uint16_t bg) {
    s.drawWedgeLine(x0, y0 - dy, x1, y1 - dy, r0, r1, col, bg);
  }
};

// UI settings/state shared with the pages (defined in ui.cpp).
extern bool g_h24;        // 24-hour clock
extern bool g_clockMode;  // show reset clock times instead of countdowns

uint16_t levelColor(float pct);  // clay < 60, amber < 85, red above
void drawMark(Canvas& c, int cx, int cy, float r, uint16_t col, uint16_t bg);  // brand mark
void drawBar(Canvas& c, int x, int y, int w, int h, float pct, uint16_t col);

void fmtDur(int64_t sec, char* b, size_t n);                    // 2h 14m
void fmtAgo(uint32_t sec, char* b, size_t n);                   // 3m ago
void fmtClock(int64_t epoch, bool withDay, char* b, size_t n);  // 14:07 / Fri 09:00
void fmtReset(int64_t epoch, int64_t now, char* b, size_t n);   // weekday only if not today
