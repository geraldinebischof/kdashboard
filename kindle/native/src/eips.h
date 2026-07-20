#ifndef KINDLE_DASHBOARD_EIPS_H
#define KINDLE_DASHBOARD_EIPS_H

#include "constants.h"

// Accumulates up to kMaxRows lines of fixed-width (kScreenColumns) text and
// flushes them to the screen via the `eips` CLI (falling back to stdout when
// eips is unavailable). This is the low-fidelity text renderer used when the
// framebuffer/bitmap path is not available.
class EipsRenderer {
 public:
  void reset();
  void addLine(const char* text);
  void addRule();
  void addCardText(const char* text);
  void addSectionTitle(const char* title);

  // Emit the accumulated lines (via eips, or stdout as a fallback).
  void flush() const;

  int count() const { return count_; }

 private:
  char lines_[kMaxRows][96];
  int count_ = 0;
};

#endif  // KINDLE_DASHBOARD_EIPS_H
