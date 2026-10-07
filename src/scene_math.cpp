#include "scene_math.h"

#include <math.h>

float sceneProgress(const Window& w, int64_t now) {
  if (!w.valid || w.resetsAt <= now) return 0.0f;
  return constrain(w.pct, 0.0f, 100.0f) / 100.0f;
}

float sceneIceScale(float weekly) { return 1.0f - (1.0f - SCENE_MIN_ICE) * weekly; }

int sceneWaterline(float weekly) { return SCENE_WATERLINE - (int)lroundf(SCENE_RISE_PX * weekly); }

float sceneDusk(float session) { return constrain((session - 0.4f) / 0.6f, 0.0f, 1.0f); }

SunPos sceneSun(float session, int waterline) {
  SunPos p;
  p.x = 36 + (int)lroundf(248 * session);
  p.y = waterline - 6 - (int)lroundf(sinf((float)M_PI * session) * 96);
  return p;
}
