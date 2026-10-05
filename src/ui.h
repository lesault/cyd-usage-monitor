#pragma once
#include "touch.h"

void uiBegin();
void uiTick();                       // dimming, auto-return, periodic redraw
void uiHandleTouch(const TouchResult& t);
void uiMarkDirty();
