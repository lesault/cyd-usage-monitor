// Touch calibration maths: a 3-point affine fit from raw touch readings to screen pixels.
#pragma once

// Solve screen = M * [rawX rawY 1] from three raw/screen pairs. cal receives the six
// coefficients row by row. False if the raw points are (nearly) in a line.
bool calSolve(const int scr[3][2], const int raw[3][2], float cal[6]);

// Apply a solved calibration; the result is clamped to the 320x240 screen.
void calMap(const float cal[6], int rawX, int rawY, int& x, int& y);
