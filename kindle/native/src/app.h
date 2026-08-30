#ifndef KINDLE_DASHBOARD_APP_H
#define KINDLE_DASHBOARD_APP_H

#include "canvas.h"
#include "client.h"
#include "constants.h"
#include "framebuffer.h"
#include "model.h"
#include "navigator.h"
#include "panel.h"
#include "pgm_cache.h"
#include "touch_registry.h"

// Top-level application: owns every subsystem (navigator, touch registry, image
// cache, framebuffer renderer, network client) and the parsed options, and
// drives the fetch/render/wait main loop. main() is just `App().run(argc,argv)`.
class App {
 public:
  int run(int argc, char** argv);

 private:
  // Render the navigator's current view into `canvas` (plus the advent popup
  // overlay when active), recording the rendered dimensions for the input
  // thread.
  void drawCurrent(Canvas& canvas, const Dashboard& dashboard, const char* status);

  // Adapter so FramebufferRenderer::render can call back into drawCurrent.
  struct FrameDrawRequest {
    App* app;
    const Dashboard* dashboard;
    const char* status;
  };
  static void drawFrame(Canvas& canvas, void* data);

  // Recompute the advent overlay state (active day + door) from the current
  // date and the persisted state file. Called at the top of every render.
  void updateAdventOverlayState();

  int dumpBitmapPreview(const Dashboard* dashboard, const char* status, const char* path, int width, int height);
  void renderPayload(const char* payload, const char* status, const char* dump_pgm, const char* save_pgm, int dump_width, int dump_height);
  int renderCachedPayload(const char* status);

  // Sleep up to `seconds`, handling touches / events. Returns 1 to wake early.
  int waitForWakeEvent(int seconds, int allow_repaint);
  // Consume the pending tap and act on it. Returns 0 none, 1 handled, 2 refresh.
  int handlePendingTouch();

  RenderContext renderContext() {
    return RenderContext{touch_, pgm_cache_, options_.invert_images,
                         list_offset_, list_page_size_, list_item_count_,
                         cookbook_offset_, cookbook_page_size_, recipe_total_};
  }

  Options options_ = {};
  int last_screen_width_ = kBitmapFallbackWidth;
  int last_screen_height_ = kBitmapFallbackHeight;

  // Paginated view state. Persists across renders so an SSE refresh keeps the
  // user on their current page (clamped at render time); reset to 0 when a list
  // or the cookbook is opened fresh from the touch handler. The *_count fields
  // are written each render so the touch handler can wrap PREV/NEXT around the
  // first/last page.
  int list_offset_ = 0;
  int list_page_size_ = 8;
  int list_item_count_ = 0;
  int cookbook_offset_ = 0;
  int cookbook_page_size_ = 8;
  int recipe_total_ = 0;

  // Advent calendar popup overlay. advent_day_ > 0 means the popup is up for
  // that December day (-1 = inactive); advent_door_open_ flips when the door
  // is tapped. Deliberately NOT Navigator state: the overlay draws on top of
  // the current view and X simply stops drawing it.
  int advent_day_ = -1;
  int advent_door_open_ = 0;
  AdventPanel advent_panel_;

  TouchRegionRegistry touch_;
  PgmCache pgm_cache_;
  Navigator navigator_;
  FramebufferRenderer framebuffer_;
  DashboardClient client_;
  // Self-pipe: the input thread writes a byte to [1] on each resolved tap; the
  // main loop select()s on [0] so it wakes instantly instead of polling. {-1,-1}
  // when creation failed (degrades gracefully to periodic waiting).
  int wake_pipe_[2] = {-1, -1};
};

#endif  // KINDLE_DASHBOARD_APP_H
