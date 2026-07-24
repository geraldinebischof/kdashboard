#ifndef KINDLE_DASHBOARD_CLIENT_H
#define KINDLE_DASHBOARD_CLIENT_H

// Talks to the dashboard backend by shelling out to curl/wget: fetches the
// dashboard JSON into the on-disk cache, posts optimistic item-toggle updates,
// and runs a background SSE watcher that flips g_event_refresh when the planner
// signals a change.
class DashboardClient {
 public:
  // Fetch `url` into `cache` atomically. Returns 1 on success.
  int fetchToCache(const char* url, const char* read_token, const char* cache);

  // Fire-and-forget POST of an item's done state. Returns 1 if the background
  // request was launched.
  int postToggleItemAsync(const char* toggle_url, const char* toggle_token, const char* item_id, int done);

  // Fire-and-forget POST to delete an item by id. Returns 1 if the background
  // request was launched. Reuses the toggle URL + token (single shared secret).
  int postDeleteItemAsync(const char* toggle_url, const char* toggle_token, const char* item_id);

  // Spawn the detached SSE watcher thread (no-op if events_url is empty).
  void startEventWatcher(const char* events_url, const char* read_token, int sleep_start_minute, int sleep_end_minute);
};

#endif  // KINDLE_DASHBOARD_CLIENT_H
