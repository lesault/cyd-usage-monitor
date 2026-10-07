// Host-side tests for the display-free firmware logic. Run: pio test -e native
#include <unity.h>

#include "calmath.h"
#include "forecast.h"
#include "format.h"
#include "linereader.h"
#include "model.h"
#include "scene_math.h"

uint32_t g_fakeMillis = 0;
SerialShim Serial;
bool g_h24 = true;

void setUp() {
  g_data = Data();
  g_fakeMillis = 0;
  g_h24 = true;
  Serial.out.clear();
}
void tearDown() {}

// 2023-11-14 22:13:20 UTC, a Tuesday.
static const int64_t T0 = 1700000000;

// ---- parseFrame ------------------------------------------------------------
void test_time_only_frame() {
  TEST_ASSERT_TRUE(parseFrame("{\"t\":1700000000,\"tz\":3600}"));
  TEST_ASSERT_TRUE(g_data.haveTime);
  TEST_ASSERT_FALSE(g_data.haveState);
  TEST_ASSERT_EQUAL_INT32(3600, g_data.tz);
  TEST_ASSERT_EQUAL_INT64(T0, g_data.epochBase);
}

void test_full_frame() {
  TEST_ASSERT_TRUE(parseFrame(
      "{\"t\":1700000000,\"tz\":0,\"age\":7,\"m\":\"Sonnet 5.5\",\"c\":31.6,\"cost\":1.84,"
      "\"dur\":5400,\"la\":212,\"lr\":37,\"s\":{\"p\":58.5,\"r\":1700010000},"
      "\"w\":{\"p\":40,\"r\":1700400000},\"auto\":1,\"night\":1,"
      "\"sys\":{\"cpu\":23,\"mem\":71,\"disk\":4,\"free\":308,\"up\":432000,\"rx\":120,\"tx\":30,"
      "\"svc\":[1,0],\"svn\":[\"claude-cyd\",\"tailscale\"]},"
      "\"act\":{\"today\":1.84,\"n\":3,\"last\":120,\"days\":[0,3,8,2,0,5,4]}}"));
  TEST_ASSERT_TRUE(g_data.haveState);
  TEST_ASSERT_EQUAL_UINT32(7, g_data.ageAtRx);
  TEST_ASSERT_EQUAL_STRING("Sonnet 5.5", g_data.model);
  TEST_ASSERT_EQUAL_INT(32, g_data.ctx);  // rounded
  TEST_ASSERT_EQUAL_FLOAT(1.84f, g_data.cost);
  TEST_ASSERT_EQUAL_INT(5400, g_data.dur);
  TEST_ASSERT_EQUAL_INT(212, g_data.la);
  TEST_ASSERT_EQUAL_INT(37, g_data.lr);
  TEST_ASSERT_TRUE(g_data.s.valid);
  TEST_ASSERT_EQUAL_FLOAT(58.5f, g_data.s.pct);
  TEST_ASSERT_EQUAL_INT64(1700010000, g_data.s.resetsAt);
  TEST_ASSERT_EQUAL_INT64(1700400000, g_data.w.resetsAt);
  TEST_ASSERT_TRUE(g_data.idleAuto);
  TEST_ASSERT_TRUE(g_data.night);
  TEST_ASSERT_TRUE(g_data.sys.valid);
  TEST_ASSERT_EQUAL_INT(308, g_data.sys.freeGb);
  TEST_ASSERT_EQUAL_UINT32(432000, g_data.sys.up);
  TEST_ASSERT_EQUAL_UINT8(2, g_data.sys.nsvc);
  TEST_ASSERT_TRUE(g_data.sys.svc[0]);
  TEST_ASSERT_FALSE(g_data.sys.svc[1]);
  TEST_ASSERT_EQUAL_STRING("tailscale", g_data.sys.svn[1]);
  TEST_ASSERT_TRUE(g_data.act.valid);
  TEST_ASSERT_EQUAL_FLOAT(1.84f, g_data.act.today);
  TEST_ASSERT_EQUAL_INT(3, g_data.act.n);
  TEST_ASSERT_EQUAL_INT32(120, g_data.act.last);
  TEST_ASSERT_EQUAL_FLOAT(5.0f, g_data.act.days[5]);
}

