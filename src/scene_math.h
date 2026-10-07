// How usage maps onto the secret scene (pages_scene.cpp): where the sun is, how much of the
// iceberg is left, how high the sea has risen. Pure, so it is unit-tested natively.
#pragma once
#include "model.h"

static const int SCENE_WATERLINE = 150;   // waterline row with no melt
static const int SCENE_RISE_PX = 6;       // sea level rise at 100% weekly
static const float SCENE_MIN_ICE = 0.2f;  // the iceberg never melts below this fraction

struct SunPos { int x, y; };

float sceneProgress(const Window& w, int64_t now);  // used fraction 0..1; 0 if no data or expired
float sceneIceScale(float weekly);                  // 1.0 intact .. SCENE_MIN_ICE
int sceneWaterline(float weekly);                   // sea rises as the ice melts
float sceneDusk(float session);                     // 0 day .. 1 sunset, from 40% onwards
SunPos sceneSun(float session, int waterline);      // arc: rises, peaks at 50%, sets at 100%
