// Mac health, Claude activity, ambient clock and the alert takeover.
#include "model.h"
#include "pages.h"

// ---- Health ----------------------------------------------------------------
static void fmtRate(int kbs, char* b, size_t n) {
  if (kbs < 1000) snprintf(b, n, "%d KB/s", kbs);
  else snprintf(b, n, "%.1f MB/s", kbs / 1024.0f);
}

static void meterRow(Canvas& c, int y, const char* label, const char* sub, int pct) {
  int lw = c.width(F_SANSB12, label);
  c.text(F_SANSB12, IVORY, label, 16, y + 19);
  if (sub[0]) c.text(F_SANS9, DIM, sub, 16 + lw + 8, y + 19);
  char v[8];
  snprintf(v, sizeof v, "%d%%", pct);
  c.text(F_SANSB12, pct < 60 ? IVORY : levelColor(pct), v, 304, y + 19, BR_DATUM);
  drawBar(c, 16, y + 24, 288, 10, pct, levelColor(pct));
}

void drawHealth(Canvas& c) {
  c.rrect(4, 30, 312, 190, 10, CARD);
  const Sys& s = g_data.sys;
  if (!s.valid) {
    c.text(F_SANS12, DIM, "Waiting for Mac stats", 160, 130, BC_DATUM);
    return;
  }
  char sub[24], a[24], b[24], line[48];
  meterRow(c, 38, "CPU", "", s.cpu);
  meterRow(c, 80, "Memory", "", s.mem);
  snprintf(sub, sizeof sub, "%d GB free", s.freeGb);
  meterRow(c, 122, "Disk", sub, s.disk);

  fmtRate(s.rx, a, sizeof a);
  fmtRate(s.tx, b, sizeof b);
  c.text(F_SANS9, DIM, "In", 16, 184);
  c.text(F_SANS9, IVORY, a, 34, 184);
  c.text(F_SANS9, DIM, "Out", 160, 184);
  c.text(F_SANS9, IVORY, b, 188, 184);

  char up[24];
  fmtDur(s.up, up, sizeof up);
  snprintf(line, sizeof line, "Up %s", up);
  c.text(F_SANS9, DIM, line, 16, 208);

  int x = 150;  // services: a dot and a name each, as many as fit
  for (int i = 0; i < s.nsvc; i++) {
    int adv = 14 + c.width(F_SANS9, s.svn[i]) + 12;
    if (x + adv > 308) break;
    c.circle(x + 4, 203, 4, s.svc[i] ? OLIVE : RED);
    c.text(F_SANS9, s.svc[i] ? DIM : RED, s.svn[i], x + 14, 208);
    x += adv;
  }
}

// ---- Activity --------------------------------------------------------------
void drawActivity(Canvas& c, int64_t now) {
  c.rrect(4, 30, 312, 190, 10, CARD);
  const Act& a = g_data.act;
  if (!a.valid) {
    c.text(F_SANS12, DIM, "No activity data yet", 160, 130, BC_DATUM);
    return;
  }
  char buf[32];
  c.text(F_SANS9, DIM, "Today", 16, 48);
  snprintf(buf, sizeof buf, "$%.2f", a.today);
  c.text(F_SANSB24, IVORY, buf, 16, 95);

  snprintf(buf, sizeof buf, "%d session%s", a.n, a.n == 1 ? "" : "s");
  c.text(F_SANSB12, IVORY, buf, 304, 66, BR_DATUM);
  if (a.last < 0) {
    strcpy(buf, "no activity yet");
  } else {
    char ago[16];
    fmtAgo(a.last, ago, sizeof ago);
    snprintf(buf, sizeof buf, "last active %s", ago);
  }
  c.text(F_SANS9, DIM, buf, 304, 86, BR_DATUM);

  c.text(F_SANS9, DIM, "Weekly allowance used per day", 16, 116);

  float maxv = 1.0f;
  for (int i = 0; i < 7; i++) maxv = max(maxv, a.days[i]);
  const int axis = 196, maxh = 56;
  c.rect(14, axis + 1, 292, 1, TRACK);
  int wdToday = (int)(((now + g_data.tz) / 86400 + 4) % 7);  // 1970-01-01 was a Thursday
  static const char* L = "SMTWTFS";
  for (int i = 0; i < 7; i++) {
    int x = 18 + i * 42, cx = x + 13;
    int h = (int)(maxh * a.days[i] / maxv);
    if (a.days[i] > 0 && h < 3) h = 3;
    if (h > 0) c.rrect(x, axis - h, 26, h, 3, i == 6 ? ACCENT : MUTED);
    if (a.days[i] > 0) {
      snprintf(buf, sizeof buf, a.days[i] >= 10 || a.days[i] == (int)a.days[i] ? "%.0f" : "%.1f",
               a.days[i]);
      c.text(F_SANS9, DIM, buf, cx, axis - h - 4, BC_DATUM);
    }
    char d[2] = {L[(wdToday - (6 - i) + 14) % 7], 0};
    c.text(F_SANS9, i == 6 ? IVORY : DIM, d, cx, 214, BC_DATUM);
  }
}

