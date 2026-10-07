// Assembles newline-terminated lines from a byte stream into a fixed buffer. An overlong
// line is dropped whole (not just its head), and blank lines are not reported.
#pragma once
#include <stddef.h>

template <size_t N>
class LineReader {
 public:
  // Feed one byte. Returns the completed line (valid until the next call) or nullptr.
  const char* feed(char ch) {
    if (ch == '\n') {
      buf_[n_] = 0;
      bool ok = !discard_ && n_ > 0;
      n_ = 0;
      discard_ = false;
      return ok ? buf_ : nullptr;
    }
    if (ch == '\r' || discard_) return nullptr;
    if (n_ < N - 1) buf_[n_++] = ch;
    else discard_ = true;
    return nullptr;
  }

 private:
  char buf_[N];
  size_t n_ = 0;
  bool discard_ = false;
};
