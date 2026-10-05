// CYD Claude usage monitor. Frames arrive as newline-delimited JSON from
// host/bridge.py over USB serial; see model.cpp for the schema.
#include <Arduino.h>

#include "model.h"
#include "touch.h"
#include "ui.h"

static void readSerial() {
  static char buf[768];
  static size_t n = 0;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n') {
      buf[n] = 0;
      if (n > 0 && parseFrame(buf)) uiMarkDirty();
      n = 0;
    } else if (ch != '\r') {
      if (n < sizeof(buf) - 1) buf[n++] = ch;
      else n = 0;  // overlong line: drop it
    }
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
