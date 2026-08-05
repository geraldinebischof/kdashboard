#include "app.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "eips.h"
#include "input.h"
#include "json_parser.h"
#include "options.h"
#include "panel.h"
#include "runtime.h"
#include "util.h"

using namespace json;
using namespace util;

namespace {

void addList(EipsRenderer& eips, const List* list) {
  eips.addSectionTitle(displayListTitle(list));

  if (list->item_count == 0) {
    eips.addCardText(" [ ] Empty");
    eips.addRule();
    return;
  }

  const int shown = list->item_count > 4 ? 4 : list->item_count;
  for (int i = 0; i < shown; i++) {
    char line[96];
    snprintf(line, sizeof(line), " %s %.30s", list->items[i].done ? "[x]" : "[ ]", list->items[i].text);
    eips.addCardText(line);
  }
  if (list->item_count > shown) {
    char more[48];
    snprintf(more, sizeof(more), " ... +%d more", list->item_count - shown);
    eips.addCardText(more);
  }
  eips.addRule();
}

void renderLines(const Dashboard* dashboard, const char* status, EipsRenderer& eips) {
  eips.reset();
  eips.addRule();
  eips.addCardText(" KINDLE DASHBOARD");
  char sync[64];
  snprintf(sync, sizeof(sync), " Sync %.16s", dashboard->generated_at[0] ? dashboard->generated_at : "unknown");
  eips.addCardText(sync);
  char mode[64];
  snprintf(mode, sizeof(mode), " Mode %s", status);
  eips.addCardText(mode);
  eips.addRule();
  for (int i = 0; i < dashboard->list_count; i++) addList(eips, &dashboard->lists[i]);
  eips.addCardText(" Telegram keeps lists in sync");
  eips.addRule();
}

void buildDashboardUrl(const char* base_url, char* out, size_t out_size) {
  if (!out || out_size == 0) return;
  if (!base_url) base_url = "";
  snprintf(out, out_size, "%s", base_url);
}

int renderViaFbink(const Dashboard* dashboard, const char* status, const char* save_pgm) {
  (void)dashboard;
  (void)status;
  (void)save_pgm;
  fprintf(stderr, "render=fbink skipped preserve_status_bar\n");
  return 0;
}

int shouldRepaintCachedTick(int tick) {
  return tick == 5;
}

// Circular page wrapping used by the PREV/NEXT touch handlers. NEXT past the
// last page wraps to the first (offset 0); PREV before the first page wraps to
// the start of the last page. The final offset is still snapped/clamped at
// render time, so a stale count between renders degrades to the nearest page
// instead of going out of bounds.
int wrapNextOffset(int offset, int page_size, int count) {
  return (page_size > 0 && count > 0 && offset >= count) ? 0 : offset;
}

int wrapPrevOffset(int offset, int page_size, int count) {
  if (offset >= 0) return offset;
  if (page_size <= 0 || count <= 0) return 0;
  const int pages = (count + page_size - 1) / page_size;
  return (pages - 1) * page_size;
}

}  // namespace

void App::drawCurrent(Canvas& canvas, const Dashboard& dashboard, const char* status) {
  last_screen_width_ = canvas.width;
  last_screen_height_ = canvas.height;
  RenderContext ctx = renderContext();
  navigator_.render(canvas, dashboard, status, ctx);
}

int App::dumpBitmapPreview(const Dashboard* dashboard, const char* status, const char* path, int width, int height) {
  if (!path || !path[0]) return 0;
  Canvas canvas;
  canvas.width = width > 0 ? width : kBitmapFallbackWidth;
  canvas.height = height > 0 ? height : kBitmapFallbackHeight;
  canvas.pixels = static_cast<unsigned char*>(calloc(static_cast<size_t>(canvas.width) * static_cast<size_t>(canvas.height), 1));
  if (!canvas.pixels) return 0;
  drawCurrent(canvas, *dashboard, status);
  const int ok = writePgm(path, &canvas);
  free(canvas.pixels);
  return ok;
}

