#include "forecast.h"

static const int64_t SESSION_SPAN = 5 * 3600;
static const int64_t WEEK_SPAN = 7 * 86400;

static float elapsedFrac(const Window& w, int64_t now, int64_t span) {
  return constrain((now - (w.resetsAt - span)) / (float)span, 0.0f, 1.0f);
}

Forecast forecastSession(const Window& w, int64_t now) {
  Forecast f = {FC_RESET, 0, 0, 0, 0, 0};
  if (w.resetsAt <= now) return f;
  f.elapsed = elapsedFrac(w, now, SESSION_SPAN);
  float hrs = f.elapsed * SESSION_SPAN / 3600.0f;
  if (w.pct >= 100) {
    f.kind = FC_LIMIT_REACHED;
  } else if (hrs < 0.1f || w.pct < 1) {
    f.kind = FC_TOO_EARLY;
  } else {
    f.rate = w.pct / hrs;
    f.projected = w.pct / f.elapsed;
    f.limitAt = now + (int64_t)((100 - w.pct) / f.rate * 3600);
    f.kind = f.limitAt < w.resetsAt ? FC_LIMIT_SOON : FC_SAFE;
  }
  return f;
}

Forecast forecastWeek(const Window& w, int64_t now) {
  Forecast f = {FC_RESET, 0, 0, 0, 0, 0};
  if (w.resetsAt <= now) return f;
  f.elapsed = elapsedFrac(w, now, WEEK_SPAN);
  if (f.elapsed < 0.03f || w.pct < 1) {
    f.kind = FC_TOO_EARLY;
  } else {
    f.kind = FC_PROJECTED;
    f.projected = w.pct / f.elapsed;
    f.diff = w.pct - f.elapsed * 100;
    f.rate = w.pct / (f.elapsed * 7);
  }
  return f;
}