void test_null_and_missing_values_use_sentinels() {
  TEST_ASSERT_TRUE(parseFrame("{\"t\":1,\"age\":0,\"c\":null,\"cost\":null,\"s\":{\"p\":null,\"r\":5}}"));
  TEST_ASSERT_EQUAL_INT(-1, g_data.ctx);
  TEST_ASSERT_EQUAL_FLOAT(-1.0f, g_data.cost);
  TEST_ASSERT_EQUAL_INT(-1, g_data.dur);
  TEST_ASSERT_EQUAL_INT(-1, g_data.la);
  TEST_ASSERT_FALSE(g_data.s.valid);  // a window needs both p and r
  TEST_ASSERT_FALSE(g_data.w.valid);
  TEST_ASSERT_EQUAL_STRING("", g_data.model);
}

void test_bad_input_is_rejected_and_changes_nothing() {
  TEST_ASSERT_TRUE(parseFrame("{\"t\":1700000000,\"tz\":3600}"));
  const char* bad[] = {"", "{oops", "[1,2]", "{\"tz\":5}", "null", "{\"t\":null}"};
  for (const char* line : bad) TEST_ASSERT_FALSE_MESSAGE(parseFrame(line), line);
  TEST_ASSERT_EQUAL_INT32(3600, g_data.tz);
  TEST_ASSERT_EQUAL_INT64(T0, g_data.epochBase);
}

void test_probe_is_answered_and_is_not_a_frame() {
  TEST_ASSERT_FALSE(parseFrame("{\"probe\":1}"));
  TEST_ASSERT_EQUAL_STRING("{\"cyd\":1}\n", Serial.out.c_str());
  TEST_ASSERT_FALSE(g_data.haveTime);
}

void test_alert_codes() {
  parseFrame("{\"t\":1,\"al\":[\"sess\",\"disk\",\"bogus\",5]}");
  TEST_ASSERT_EQUAL_UINT8(AL_SESS | AL_DISK, g_data.alertMask);
  parseFrame("{\"t\":1,\"al\":[\"week\",\"svc\"]}");
  TEST_ASSERT_EQUAL_UINT8(AL_WEEK | AL_SVC, g_data.alertMask);
  parseFrame("{\"t\":1}");
  TEST_ASSERT_EQUAL_UINT8(0, g_data.alertMask);  // absent means cleared
}

void test_services_are_capped_and_names_truncated() {
  parseFrame("{\"t\":1,\"sys\":{\"svc\":[1,1,1,1,1,1],"
             "\"svn\":[\"a\",\"b\",\"c\",\"d\",\"e\",\"f\"]}}");
  TEST_ASSERT_EQUAL_UINT8(4, g_data.sys.nsvc);
  parseFrame("{\"t\":1,\"sys\":{\"svc\":[1],\"svn\":[\"averyveryverylongname\"]}}");
  TEST_ASSERT_EQUAL_UINT8(1, g_data.sys.nsvc);
  TEST_ASSERT_EQUAL_INT(13, (int)strlen(g_data.sys.svn[0]));
}

void test_activity_days_are_padded() {
  parseFrame("{\"t\":1,\"act\":{\"days\":[1,2]}}");
  TEST_ASSERT_EQUAL_FLOAT(2.0f, g_data.act.days[1]);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, g_data.act.days[6]);
  TEST_ASSERT_EQUAL_INT32(-1, g_data.act.last);
}

void test_state_survives_a_frame_without_it() {
  parseFrame("{\"t\":1,\"age\":3,\"m\":\"Opus\"}");
  parseFrame("{\"t\":2}");
  TEST_ASSERT_TRUE(g_data.haveState);
  TEST_ASSERT_EQUAL_STRING("Opus", g_data.model);
}