void App::renderPayload(const char* payload, const char* status, const char* dump_pgm, const char* save_pgm, int dump_width, int dump_height) {
  const long long started = monotonicMs();
  Dashboard dashboard;
  if (!parseDashboard(payload, &dashboard)) {
    EipsRenderer eips;
    eips.addRule();
    eips.addCardText(" KINDLE DASHBOARD");
    eips.addCardText(" Dashboard unavailable");
    eips.addCardText(" Could not parse dashboard data");
    eips.addRule();
    eips.flush();
    fprintf(stderr, "timing=render status=parse_failed ms=%lld\n", monotonicMs() - started);
    return;
  }
  if (dump_pgm && dump_pgm[0]) {
    dumpBitmapPreview(&dashboard, status, dump_pgm, dump_width, dump_height);
    fprintf(stderr, "render=pgm %s width=%d height=%d\n", dump_pgm, dump_width > 0 ? dump_width : kBitmapFallbackWidth, dump_height > 0 ? dump_height : kBitmapFallbackHeight);
    freeDashboard(&dashboard);
    fprintf(stderr, "timing=render status=dump ms=%lld\n", monotonicMs() - started);
    return;
  }
  if (renderViaFbink(&dashboard, status, save_pgm)) {
    freeDashboard(&dashboard);
    fprintf(stderr, "timing=render status=fbink ms=%lld\n", monotonicMs() - started);
    return;
  }
  RenderContext framebuffer_ctx = renderContext();
  // Mark the input thread busy for the duration of the framebuffer write +
  // e-ink refresh so a rapid follow-up tap can't stack a second flash+refresh
  // on top of this one (which wedges the e-ink controller).
  touch_.busy = 1;
  const int fb_ok = framebuffer_.render(navigator_, dashboard, status, framebuffer_ctx, save_pgm, last_screen_width_, last_screen_height_);
  touch_.busy = 0;
  if (fb_ok) {
    freeDashboard(&dashboard);
    fprintf(stderr, "timing=render status=framebuffer ms=%lld\n", monotonicMs() - started);
    return;
  }
  if (save_pgm && save_pgm[0]) {
    dumpBitmapPreview(&dashboard, status, save_pgm, kBitmapFallbackWidth, kBitmapFallbackHeight);
    fprintf(stderr, "render=save-pgm %s width=%d height=%d fallback=1\n", save_pgm, kBitmapFallbackWidth, kBitmapFallbackHeight);
  }
  if (getenv("KINDLE_DASHBOARD_TEXT_FALLBACK") == NULL) {
    fprintf(stderr, "render=bitmap unavailable text_fallback=disabled\n");
    freeDashboard(&dashboard);
    fprintf(stderr, "timing=render status=bitmap_unavailable ms=%lld\n", monotonicMs() - started);
    return;
  }
  fprintf(stderr, "render=eips fallback\n");
  EipsRenderer eips;
  renderLines(&dashboard, status, eips);
  eips.flush();
  freeDashboard(&dashboard);
  fprintf(stderr, "timing=render status=eips ms=%lld\n", monotonicMs() - started);
}

int App::renderCachedPayload(const char* status) {
  char* payload = readFile(options_.cache);
  if (!payload) {
    fprintf(stderr, "render=cache-miss path=%s\n", options_.cache);
    return 0;
  }
  renderPayload(payload, status, options_.dump_pgm, options_.save_pgm, options_.dump_width, options_.dump_height);
  free(payload);
  return 1;
}

