#include "touch.h"

#include <Preferences.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>

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
  x = (int)lroundf(cal[0] * rx + cal[1] * ry + cal[2]);
  y = (int)lroundf(cal[3] * rx + cal[4] * ry + cal[5]);
  x = constrain(x, 0, 319);
  y = constrain(y, 0, 239);
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
  double x1 = raw[0][0], y1 = raw[0][1], x2 = raw[1][0], y2 = raw[1][1], x3 = raw[2][0],
         y3 = raw[2][1];
  double det = x1 * (y2 - y3) - y1 * (x2 - x3) + (x2 * y3 - x3 * y2);
  if (fabs(det) < 1.0) return false;
  for (int axis = 0; axis < 2; axis++) {
    double X1 = scr[0][axis], X2 = scr[1][axis], X3 = scr[2][axis];
    cal[axis * 3 + 0] = (X1 * (y2 - y3) - y1 * (X2 - X3) + (X2 * y3 - X3 * y2)) / det;
    cal[axis * 3 + 1] = (x1 * (X2 - X3) - X1 * (x2 - x3) + (x2 * X3 - x3 * X2)) / det;
    cal[axis * 3 + 2] =
        (x1 * (y2 * X3 - y3 * X2) - y1 * (x2 * X3 - x3 * X2) + X1 * (x2 * y3 - x3 * y2)) / det;
  }
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
