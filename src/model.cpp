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

  g_data.idleAuto = (doc["auto"] | 0) != 0;
  g_data.night = (doc["night"] | 0) != 0;

  g_data.alertMask = 0;
  for (JsonVariantConst code : doc["al"].as<JsonArrayConst>()) {
    const char* s = code | "";
    if (!strcmp(s, "sess")) g_data.alertMask |= AL_SESS;
    else if (!strcmp(s, "week")) g_data.alertMask |= AL_WEEK;
    else if (!strcmp(s, "disk")) g_data.alertMask |= AL_DISK;
    else if (!strcmp(s, "svc")) g_data.alertMask |= AL_SVC;
  }

  JsonObjectConst sys = doc["sys"];
  if (!sys.isNull()) {
    Sys& o = g_data.sys;
    o.valid = true;
    o.cpu = sys["cpu"] | 0;
    o.mem = sys["mem"] | 0;
    o.disk = sys["disk"] | 0;
    o.freeGb = sys["free"] | 0;
    o.up = sys["up"] | 0;
    o.rx = sys["rx"] | 0;
    o.tx = sys["tx"] | 0;
    o.nsvc = 0;
    JsonArrayConst svc = sys["svc"], svn = sys["svn"];
    for (size_t i = 0; i < svc.size() && i < 4; i++) {
      o.svc[i] = (svc[i] | 0) != 0;
      strlcpy(o.svn[i], svn[i] | "", sizeof(o.svn[i]));
      o.nsvc = i + 1;
    }
  }

  JsonObjectConst act = doc["act"];
  if (!act.isNull()) {
    Act& o = g_data.act;
    o.valid = true;
    o.today = act["today"] | 0.0f;
    o.n = act["n"] | 0;
    o.last = act["last"] | -1;
    JsonArrayConst days = act["days"];
    for (size_t i = 0; i < 7; i++) o.days[i] = i < days.size() ? (days[i] | 0.0f) : 0.0f;
  }
  return true;
}