// ---- clocks ----------------------------------------------------------------
void test_clock_helpers() {
  g_fakeMillis = 5000;
  parseFrame("{\"t\":1700000000,\"age\":10}");
  TEST_ASSERT_TRUE(linkAlive());
  g_fakeMillis = 5000 + 3500;
  TEST_ASSERT_EQUAL_INT64(T0 + 3, nowEpoch());
  TEST_ASSERT_EQUAL_UINT32(13, dataAge());
  g_fakeMillis = 5000 + 15000;
  TEST_ASSERT_FALSE(linkAlive());
}

void test_clock_helpers_survive_millis_wrap() {
  g_fakeMillis = UINT32_MAX - 1500;
  parseFrame("{\"t\":1700000000,\"age\":0}");
  g_fakeMillis = 2500;  // wrapped: 4 seconds later
  TEST_ASSERT_EQUAL_INT64(T0 + 4, nowEpoch());
  TEST_ASSERT_EQUAL_UINT32(4, dataAge());
  TEST_ASSERT_TRUE(linkAlive());
}

void test_no_state_means_infinite_age_and_no_link_before_a_frame() {
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, dataAge());
  TEST_ASSERT_FALSE(linkAlive());
}

// ---- format ----------------------------------------------------------------
static const char* dur(int64_t s) {
  static char b[24];
  fmtDur(s, b, sizeof b);
  return b;
}
void test_fmtDur() {
  TEST_ASSERT_EQUAL_STRING("0s", dur(0));
  TEST_ASSERT_EQUAL_STRING("0s", dur(-30));
  TEST_ASSERT_EQUAL_STRING("59s", dur(59));
  TEST_ASSERT_EQUAL_STRING("1m 00s", dur(60));
  TEST_ASSERT_EQUAL_STRING("9m 59s", dur(599));
  TEST_ASSERT_EQUAL_STRING("10m", dur(600));
  TEST_ASSERT_EQUAL_STRING("59m", dur(3599));
  TEST_ASSERT_EQUAL_STRING("1h 00m", dur(3600));
  TEST_ASSERT_EQUAL_STRING("2h 03m", dur(7384));
  TEST_ASSERT_EQUAL_STRING("1d 1h", dur(90000));
}

