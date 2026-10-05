#pragma once
#include <Arduino.h>

enum TouchEvent { TE_NONE, TE_TAP, TE_LONG };

struct TouchResult {
  TouchEvent ev;
  int x, y;  // screen coordinates (320x240 landscape)
};

void touchBegin();
TouchResult touchPoll();  // call every loop iteration

// Calibration: block until a press, return the averaged raw reading.
void touchRawWait(int& rx, int& ry);
// Fit an affine map from three raw readings to three known screen points.
bool touchSetCal(const int scr[3][2], const int raw[3][2]);
bool touchLoadCal();
void touchSaveCal();
