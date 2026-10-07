// CYD Claude usage monitor. Frames arrive as newline-delimited JSON from
// host/bridge.py over USB serial; see model.cpp for the schema.
#include <Arduino.h>

#include "linereader.h"
#include "model.h"
#include "touch.h"
#include "ui.h"

static void readSerial() {
  static LineReader<1024> lines;
  while (Serial.available()) {
    const char* line = lines.feed(Serial.read());
    if (line && parseFrame(line)) uiMarkDirty();
  }
}

void setup() {
  Serial.setRxBufferSize(1024);  // a frame must survive a ~70 ms redraw
  Serial.begin(115200);
  uiBegin();
}

void loop() {
  readSerial();
  TouchResult t = touchPoll();
  if (t.ev != TE_NONE) uiHandleTouch(t);
  uiTick();
  delay(5);
}
