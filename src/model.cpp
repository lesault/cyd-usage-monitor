#include "model.h"

#include <ArduinoJson.h>

Data g_data;

int64_t nowEpoch() {
  return g_data.epochBase + (int64_t)((millis() - g_data.epochMillis) / 1000);
}

uint32_t dataAge() {
  if (!g_data.haveState) return UINT32_MAX;
  return g_data.ageAtRx + (millis() - g_data.rxMillis) / 1000;
}

bool linkAlive() {
  return g_data.haveTime && (millis() - g_data.rxMillis) < 15000;
}

static void readWindow(JsonVariantConst v, Window& w) {
  w.valid = !v.isNull() && !v["p"].isNull() && !v["r"].isNull();
  if (w.valid) {
    w.pct = v["p"].as<float>();
    w.resetsAt = (int64_t)v["r"].as<double>();
  }
}

bool parseFrame(const char* line) {
  JsonDocument doc;
  if (deserializeJson(doc, line) || doc["t"].isNull()) return false;

  g_data.epochBase = (int64_t)doc["t"].as<double>();
  g_data.epochMillis = millis();
  g_data.rxMillis = millis();
  g_data.tz = doc["tz"] | 0;
  g_data.haveTime = true;

  if (!doc["age"].isNull()) {
    g_data.haveState = true;
    g_data.ageAtRx = doc["age"] | 0;
    readWindow(doc["s"], g_data.s);
    readWindow(doc["w"], g_data.w);
    strlcpy(g_data.model, doc["m"] | "", sizeof(g_data.model));
    g_data.ctx = doc["c"].isNull() ? -1 : (int)lroundf(doc["c"].as<float>());
    g_data.cost = doc["cost"].isNull() ? -1 : doc["cost"].as<float>();
    g_data.dur = doc["dur"] | -1;
    g_data.la = doc["la"] | -1;
    g_data.lr = doc["lr"] | -1;
  }
  return true;
}
