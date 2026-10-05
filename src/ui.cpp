// UI shell: page list, header/nav, settings, dimming, auto-rotation, alerts, touch.
// Page content lives in pages_claude.cpp and pages_system.cpp.
#include "ui.h"

#include <Preferences.h>

#include "model.h"
#include "pages.h"

// ---- pages -----------------------------------------------------------------
// Order matters: tapping cycles through the first NAV_PAGES in this order.
enum Page { P_HOME, P_FORECAST, P_INFO, P_ACTIVITY, P_HEALTH, P_CLOCK, P_SETTINGS };
static const int NAV_PAGES = 6;  // settings is reached by long-press only
static const int NAV_Y = 222;

// Auto-rotation while Claude is idle: page and how long it stays up (ms).
static const Page AUTO_PAGE[] = {P_CLOCK, P_HEALTH, P_ACTIVITY};
static const uint32_t AUTO_MS[] = {15000, 15000, 10000};
static const int AUTO_N = 3;
static const uint32_t AUTO_AFTER_TOUCH_MS = 30000;

static const uint32_t WAKE_MS = 60000;         // screen stays lit after a touch at night
static const uint32_t SNOOZE_MS = 30UL * 60000;  // dismissed alerts stay quiet this long

// ---- state -----------------------------------------------------------------
static TFT_eSPI tft;
static TFT_eSprite spr(&tft);
static bool spriteOk = false;

static Page page = P_HOME;
bool g_clockMode = false;  // show reset clock time instead of countdown
bool g_h24 = true;
static bool dirty = true;
static bool dimmed = false;
static uint32_t lastTouch = 0, lastRender = 0, wakeUntil = 0;

static bool autoActive = false;
static int autoIdx = 0;
static uint32_t autoSince = 0;

static uint32_t snoozeUntil[4] = {0, 0, 0, 0};  // indexed by alert bit position
static uint8_t prevAlerts = 0;

static uint8_t brightness = 80;  // percent, 10..100 in steps of 10
static uint8_t dimIdx = 2;
static bool autoSwitch = true;
static const uint8_t DIM_MIN[] = {0, 1, 5, 15};
static const uint8_t DIM_LEVEL = 10;

static void setBacklight(uint8_t v) { ledcWrite(0, v); }
static uint8_t brightnessDuty() { return brightness * 255 / 100; }

static void loadPrefs() {
  Preferences p;
  p.begin("cyd", false);
  brightness = constrain(p.getUChar("brp", brightness) / 10 * 10, 10, 100);
  dimIdx = p.getUChar("dim", dimIdx) % 4;
  g_h24 = p.getBool("h24", g_h24);
  autoSwitch = p.getBool("auto", autoSwitch);
  p.end();
}

static void savePrefs() {
  Preferences p;
  p.begin("cyd", false);
  p.putUChar("brp", brightness);
  p.putUChar("dim", dimIdx);
  p.putBool("h24", g_h24);
  p.putBool("auto", autoSwitch);
  p.end();
}

void uiMarkDirty() { dirty = true; }

// ---- alerts ----------------------------------------------------------------
static int bitIndex(uint8_t bit) {
  int i = 0;
  while (bit > 1) { bit >>= 1; i++; }
  return i;
}

// Raised by the bridge, link is up, and not snoozed.
static uint8_t activeAlerts() {
  if (!linkAlive()) return 0;
  uint8_t m = 0;
  uint32_t now = millis();
  for (int i = 0; i < 4; i++) {
    if (!(g_data.alertMask & (1 << i))) continue;
    if (snoozeUntil[i] == 0 || (int32_t)(now - snoozeUntil[i]) >= 0) m |= 1 << i;
  }
  return m;
}

// ---- header / nav ----------------------------------------------------------
static void drawHeader(Canvas& c) {
  drawMark(c, 20, 15, 11, ACCENT, BG);
  c.text(F_BRAND, IVORY, BRAND_NAME, 38, 22);

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
  if (page != P_CLOCK) {  // the clock page already shows the time
    if (g_data.haveTime) fmtClock(nowEpoch(), false, b, sizeof b);
    else strcpy(b, "--:--");
    c.text(F_SANS9, DIM, b, 12, 237);
  }

  if (autoActive) {
    c.text(F_SANS9, ACCENT, "auto", 160, 237, BC_DATUM);
  } else {
    for (int i = 0; i < NAV_PAGES; i++) {
      int x = 160 + (2 * i - (NAV_PAGES - 1)) * 7;
      c.circle(x, 231, i == (int)page ? 4 : 3, i == (int)page ? ACCENT : TRACK);
    }
  }
  if (g_data.model[0]) c.text(F_SANS9, DIM, g_data.model, 310, 237, BR_DATUM);
}

