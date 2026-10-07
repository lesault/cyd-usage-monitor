// Just enough of Arduino.h for the display-free firmware sources to build and run on the
// host. Only the native test environment puts this directory on the include path.
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>

// Tests set the clock by hand.
extern uint32_t g_fakeMillis;
inline uint32_t millis() { return g_fakeMillis; }

// Captures what the firmware prints on the serial port.
struct SerialShim {
  std::string out;
  void println(const char* s) {
    out += s;
    out += "\n";
  }
};
extern SerialShim Serial;

// strlcpy is not on every libc; always use our own.
inline size_t cyd_strlcpy(char* dst, const char* src, size_t n) {
  size_t len = strlen(src);
  if (n) {
    size_t c = len >= n ? n - 1 : len;
    memcpy(dst, src, c);
    dst[c] = 0;
  }
  return len;
}
#define strlcpy cyd_strlcpy

#define constrain(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
