#ifndef KINDLE_DASHBOARD_APP_H
#define KINDLE_DASHBOARD_APP_H

#include "canvas.h"
#include "client.h"
#include "constants.h"
#include "framebuffer.h"
#include "model.h"
#include "navigator.h"
#include "pgm_cache.h"
#include "touch_registry.h"

// Top-level application: owns every subsystem (navigator, touch registry, image
// cache, framebuffer renderer, network client) and the parsed options, and
// drives the fetch/render/wait main loop. main() is just `App().run(argc,argv)`.
class App {
 public:
  int run(int argc, char** argv);

 private:
  // Render the navigator's current view into `canvas`, recording the rendered
  // dimensions for the input thread.
  void drawCurrent(Canvas& canvas, const Dashboard& dashboard, const char* status);

  int dumpBitmapPreview(const Dashboard* dashboard, const char* status, const char* path, int width, int height);
  void renderPayload(const char* payload, const char* status, const char* dump_pgm, const char* save_pgm, int dump_width, int dump_height);
  int renderCachedPayload(const char* status);

  // Sleep up to `seconds`, handling touches / events. Returns 1 to wake early.
  int waitForWakeEvent(int seconds, int allow_repaint);
  // Consume the pending tap and act on it. Returns 0 none, 1 handled, 2 refresh.
  int handlePendingTouch();

  RenderContext renderContext() { return RenderContext{touch_, pgm_cache_, options_.invert_images}; }

  Options options_ = {};
  int last_screen_width_ = kBitmapFallbackWidth;
  int last_screen_height_ = kBitmapFallbackHeight;

  TouchRegionRegistry touch_;
  PgmCache pgm_cache_;
  Navigator navigator_;
  FramebufferRenderer framebuffer_;
  DashboardClient client_;
};

#endif  // KINDLE_DASHBOARD_APP_H
