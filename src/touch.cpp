#include "touch.h"

#include <Preferences.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>

#include "calmath.h"

#define T_CLK 25
#define T_MOSI 32
#define T_MISO 39
#define T_CS 33
#define T_IRQ 36

static const uint32_t LONG_PRESS_MS = 800;
static const uint32_t RELEASE_MS = 60;
static const int LONG_MOVE_PX = 25;

static SPIClass touchSPI(VSPI);
static XPT2046_Touchscreen ts(T_CS, T_IRQ);

// screen = M * [rawX rawY 1]
static float cal[6];
static bool calOk = false;

void touchBegin() {
  touchSPI.begin(T_CLK, T_MISO, T_MOSI, T_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);
}

static void mapRaw(int rx, int ry, int& x, int& y) {
  calMap(cal, rx, ry, x, y);
}

TouchResult touchPoll() {
  static bool down = false, longFired = false;
  static uint32_t downAt = 0, lastSeen = 0;
  static int sx = 0, sy = 0, lx = 0, ly = 0;

  TouchResult r{TE_NONE, 0, 0};
  uint32_t now = millis();

  if (ts.touched()) {
    TS_Point p = ts.getPoint();
    mapRaw(p.x, p.y, lx, ly);
    lastSeen = now;
    if (!down) {
      down = true;
      longFired = false;
      downAt = now;
      sx = lx;
      sy = ly;
    }
    if (!longFired && now - downAt > LONG_PRESS_MS && abs(lx - sx) < LONG_MOVE_PX &&
        abs(ly - sy) < LONG_MOVE_PX) {
      longFired = true;
      r = {TE_LONG, lx, ly};
    }
  } else if (down && now - lastSeen > RELEASE_MS) {
    down = false;
    if (!longFired && lastSeen - downAt >= 20) r = {TE_TAP, lx, ly};
  }
  return r;
}

void touchRawWait(int& rx, int& ry) {
  for (;;) {
    while (!ts.touched()) delay(10);
    long sumX = 0, sumY = 0;
    int n = 0;
    uint32_t lastTouch = millis();
    while (millis() - lastTouch < 100) {
      if (ts.touched()) {
        TS_Point p = ts.getPoint();
        sumX += p.x;
        sumY += p.y;
        n++;
        lastTouch = millis();
      }
      delay(8);
    }
    if (n >= 6) {  // ignore brushes and glitches
      rx = sumX / n;
      ry = sumY / n;
      return;
    }
  }
}

bool touchSetCal(const int scr[3][2], const int raw[3][2]) {
  if (!calSolve(scr, raw, cal)) return false;
  calOk = true;
  return true;
}

bool touchLoadCal() {
  Preferences p;
  p.begin("cydtouch", false);
  calOk = p.getBytesLength("cal") == sizeof(cal);
  if (calOk) p.getBytes("cal", cal, sizeof(cal));
  p.end();
  return calOk;
}

void touchSaveCal() {
  Preferences p;
  p.begin("cydtouch", false);
  p.putBytes("cal", cal, sizeof(cal));
  p.end();
}
