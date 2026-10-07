// Text formatting shared by the pages. No display dependencies, so it is unit-tested natively.
#pragma once
#include <Arduino.h>

extern bool g_h24;  // 24-hour clock (defined in ui.cpp)

void fmtDur(int64_t sec, char* b, size_t n);                    // 2h 14m
void fmtAgo(uint32_t sec, char* b, size_t n);                   // 3m ago
void fmtClock(int64_t epoch, bool withDay, char* b, size_t n);  // 14:07 / Fri 09:00
void fmtReset(int64_t epoch, int64_t now, char* b, size_t n);   // weekday only if not today
