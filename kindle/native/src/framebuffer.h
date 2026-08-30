#ifndef KINDLE_DASHBOARD_FRAMEBUFFER_H
#define KINDLE_DASHBOARD_FRAMEBUFFER_H

#include "canvas.h"
#include "model.h"

// Write a Canvas out as a binary (P5) PGM file. Platform-agnostic.
int writePgm(const char* path, const Canvas* canvas);

// Callback that paints a full view into a canvas. Lets the App compose the
// navigator render with app-level overlays (advent popup) inside both the
// framebuffer and the PGM dump paths without FramebufferRenderer knowing
// about them. `data` is caller-owned draw state.
typedef void (*CanvasDrawFn)(Canvas& canvas, void* data);

// Presents rendered views on the Kindle's e-ink framebuffer and provides the
// tap-feedback flash and "return home" affordances. All device access is
// behind #ifdef __linux__ in the .cpp; on non-Linux hosts render() reports
// unavailable and the flash is a no-op, so the render-only/local paths still
// run.
class FramebufferRenderer {
 public:
  // Size a canvas to the framebuffer, invoke `draw` to render the current
  // view into it, optionally save a PGM, and blit it to /dev/fb0. Updates
  // last_width / last_height (the shared last-rendered dimensions read by the
  // input thread). Returns 1 on success.
  int render(CanvasDrawFn draw, void* draw_data, const char* save_pgm, int& last_width, int& last_height);

  // Briefly invert a rectangle as visual feedback for a tap.
  void flashRect(Rect rect);

  // Flash feedback for a resolved tap (given the pending rect captured at tap).
  void showTouchVisualFeedback(TouchAction action, int x, int y, int pending_rect_valid, Rect pending_rect);

  // Ask the Kindle framework to return to the home screen.
  void returnToKindleHome();
};

#endif  // KINDLE_DASHBOARD_FRAMEBUFFER_H
