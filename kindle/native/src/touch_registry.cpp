#include "touch_registry.h"

#include <string.h>

#include "util.h"

namespace {

int containsPoint(const Rect* rect, int x, int y) {
  return rect && x >= rect->x && y >= rect->y && x < rect->x + rect->w && y < rect->y + rect->h;
}

}  // namespace

void TouchRegionRegistry::clear() {
  memset(regions_, 0, sizeof(regions_));
  count_ = 0;
}

void TouchRegionRegistry::setPendingRect(Rect rect) {
  pending_touch_rect = rect;
  pending_touch_rect_valid = rect.w > 0 && rect.h > 0 ? 1 : 0;
}

void TouchRegionRegistry::add(Rect rect, TouchAction action, int list_index, int item_index, const char* item_id, int item_done) {
  if (count_ >= kMaxTouchRegions) return;
  TouchRegion* region = &regions_[count_++];
  region->rect = rect;
  region->action = action;
  region->list_index = list_index;
  region->item_index = item_index;
  util::copyText(region->item_id, sizeof(region->item_id), item_id ? item_id : "");
  region->item_done = item_done;
}

int TouchRegionRegistry::applyTouchAt(int x, int y) {
  for (int i = count_ - 1; i >= 0; i--) {
    TouchRegion* region = &regions_[i];
    if (!containsPoint(&region->rect, x, y)) continue;
    pending_action = region->action;
    pending_list_index = region->list_index;
    pending_recipe_index = region->item_index;
    util::copyText(pending_item_id, sizeof(pending_item_id), region->item_id);
    pending_item_done = region->item_done;
    setPendingRect(region->rect);
    return 1;
  }
  return 0;
}
