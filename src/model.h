#pragma once
#include <Arduino.h>

struct Window {
  bool valid = false;
  float pct = 0;
  int64_t resetsAt = 0;  // epoch seconds (UTC)
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
};

extern Data g_data;

int64_t nowEpoch();
uint32_t dataAge();          // seconds since Claude Code last refreshed the state
bool linkAlive();            // a frame arrived recently
bool parseFrame(const char* line);
