#pragma once
#include <Arduino.h>

struct Window {
  bool valid = false;
  float pct = 0;
  int64_t resetsAt = 0;  // epoch seconds (UTC)
};

// Alert codes sent by the bridge in "al"; bit positions in Data::alertMask.
enum AlertBit { AL_SESS = 1, AL_WEEK = 2, AL_DISK = 4, AL_SVC = 8 };

struct Sys {
  bool valid = false;
  int cpu = 0, mem = 0, disk = 0;  // %
  int freeGb = 0;
  uint32_t up = 0;                 // uptime, seconds
  int rx = 0, tx = 0;              // KB/s
  uint8_t nsvc = 0;
  bool svc[4] = {false, false, false, false};
  char svn[4][14] = {"", "", "", ""};
};

struct Act {
  bool valid = false;
  float today = 0;     // USD
  int n = 0;           // sessions today
  int32_t last = -1;   // seconds since Claude last changed, -1 = never
  float days[7] = {0, 0, 0, 0, 0, 0, 0};  // weekly-% used per day, oldest first
};

struct Data {
  bool haveTime = false;   // at least one frame received (clock is valid)
  bool haveState = false;  // frame carried Claude Code state
  Window s, w;             // 5-hour session, 7-day week
  char model[24] = "";
  int ctx = -1;            // context window %, -1 = unknown
  float cost = -1;         // USD
  int dur = -1;            // session duration, seconds
  int la = -1, lr = -1;    // lines added / removed
  int32_t tz = 0;          // local UTC offset, seconds
  int64_t epochBase = 0;   // epoch at epochMillis
  uint32_t epochMillis = 0;
  uint32_t rxMillis = 0;   // when the last frame arrived
  uint32_t ageAtRx = 0;    // age of the Claude state when the frame was sent
  Sys sys;
  Act act;
  bool idleAuto = false;   // Claude has been quiet: rotate ambient pages
  bool night = false;      // quiet hours: dim
  uint8_t alertMask = 0;   // AlertBit flags currently raised by the bridge
};

extern Data g_data;

int64_t nowEpoch();
uint32_t dataAge();          // seconds since Claude Code last refreshed the state
bool linkAlive();            // a frame arrived recently
bool parseFrame(const char* line);
