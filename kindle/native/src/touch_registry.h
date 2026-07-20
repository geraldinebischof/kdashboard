#ifndef KINDLE_DASHBOARD_TOUCH_REGISTRY_H
#define KINDLE_DASHBOARD_TOUCH_REGISTRY_H

#include "model.h"

// One tappable rectangle registered during a render, plus the payload the
// action needs (which list/recipe/item it refers to).
struct TouchRegion {
  Rect rect;
  TouchAction action;
  int list_index;
  int item_index;
  char item_id[48];
  int item_done;
};

// Collects the tappable regions produced while rendering the current view and
// resolves a tap coordinate to a pending action. A single instance is shared
// between the render/main thread (which populates regions via add()/clear() and
// consumes the pending_* fields) and the touch-input thread (which resolves
// taps via applyTouchAt()). The pending_* fields carry the same benign racy
// hand-off semantics as the original file-scope globals — no locking, so the
// input thread never stalls.
class TouchRegionRegistry {
 public:
  void clear();
  void add(Rect rect, TouchAction action, int list_index, int item_index, const char* item_id, int item_done);

  // Resolve a tap at (x, y) against the registered regions (most-recently-added
  // first). On a hit, records the pending action/indices/rect and returns 1.
  int applyTouchAt(int x, int y);

  // Record the rectangle to flash as visual feedback for the pending tap.
  void setPendingRect(Rect rect);

  int count() const { return count_; }

  // Pending tap hand-off: written when a tap resolves, read and cleared by the
  // main loop's handlePendingTouch.
  TouchAction pending_action = kTouchNone;
  int pending_list_index = -1;
  int pending_recipe_index = -1;
  char pending_item_id[48] = {};
  int pending_item_done = 0;
  int pending_touch_x = -1;
  int pending_touch_y = -1;
  Rect pending_touch_rect = {0, 0, 0, 0};
  int pending_touch_rect_valid = 0;

 private:
  static constexpr int kMaxTouchRegions = 32;
  TouchRegion regions_[kMaxTouchRegions] = {};
  int count_ = 0;
};

#endif  // KINDLE_DASHBOARD_TOUCH_REGISTRY_H
