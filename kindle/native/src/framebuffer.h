#ifndef KINDLE_DASHBOARD_FRAMEBUFFER_H
#define KINDLE_DASHBOARD_FRAMEBUFFER_H

#include "canvas.h"
#include "model.h"
#include "navigator.h"
#include "panel.h"  // RenderContext

// Write a Canvas out as a binary (P5) PGM file. Platform-agnostic.
int writePgm(const char* path, const Canvas* canvas);

// Presents rendered views on the Kindle's e-ink framebuffer and provides the
// tap-feedback flash and "return home" affordances. All device access is
// behind #ifdef __linux__ in the .cpp; on non-Linux hosts render() reports
// unavailable and the flash is a no-op, so the render-only/local paths still
// run.
class FramebufferRenderer {
 public:
  // Size a canvas to the framebuffer, render the navigator's current view into
  // it, optionally save a PGM, and blit it to /dev/fb0. Updates last_width /
  // last_height (the shared last-rendered dimensions read by the input thread).
  // Returns 1 on success.
  int render(Navigator& navigator, const Dashboard& dashboard, const char* status,
             RenderContext& ctx, const char* save_pgm, int& last_width, int& last_height);

  // Briefly invert a rectangle as visual feedback for a tap.
  void flashRect(Rect rect);

  // Flash feedback for a resolved tap (given the pending rect captured at tap).
  void showTouchVisualFeedback(TouchAction action, int x, int y, int pending_rect_valid, Rect pending_rect);

  // Ask the Kindle framework to return to the home screen.
  void returnToKindleHome();
};

#endif  // KINDLE_DASHBOARD_FRAMEBUFFER_H
