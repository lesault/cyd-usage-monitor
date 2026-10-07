#include "calmath.h"

#include <Arduino.h>
#include <math.h>

bool calSolve(const int scr[3][2], const int raw[3][2], float cal[6]) {
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
  return true;
}

void calMap(const float cal[6], int rawX, int rawY, int& x, int& y) {
  x = (int)lroundf(cal[0] * rawX + cal[1] * rawY + cal[2]);
  y = (int)lroundf(cal[3] * rawX + cal[4] * rawY + cal[5]);
  x = constrain(x, 0, 319);
  y = constrain(y, 0, 239);
}