static void drawNoLink(Canvas& c) {
  drawMark(c, 160, 84, 30, ACCENT, BG);
  c.text(F_SANSB18, IVORY, "Waiting for bridge", 160, 150, BC_DATUM);
  c.text(F_SANS9, DIM, "run host/install.sh on the Mac", 160, 176, BC_DATUM);
}

// ---- settings --------------------------------------------------------------
struct Rect { int x, y, w, h; };
static const Rect R_BR_MINUS = {176, 46, 40, 32}, R_BR_PLUS = {268, 46, 40, 32};
static const Rect R_DIM_PREV = {176, 84, 40, 32}, R_DIM_NEXT = {268, 84, 40, 32};
static const Rect R_CLOCK = {176, 122, 132, 32};
static const Rect R_AUTO = {176, 160, 132, 32};
static const Rect R_RECAL = {12, 198, 148, 38}, R_BACK = {172, 198, 136, 38};

static bool inRect(const Rect& r, int x, int y) {
  return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void drawBtn(Canvas& c, const Rect& r, const char* t, uint16_t fill = CARD,
                    uint16_t ink = IVORY) {
  c.rrect(r.x, r.y, r.w, r.h, 8, fill);
  c.text(F_SANS12, ink, t, r.x + r.w / 2, r.y + r.h / 2 + 1, MC_DATUM);
}

static void drawSettings(Canvas& c) {
  drawMark(c, 24, 25, 12, ACCENT, BG);
  c.text(F_SANSB18, IVORY, "Settings", 44, 37);
#ifdef BRAND_CLAUDE
  c.text(F_SANS9, MUTED, "Unofficial", 312, 37, BR_DATUM);
#endif
  c.text(F_SANS12, IVORY, "Brightness", 12, 69);
  c.text(F_SANS12, IVORY, "Auto-dim", 12, 107);
  c.text(F_SANS12, IVORY, "Clock", 12, 145);
  c.text(F_SANS12, IVORY, "Auto-switch", 12, 183);

  char b[16];
  drawBtn(c, R_BR_MINUS, "-");
  drawBtn(c, R_BR_PLUS, "+");
  snprintf(b, sizeof b, "%d%%", brightness);
  c.text(F_SANS12, IVORY, b, 242, 63, MC_DATUM);

  drawBtn(c, R_DIM_PREV, "<");
  drawBtn(c, R_DIM_NEXT, ">");
  if (DIM_MIN[dimIdx]) snprintf(b, sizeof b, "%dm", DIM_MIN[dimIdx]);
  else strcpy(b, "Off");
  c.text(F_SANS12, IVORY, b, 242, 101, MC_DATUM);

  drawBtn(c, R_CLOCK, g_h24 ? "24-hour" : "12-hour");
  drawBtn(c, R_AUTO, autoSwitch ? "On" : "Off");
  drawBtn(c, R_RECAL, "Recalibrate");
  drawBtn(c, R_BACK, "Back", ACCENT, BG);
}

// ---- render ----------------------------------------------------------------
static void drawPage(Canvas& c) {
  uint8_t alerts = activeAlerts();
  if (alerts && page != P_SETTINGS) {
    int top = 0;
    for (int i = 0; i < 4; i++) {
      if (alerts & (1 << i)) {
        top = 1 << i;
        break;
      }
    }
    int extra = __builtin_popcount(alerts) - 1;
    drawAlert(c, top, extra, nowEpoch());
    return;
  }
  if (page == P_SETTINGS) {
    drawSettings(c);
    return;
  }
  drawHeader(c);
  if (!g_data.haveTime) {
    drawNoLink(c);
  } else {
    int64_t now = nowEpoch();
    switch (page) {
      case P_HOME: drawHome(c, now); break;
      case P_FORECAST: drawForecast(c, now); break;
      case P_INFO: drawInfo(c, now); break;
      case P_ACTIVITY: drawActivity(c, now); break;
      case P_HEALTH: drawHealth(c); break;
      default: drawClock(c, now); break;
    }
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
      tft.drawFastHLine(x - 12, y, 25, ACCENT);
      tft.drawFastVLine(x, y - 12, 25, ACCENT);
      tft.drawCircle(x, y, 6, ACCENT);
      touchRawWait(raw[i][0], raw[i][1]);
    }
    if (touchSetCal(scr, raw)) break;
  }
  touchSaveCal();
  lastTouch = millis();
  dirty = true;
}

