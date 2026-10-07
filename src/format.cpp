#include "format.h"

#include <time.h>

#include "model.h"

void fmtDur(int64_t sec, char* b, size_t n) {
  if (sec < 0) sec = 0;
  int d = sec / 86400, h = (sec % 86400) / 3600, m = (sec % 3600) / 60, s = sec % 60;
  if (d > 0) snprintf(b, n, "%dd %dh", d, h);
  else if (h > 0) snprintf(b, n, "%dh %02dm", h, m);
  else if (m >= 10) snprintf(b, n, "%dm", m);
  else if (m > 0) snprintf(b, n, "%dm %02ds", m, s);
  else snprintf(b, n, "%ds", s);
}

void fmtAgo(uint32_t sec, char* b, size_t n) {
  if (sec < 60) strlcpy(b, "just now", n);
  else if (sec < 3600) snprintf(b, n, "%dm ago", (int)(sec / 60));
  else if (sec < 86400) snprintf(b, n, "%dh ago", (int)(sec / 3600));
  else snprintf(b, n, "%dd ago", (int)(sec / 86400));
}

void fmtClock(int64_t epoch, bool withDay, char* b, size_t n) {
  time_t lt = (time_t)(epoch + g_data.tz);
  struct tm t;
  gmtime_r(&lt, &t);
  static const char* D[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  char tb[12];
  if (g_h24) snprintf(tb, sizeof tb, "%02d:%02d", t.tm_hour, t.tm_min);
  else snprintf(tb, sizeof tb, "%d:%02d%s", t.tm_hour % 12 == 0 ? 12 : t.tm_hour % 12, t.tm_min,
                t.tm_hour < 12 ? "am" : "pm");
  if (withDay) snprintf(b, n, "%s %s", D[t.tm_wday], tb);
  else strlcpy(b, tb, n);
}

void fmtReset(int64_t epoch, int64_t now, char* b, size_t n) {
  bool same = (epoch + g_data.tz) / 86400 == (now + g_data.tz) / 86400;
  fmtClock(epoch, !same, b, n);
}
