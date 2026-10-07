// Pace projections for the Forecast page. Pure maths on a usage window.
#pragma once
#include "model.h"

enum ForecastKind {
  FC_RESET,         // the window has already reset
  FC_LIMIT_REACHED, // session is at 100%
  FC_TOO_EARLY,     // not enough usage or elapsed time to say
  FC_LIMIT_SOON,    // session: on pace to hit 100% before it resets
  FC_SAFE,          // session: on pace to stay under until it resets
  FC_PROJECTED,     // week: projected % at reset
};

struct Forecast {
  ForecastKind kind;
  float elapsed;     // fraction of the window gone, 0..1
  float rate;        // session: % per hour, week: % per day
  int64_t limitAt;   // FC_LIMIT_SOON: epoch the session hits 100%
  float projected;   // % used by reset at the current pace
  float diff;        // week: used % minus elapsed % (positive = ahead of pace)
};

Forecast forecastSession(const Window& w, int64_t now);  // w must be valid
Forecast forecastWeek(const Window& w, int64_t now);     // w must be valid