int App::handlePendingTouch() {
  const TouchAction action = touch_.pending_action;
  const int touch_x = touch_.pending_touch_x;
  const int touch_y = touch_.pending_touch_y;
  touch_.pending_action = kTouchNone;
  touch_.pending_touch_x = -1;
  touch_.pending_touch_y = -1;
  framebuffer_.showTouchVisualFeedback(action, touch_x, touch_y, touch_.pending_touch_rect_valid, touch_.pending_touch_rect);
  touch_.pending_touch_rect_valid = 0;

  if (action == kTouchExit) {
    fprintf(stderr, "touch=exit\n");
    framebuffer_.returnToKindleHome();
    g_running = 0;
    return 1;
  }

  if (action == kTouchBack) {
    fprintf(stderr, "touch=back\n");
    navigator_.goBack();
    return 1;
  }

  if (action == kTouchHome) {
    fprintf(stderr, "touch=home\n");
    navigator_.goHome();
    return 1;
  }

  if (action == kTouchOpenList) {
    fprintf(stderr, "touch=open-list index=%d\n", touch_.pending_list_index);
    list_offset_ = 0;  // opening a list fresh always starts on page 1
    navigator_.openList(touch_.pending_list_index);
    return 1;
  }

  if (action == kTouchOpenRecipe) {
    fprintf(stderr, "touch=open-recipe index=%d\n", touch_.pending_recipe_index);
    navigator_.openRecipe(touch_.pending_recipe_index);
    return 1;
  }

  if (action == kTouchOpenRecipes) {
    fprintf(stderr, "touch=open-recipes\n");
    cookbook_offset_ = 0;  // opening the cookbook fresh always starts on page 1
    navigator_.openRecipes();
    return 1;
  }

  if (action == kTouchListPrevPage) {
    list_offset_ = wrapPrevOffset(list_offset_ - list_page_size_, list_page_size_, list_item_count_);
    fprintf(stderr, "touch=list-prev offset=%d\n", list_offset_);
    return 1;
  }
  if (action == kTouchListNextPage) {
    list_offset_ = wrapNextOffset(list_offset_ + list_page_size_, list_page_size_, list_item_count_);
    fprintf(stderr, "touch=list-next offset=%d\n", list_offset_);
    return 1;
  }
  if (action == kTouchCookbookPrevPage) {
    cookbook_offset_ = wrapPrevOffset(cookbook_offset_ - cookbook_page_size_, cookbook_page_size_, recipe_total_);
    fprintf(stderr, "touch=cookbook-prev offset=%d\n", cookbook_offset_);
    return 1;
  }
  if (action == kTouchCookbookNextPage) {
    cookbook_offset_ = wrapNextOffset(cookbook_offset_ + cookbook_page_size_, cookbook_page_size_, recipe_total_);
    fprintf(stderr, "touch=cookbook-next offset=%d\n", cookbook_offset_);
    return 1;
  }

  if (action == kTouchToggleItem) {
    const int next_done = touch_.pending_item_done ? 0 : 1;
    fprintf(stderr, "touch=toggle-list-item id=%s done=%d\n", touch_.pending_item_id, next_done);
    patchCachedItemDone(options_.cache, touch_.pending_item_id, next_done);
    client_.postToggleItemAsync(options_.toggle_url, options_.toggle_token, touch_.pending_item_id, next_done);
    return 1;
  }

  if (action == kTouchDeleteItem) {
    fprintf(stderr, "touch=delete-list-item-request index=%d id=%s\n", touch_.pending_item_index, touch_.pending_item_id);
    navigator_.requestDeleteItem(touch_.pending_item_index);
    return 1;
  }

  if (action == kTouchCancelDelete) {
    fprintf(stderr, "touch=delete-list-item-cancel id=%s\n", touch_.pending_item_id);
    navigator_.cancelDeleteItem();
    return 1;
  }

  if (action == kTouchConfirmDelete) {
    fprintf(stderr, "touch=delete-list-item-confirm id=%s\n", touch_.pending_item_id);
    navigator_.cancelDeleteItem();
    removeCachedItem(options_.cache, touch_.pending_item_id);
    // Do NOT fall back to the toggle URL when the delete URL is unset: the
    // toggle endpoint only flips "done" and never removes the row, so a delete
    // posted there would be silently dropped and the item would reappear on the
    // next backend refresh. postDeleteItemAsync logs a "post-skipped
    // missing_config" warning when the URL is empty, which is the correct
    // behavior until the delete endpoint is configured.
    client_.postDeleteItemAsync(options_.delete_url, options_.toggle_token, touch_.pending_item_id);
    return 1;
  }

  return 0;
}

int App::waitForWakeEvent(int seconds, int allow_repaint) {
  for (int elapsed = 1; elapsed <= seconds && g_running; elapsed++) {
    if (touch_.pending_action != kTouchNone) {
      const int touch_result = handlePendingTouch();
      if (!g_running) return 0;
      if (touch_result == 2) return 1;
      if (touch_result == 1) renderCachedPayload("cached/local");
      continue;
    }
    if (g_event_refresh) {
      fprintf(stderr, "events=refresh_now\n");
      return 1;
    }
    if (allow_repaint && shouldRepaintCachedTick(elapsed)) {
      fprintf(stderr, "render=repaint tick=%d\n", elapsed);
      renderCachedPayload("cached/local");
    }
    sleep(1);
  }
  return 1;
}