// ---- touch -----------------------------------------------------------------
void uiHandleTouch(const TouchResult& t) {
  uint32_t now = millis();
  lastTouch = now;
  wakeUntil = now + WAKE_MS;
  dirty = true;
  autoActive = false;  // any touch takes over from auto-rotation

  if (dimmed) {  // first touch only wakes the screen
    dimmed = false;
    setBacklight(brightnessDuty());
    return;
  }

  uint8_t alerts = activeAlerts();
  if (alerts && page != P_SETTINGS) {  // tap dismisses and snoozes everything showing
    for (int i = 0; i < 4; i++)
      if (alerts & (1 << i)) snoozeUntil[i] = now + SNOOZE_MS;
    return;
  }

  if (page == P_SETTINGS) {
    if (t.ev != TE_TAP) return;
    if (inRect(R_BR_MINUS, t.x, t.y)) brightness = max(10, brightness - 10);
    else if (inRect(R_BR_PLUS, t.x, t.y)) brightness = min(100, brightness + 10);
    else if (inRect(R_DIM_PREV, t.x, t.y)) dimIdx = (dimIdx + 3) % 4;
    else if (inRect(R_DIM_NEXT, t.x, t.y)) dimIdx = (dimIdx + 1) % 4;
    else if (inRect(R_CLOCK, t.x, t.y)) g_h24 = !g_h24;
    else if (inRect(R_AUTO, t.x, t.y)) autoSwitch = !autoSwitch;
    else if (inRect(R_RECAL, t.x, t.y)) calibrate();
    else if (inRect(R_BACK, t.x, t.y)) page = P_HOME;
    else return;
    setBacklight(brightnessDuty());
    savePrefs();
    return;
  }

  if (t.ev == TE_LONG) {
    page = P_SETTINGS;
  } else if (t.y >= NAV_Y && t.x < 90 && page != P_CLOCK) {
    g_clockMode = !g_clockMode;
  } else if (t.x < 160) {
    page = (Page)((page + NAV_PAGES - 1) % NAV_PAGES);
  } else {
    page = (Page)((page + 1) % NAV_PAGES);
  }
}

// ---- unofficial notice (Claude look only) -----------------------------------
#ifdef BRAND_CLAUDE
#pragma message("BRAND_CLAUDE: unofficial look, not affiliated with or endorsed by Anthropic")
// The Claude look borrows Anthropic's name and styling, so say plainly at every boot that
// this is a fan project.
static void showUnofficialSplash() {
  if (!spriteOk) return;
  for (int band = 0; band < 2; band++) {
    Canvas c{spr, band * 120};
    spr.fillSprite(BG);
    drawMark(c, 160, 66, 30, ACCENT, BG);
    c.text(F_BRAND, IVORY, "Claude usage monitor", 160, 128, BC_DATUM);
    c.text(F_SANS12, DIM, "Unofficial fan project", 160, 160, BC_DATUM);
    c.text(F_SANS12, DIM, "Not affiliated with Anthropic", 160, 186, BC_DATUM);
    spr.pushSprite(0, band * 120);
  }
  delay(6000);
}
#endif

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

#ifdef BRAND_CLAUDE
  showUnofficialSplash();
#endif
  touchBegin();
  if (!touchLoadCal()) calibrate();
  lastTouch = millis();
}

void uiTick() {
  uint32_t now = millis();
  uint8_t alerts = activeAlerts();

  // A newly raised alert lights the screen for a minute even at night / when dimmed.
  if (alerts & ~prevAlerts) {
    wakeUntil = now + WAKE_MS;
    dirty = true;
  }
  if (alerts != prevAlerts) dirty = true;
  prevAlerts = alerts;

  // Backlight: dim after idle time or at night, but never during alerts or settings.
  bool idleDim = DIM_MIN[dimIdx] && now - lastTouch > DIM_MIN[dimIdx] * 60000UL;
  bool nightDim = g_data.night && (int32_t)(now - wakeUntil) >= 0;
  bool shouldDim = page != P_SETTINGS && !alerts && (idleDim || nightDim);
  if (shouldDim != dimmed) {
    dimmed = shouldDim;
    setBacklight(dimmed ? DIM_LEVEL : brightnessDuty());
  }

  // Auto-rotation while Claude is quiet; snap back to Home when it wakes up.
  bool wantAuto = autoSwitch && g_data.idleAuto && linkAlive() && page != P_SETTINGS && !alerts &&
                  now - lastTouch > AUTO_AFTER_TOUCH_MS;
  if (wantAuto) {
    if (!autoActive) {
      autoActive = true;
      autoIdx = 0;
      autoSince = now;
      page = AUTO_PAGE[0];
      dirty = true;
    } else if (now - autoSince >= AUTO_MS[autoIdx]) {
      autoIdx = (autoIdx + 1) % AUTO_N;
      autoSince = now;
      page = AUTO_PAGE[autoIdx];
      dirty = true;
    }
  } else if (autoActive) {
    autoActive = false;
    if (!g_data.idleAuto) page = P_HOME;
    dirty = true;
  } else if (page != P_HOME && now - lastTouch > (page == P_SETTINGS ? 60000UL : 30000UL)) {
    page = P_HOME;
    dirty = true;
  }

  if (dirty || now - lastRender >= 500) {
    render();
    lastRender = now;
    dirty = false;
  }
}