// ---- Clock -----------------------------------------------------------------
void drawClock(Canvas& c, int64_t now) {
  time_t lt = (time_t)(now + g_data.tz);
  struct tm t;
  gmtime_r(&lt, &t);

  char hh[4], mm[4];
  snprintf(mm, sizeof mm, "%02d", t.tm_min);
  if (g_h24) snprintf(hh, sizeof hh, "%02d", t.tm_hour);
  else snprintf(hh, sizeof hh, "%d", t.tm_hour % 12 == 0 ? 12 : t.tm_hour % 12);

  const uint8_t SZ = 2;
  const int base = 126;
  int dw = c.width(F_SANSB24, "00", SZ), cw = c.width(F_SANSB24, ":", SZ);
  const char* suffix = g_h24 ? "" : (t.tm_hour < 12 ? "am" : "pm");
  int sw = g_h24 ? 0 : c.width(F_SANS12, suffix) + 6;
  int x0 = 160 - (dw + cw + dw + sw) / 2;

  c.text(F_SANSB24, IVORY, hh, x0 + dw, base, BR_DATUM, SZ);
  if (t.tm_sec % 2 == 0) c.text(F_SANSB24, ACCENT, ":", x0 + dw, base, BL_DATUM, SZ);
  c.text(F_SANSB24, IVORY, mm, x0 + dw + cw, base, BL_DATUM, SZ);
  if (sw) c.text(F_SANS12, DIM, suffix, x0 + dw + cw + dw + 6, base, BL_DATUM);

  static const char* DAYS[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday",
                               "Saturday"};
  static const char* MONTHS[] = {"January", "February", "March", "April", "May", "June", "July",
                                 "August", "September", "October", "November", "December"};
  char date[40];
  snprintf(date, sizeof date, "%s %d %s", DAYS[t.tm_wday], t.tm_mday, MONTHS[t.tm_mon]);
  c.text(F_BRAND, IVORY, date, 160, 160, BC_DATUM);

  char line[40];
  const Window &s = g_data.s, &w = g_data.w;
  if (s.valid && s.resetsAt > now && w.valid && w.resetsAt > now)
    snprintf(line, sizeof line, "Session %d%%   -   Week %d%%", (int)lroundf(s.pct),
             (int)lroundf(w.pct));
  else if (w.valid && w.resetsAt > now)
    snprintf(line, sizeof line, "Claude idle   -   Week %d%%", (int)lroundf(w.pct));
  else
    strcpy(line, "Claude idle");
  c.text(F_SANS12, DIM, line, 160, 196, BC_DATUM);
}

// ---- Alert takeover --------------------------------------------------------
void drawAlert(Canvas& c, int code, int extraCount, int64_t now) {
  uint16_t col = (code == AL_SESS || code == AL_SVC) ? RED : AMBER;
  const char* title = "";
  char reason[48] = "", d[24];

  if (code == AL_SESS || code == AL_WEEK) {
    const Window& w = code == AL_SESS ? g_data.s : g_data.w;
    title = code == AL_SESS ? "Session limit near" : "Weekly limit near";
    fmtDur(w.resetsAt - now, d, sizeof d);
    snprintf(reason, sizeof reason, "%d%% used, resets in %s", (int)lroundf(w.pct), d);
  } else if (code == AL_DISK) {
    title = "Low disk space";
    snprintf(reason, sizeof reason, "%d GB free (%d%% used)", g_data.sys.freeGb, g_data.sys.disk);
  } else {
    title = "Service down";
    const Sys& s = g_data.sys;
    for (int i = 0; i < s.nsvc; i++) {
      if (!s.svc[i]) {
        snprintf(reason, sizeof reason, "%s is not running", s.svn[i]);
        break;
      }
    }
  }

  // inset 4 px like the cards: the panel edge is not fully visible
  for (int i = 0; i < 3; i++) c.outline(4 + i, 4 + i, 312 - 2 * i, 232 - 2 * i, 12 - i, col);
  drawMark(c, 160, 62, 30, col, BG);
  c.text(F_SANSB18, IVORY, title, 160, 132, BC_DATUM);
  c.text(F_SANS12, DIM, reason, 160, 164, BC_DATUM);

  char foot[40] = "Tap to dismiss";
  if (extraCount > 0) snprintf(foot, sizeof foot, "+%d more  -  tap to dismiss", extraCount);
  c.text(F_SANS9, MUTED, foot, 160, 212, BC_DATUM);
}