int App::run(int argc, char** argv) {
  if (!parseOptions(argc, argv, &options_)) return 1;

  if (options_.render_only[0]) {
    navigator_.applyInitialView(options_.view);
    char* payload = readFile(options_.render_only);
    if (!payload) {
      fprintf(stderr, "Could not read %s\n", options_.render_only);
      return 1;
    }
    renderPayload(payload, "fixture", options_.dump_pgm, options_.save_pgm, options_.dump_width, options_.dump_height);
    free(payload);
    return 0;
  }

  signal(SIGINT, handleSignal);
  signal(SIGTERM, handleSignal);
  navigator_.applyInitialView(options_.view);

  InputManager input(touch_, last_screen_width_, last_screen_height_);
  input.open();
  input.startWatcher();
  if (!options_.once && !options_.render_only[0]) {
    client_.startEventWatcher(options_.events_url, options_.read_token, options_.sleep_start_minute, options_.sleep_end_minute);
  }

  while (g_running) {
    int pending_result = 0;
    if (touch_.pending_action != kTouchNone) pending_result = handlePendingTouch();
    if (!g_running) break;
    if (pending_result == 1 && renderCachedPayload("cached/local")) {
      g_event_refresh = 0;
      if (options_.once) break;
      for (int remaining = options_.interval; remaining > 0 && g_running;) {
        if (inSleepWindow(options_.sleep_start_minute, options_.sleep_end_minute)) break;
        const int chunk = remaining > 60 ? 60 : remaining;
        waitForWakeEvent(chunk, remaining == options_.interval);
        if (g_event_refresh) break;
        remaining -= chunk;
      }
      continue;
    }
    g_event_refresh = 0;

    if (!options_.once && inSleepWindow(options_.sleep_start_minute, options_.sleep_end_minute)) {
      fprintf(stderr, "power=quiet sleep_window=1\n");
      if (!renderCachedPayload("sleep/quiet")) {
        EipsRenderer eips;
        eips.addRule();
        eips.addCardText(" KINDLE DASHBOARD");
        eips.addCardText(" Quiet hours");
        eips.addCardText(" Cache unavailable");
        eips.addRule();
        eips.flush();
      }
      while (g_running && inSleepWindow(options_.sleep_start_minute, options_.sleep_end_minute)) {
        waitForWakeEvent(60, 0);
        if (g_manual_fetch_refresh) break;
        g_event_refresh = 0;
      }
      if (!g_manual_fetch_refresh) continue;
      fprintf(stderr, "power=quiet manual_fetch=1\n");
      g_manual_fetch_refresh = 0;
      g_event_refresh = 0;
    }

    if (g_manual_fetch_refresh) {
      fprintf(stderr, "events=manual-fetch\n");
      g_manual_fetch_refresh = 0;
    }
    char dashboard_url[320];
    buildDashboardUrl(options_.url, dashboard_url, sizeof(dashboard_url));
    const int fetched = client_.fetchToCache(dashboard_url, options_.read_token, options_.cache);
    if (!renderCachedPayload(fetched ? "live" : "cached/offline")) {
      EipsRenderer eips;
      eips.addRule();
      eips.addCardText(" KINDLE DASHBOARD");
      eips.addCardText(" Dashboard unavailable");
      eips.addCardText(" Check Wi-Fi or try again later");
      eips.addRule();
      eips.flush();
    }

    if (options_.once) break;
    for (int remaining = options_.interval; remaining > 0 && g_running;) {
      if (inSleepWindow(options_.sleep_start_minute, options_.sleep_end_minute)) break;
      const int chunk = remaining > 60 ? 60 : remaining;
      waitForWakeEvent(chunk, remaining == options_.interval);
      if (g_event_refresh) break;
      remaining -= chunk;
    }
  }

  input.close();
  pgm_cache_.clear();
  return 0;
}