void test_fmtAgo() {
  char b[24];
  fmtAgo(0, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("just now", b);
  fmtAgo(59, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("just now", b);
  fmtAgo(120, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("2m ago", b);
  fmtAgo(7200, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("2h ago", b);
  fmtAgo(2 * 86400 + 5, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("2d ago", b);
}

void test_fmtClock() {
  char b[24];
  fmtClock(T0, false, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("22:13", b);
  fmtClock(T0, true, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("Tue 22:13", b);
  g_h24 = false;
  fmtClock(T0, false, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("10:13pm", b);
  g_data.tz = 2 * 3600;  // 00:13 Wednesday locally
  fmtClock(T0, true, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("Wed 12:13am", b);
  fmtClock(T0 + 12 * 3600, false, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("12:13pm", b);  // noon hour is 12, not 0
}

void test_fmtReset_shows_the_day_only_when_it_differs() {
  char b[24];
  fmtReset(T0 + 3600, T0, b, sizeof b);  // 23:13 same day
  TEST_ASSERT_EQUAL_STRING("23:13", b);
  fmtReset(T0 + 7200, T0, b, sizeof b);  // 00:13 Wednesday
  TEST_ASSERT_EQUAL_STRING("Wed 00:13", b);
  g_data.tz = -3600;  // locally both are still Tuesday (21:13 and 23:13)
  fmtReset(T0 + 7200, T0, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("23:13", b);
}

void test_formatters_never_overflow_the_buffer() {
  char b[6];
  fmtDur(90000, b, sizeof b);
  TEST_ASSERT_EQUAL_INT(5, (int)strlen(b));
  fmtAgo(100000, b, sizeof b);
  TEST_ASSERT_TRUE(strlen(b) <= 5);
}

// ---- forecast --------------------------------------------------------------
static Window win(float pct, int64_t resetsAt) {
  Window w;
  w.valid = true;
  w.pct = pct;
  w.resetsAt = resetsAt;
  return w;
}

void test_session_forecast_cases() {
  const int64_t H = 3600;
  // 2 h into a 5 h window.
  int64_t now = T0, reset = T0 + 3 * H;
  Forecast f = forecastSession(win(50, reset), now);  // 25%/h: full in 2 h, before the reset
  TEST_ASSERT_EQUAL(FC_LIMIT_SOON, f.kind);
  TEST_ASSERT_EQUAL_FLOAT(25.0f, f.rate);
  TEST_ASSERT_EQUAL_INT64(now + 2 * H, f.limitAt);
  TEST_ASSERT_EQUAL_FLOAT(0.4f, f.elapsed);

  f = forecastSession(win(20, reset), now);  // 10%/h: 8 h to full, after the reset
  TEST_ASSERT_EQUAL(FC_SAFE, f.kind);
  TEST_ASSERT_EQUAL_FLOAT(50.0f, f.projected);

  TEST_ASSERT_EQUAL(FC_LIMIT_REACHED, forecastSession(win(100, reset), now).kind);
  TEST_ASSERT_EQUAL(FC_TOO_EARLY, forecastSession(win(0.5f, reset), now).kind);
  TEST_ASSERT_EQUAL(FC_TOO_EARLY, forecastSession(win(30, now + 5 * H - 60), now).kind);  // 1 min in
  TEST_ASSERT_EQUAL(FC_RESET, forecastSession(win(80, now - 1), now).kind);
  TEST_ASSERT_EQUAL(FC_RESET, forecastSession(win(80, now), now).kind);
}

void test_session_limit_reached_wins_over_too_early() {
  TEST_ASSERT_EQUAL(FC_LIMIT_REACHED, forecastSession(win(100, T0 + 5 * 3600 - 10), T0).kind);
}

void test_week_forecast_cases() {
  const int64_t D = 86400;
  int64_t now = T0, reset = T0 + 3 * D + D / 2;  // exactly half the week gone
  Forecast f = forecastWeek(win(60, reset), now);
  TEST_ASSERT_EQUAL(FC_PROJECTED, f.kind);
  TEST_ASSERT_EQUAL_FLOAT(0.5f, f.elapsed);
  TEST_ASSERT_EQUAL_FLOAT(120.0f, f.projected);
  TEST_ASSERT_EQUAL_FLOAT(10.0f, f.diff);   // 10 points ahead of an even pace
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 17.14f, f.rate);

  f = forecastWeek(win(30, reset), now);
  TEST_ASSERT_EQUAL_FLOAT(-20.0f, f.diff);  // behind pace

  TEST_ASSERT_EQUAL(FC_TOO_EARLY, forecastWeek(win(5, now + 7 * D - D / 10), now).kind);  // 1.4% in
  TEST_ASSERT_EQUAL(FC_TOO_EARLY, forecastWeek(win(0.5f, reset), now).kind);
  TEST_ASSERT_EQUAL(FC_RESET, forecastWeek(win(60, now - 5), now).kind);
}

void test_forecast_elapsed_is_clamped() {
  TEST_ASSERT_EQUAL_FLOAT(0.0f, forecastWeek(win(40, T0 + 9 * 86400), T0).elapsed);  // window not begun
}

// ---- scene maths -----------------------------------------------------------
void test_scene_progress() {
  Window none;
  TEST_ASSERT_EQUAL_FLOAT(0.0f, sceneProgress(none, T0));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, sceneProgress(win(80, T0 - 1), T0));  // expired
  TEST_ASSERT_EQUAL_FLOAT(0.4f, sceneProgress(win(40, T0 + 60), T0));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, sceneProgress(win(140, T0 + 60), T0));  // clamped
  TEST_ASSERT_EQUAL_FLOAT(0.0f, sceneProgress(win(-5, T0 + 60), T0));
}

void test_ice_scale_and_sea_level() {
  TEST_ASSERT_EQUAL_FLOAT(1.0f, sceneIceScale(0));
  TEST_ASSERT_EQUAL_FLOAT(SCENE_MIN_ICE, sceneIceScale(1));
  TEST_ASSERT_EQUAL_FLOAT(0.6f, sceneIceScale(0.5f));
  TEST_ASSERT_EQUAL_INT(SCENE_WATERLINE, sceneWaterline(0));
  TEST_ASSERT_EQUAL_INT(SCENE_WATERLINE - SCENE_RISE_PX, sceneWaterline(1));
  TEST_ASSERT_TRUE(sceneWaterline(0.9f) <= sceneWaterline(0.1f));  // monotonic
}

void test_sun_arc() {
  int wl = SCENE_WATERLINE;
  SunPos a = sceneSun(0, wl), b = sceneSun(0.5f, wl), c = sceneSun(1, wl);
  TEST_ASSERT_EQUAL_INT(36, a.x);
  TEST_ASSERT_EQUAL_INT(wl - 6, a.y);   // on the horizon at the left
  TEST_ASSERT_EQUAL_INT(160, b.x);
  TEST_ASSERT_EQUAL_INT(wl - 6 - 96, b.y);  // top of the arc
  TEST_ASSERT_EQUAL_INT(284, c.x);
  TEST_ASSERT_EQUAL_INT(wl - 6, c.y);   // setting at the right
  for (float p = 0; p < 1.0f; p += 0.05f)  // moves right as usage grows
    TEST_ASSERT_TRUE(sceneSun(p + 0.05f, wl).x >= sceneSun(p, wl).x);
}

void test_dusk_starts_at_forty_percent() {
  TEST_ASSERT_EQUAL_FLOAT(0.0f, sceneDusk(0));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, sceneDusk(0.4f));
  TEST_ASSERT_EQUAL_FLOAT(0.5f, sceneDusk(0.7f));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, sceneDusk(1));
}

// ---- touch calibration -----------------------------------------------------
static void raw_for(const int scr[3][2], int raw[3][2], float sx, float sy, int ox, int oy, bool swap) {
  // Invert screen = (sx*raw + ox) to fabricate readings, optionally with the axes swapped.
  for (int i = 0; i < 3; i++) {
    int a = (int)((scr[i][0] - ox) / sx), b = (int)((scr[i][1] - oy) / sy);
    raw[i][0] = swap ? b : a;
    raw[i][1] = swap ? a : b;
  }
}

void test_calibration_recovers_the_screen_points() {
  const int scr[3][2] = {{20, 20}, {300, 40}, {160, 220}};
  int raw[3][2];
  raw_for(scr, raw, 0.08f, 0.06f, -10, -15, false);
  float cal[6];
  TEST_ASSERT_TRUE(calSolve(scr, raw, cal));
  for (int i = 0; i < 3; i++) {
    int x, y;
    calMap(cal, raw[i][0], raw[i][1], x, y);
    TEST_ASSERT_INT_WITHIN(1, scr[i][0], x);
    TEST_ASSERT_INT_WITHIN(1, scr[i][1], y);
  }
}

void test_calibration_handles_swapped_axes_and_other_points() {
  const int scr[3][2] = {{20, 20}, {300, 40}, {160, 220}};
  int raw[3][2];
  raw_for(scr, raw, 0.08f, 0.06f, -10, -15, true);
  float cal[6];
  TEST_ASSERT_TRUE(calSolve(scr, raw, cal));
  int x, y;
  // A point halfway between corners 1 and 2 in screen space maps to the same in raw space.
  int rx = (raw[0][0] + raw[1][0]) / 2, ry = (raw[0][1] + raw[1][1]) / 2;
  calMap(cal, rx, ry, x, y);
  TEST_ASSERT_INT_WITHIN(2, 160, x);
  TEST_ASSERT_INT_WITHIN(2, 30, y);
}

void test_calibration_rejects_collinear_points_and_clamps() {
  const int scr[3][2] = {{20, 20}, {300, 40}, {160, 220}};
  const int line[3][2] = {{100, 100}, {200, 200}, {300, 300}};
  float cal[6];
  TEST_ASSERT_FALSE(calSolve(scr, line, cal));

  int raw[3][2];
  raw_for(scr, raw, 0.08f, 0.06f, -10, -15, false);
  TEST_ASSERT_TRUE(calSolve(scr, raw, cal));
  int x, y;
  calMap(cal, -50000, 50000, x, y);
  TEST_ASSERT_EQUAL_INT(0, x);
  TEST_ASSERT_EQUAL_INT(239, y);
  calMap(cal, 50000, -50000, x, y);
  TEST_ASSERT_EQUAL_INT(319, x);
  TEST_ASSERT_EQUAL_INT(0, y);
}

// ---- line reader -----------------------------------------------------------
static const char* feedAll(LineReader<16>& r, const char* s) {
  const char* got = nullptr;
  for (; *s; s++) {
    const char* l = r.feed(*s);
    if (l) got = l;
  }
  return got;
}

void test_linereader_basic_lines() {
  LineReader<16> r;
  TEST_ASSERT_EQUAL_STRING("hello", feedAll(r, "hello\n"));
  TEST_ASSERT_EQUAL_STRING("a b", feedAll(r, "a b\r\n"));  // CRLF
  TEST_ASSERT_NULL(feedAll(r, "\n\r\n\n"));                // blank lines are not lines
  TEST_ASSERT_NULL(feedAll(r, "partial"));                 // no newline yet
  TEST_ASSERT_EQUAL_STRING("partialdone", feedAll(r, "done\n"));
}

void test_linereader_drops_an_overlong_line_whole() {
  LineReader<16> r;
  TEST_ASSERT_EQUAL_STRING("123456789012345", feedAll(r, "123456789012345\n"));  // 15 chars fit
  TEST_ASSERT_NULL(feedAll(r, "1234567890123456\n"));                           // 16 do not
  // The tail of an overlong line must not be mistaken for a line of its own.
  TEST_ASSERT_NULL(feedAll(r, "0123456789abcdefghij{\"t\":1}\n"));
  TEST_ASSERT_EQUAL_STRING("ok", feedAll(r, "ok\n"));  // and the next line is fine
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_time_only_frame);
  RUN_TEST(test_full_frame);
  RUN_TEST(test_null_and_missing_values_use_sentinels);
  RUN_TEST(test_bad_input_is_rejected_and_changes_nothing);
  RUN_TEST(test_probe_is_answered_and_is_not_a_frame);
  RUN_TEST(test_alert_codes);
  RUN_TEST(test_services_are_capped_and_names_truncated);
  RUN_TEST(test_activity_days_are_padded);
  RUN_TEST(test_state_survives_a_frame_without_it);
  RUN_TEST(test_clock_helpers);
  RUN_TEST(test_clock_helpers_survive_millis_wrap);
  RUN_TEST(test_no_state_means_infinite_age_and_no_link_before_a_frame);
  RUN_TEST(test_fmtDur);
  RUN_TEST(test_fmtAgo);
  RUN_TEST(test_fmtClock);
  RUN_TEST(test_fmtReset_shows_the_day_only_when_it_differs);
  RUN_TEST(test_formatters_never_overflow_the_buffer);
  RUN_TEST(test_session_forecast_cases);
  RUN_TEST(test_session_limit_reached_wins_over_too_early);
  RUN_TEST(test_week_forecast_cases);
  RUN_TEST(test_forecast_elapsed_is_clamped);
  RUN_TEST(test_scene_progress);
  RUN_TEST(test_ice_scale_and_sea_level);
  RUN_TEST(test_sun_arc);
  RUN_TEST(test_dusk_starts_at_forty_percent);
  RUN_TEST(test_calibration_recovers_the_screen_points);
  RUN_TEST(test_calibration_handles_swapped_axes_and_other_points);
  RUN_TEST(test_calibration_rejects_collinear_points_and_clamps);
  RUN_TEST(test_linereader_basic_lines);
  RUN_TEST(test_linereader_drops_an_overlong_line_whole);
  return UNITY_END();
}
