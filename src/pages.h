// Page renderers. Each draws into the content area (y 30..220); header, nav and
// the page list live in ui.cpp.
#pragma once
#include "ui_common.h"

// Claude usage (pages_claude.cpp)
void drawHome(Canvas& c, int64_t now);
void drawForecast(Canvas& c, int64_t now);
void drawInfo(Canvas& c, int64_t now);

// Mac and ambient (pages_system.cpp)
void drawHealth(Canvas& c);
void drawActivity(Canvas& c, int64_t now);
void drawClock(Canvas& c, int64_t now);
void drawAlert(Canvas& c, int code, int extraCount, int64_t now);  // full-screen takeover

// Secret scene (pages_scene.cpp): full-screen, draws its own background, no header/nav.
void drawScene(Canvas& c, int64_t now);
