#include "client.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "constants.h"
#include "json_parser.h"
#include "runtime.h"
#include "util.h"

using namespace util;

namespace {

struct EventWatcherArgs {
  char events_url[256];
  char read_token[160];
  int sleep_start_minute;
  int sleep_end_minute;
};

void* eventWatcherMain(void* raw) {
  EventWatcherArgs* args = static_cast<EventWatcherArgs*>(raw);
  if (!args || !args->events_url[0]) return NULL;
  if (!commandExists("curl")) {
    fprintf(stderr, "events=disabled missing_curl\n");
    free(args);
    return NULL;
  }

  char quoted_url[400];
  char quoted_header[260];
  shellQuote(args->events_url, quoted_url, sizeof(quoted_url));
  char header[200];
  header[0] = '\0';
  if (args->read_token[0]) {
    snprintf(header, sizeof(header), "X-Dashboard-Read-Token: %.150s", args->read_token);
    shellQuote(header, quoted_header, sizeof(quoted_header));
  } else {
    quoted_header[0] = '\0';
  }
  fprintf(stderr, "events=watching %s\n", args->events_url);

  while (g_running) {
    if (inSleepWindow(args->sleep_start_minute, args->sleep_end_minute)) {
      fprintf(stderr, "events=quiet\n");
      sleep(60);
      continue;
    }

    char command[900];
    snprintf(command, sizeof(command), "curl -fsSL --no-buffer --connect-timeout 20 --max-time 65 %s%s%s %s 2>/dev/null",
             quoted_header[0] ? "-H " : "",
             quoted_header[0] ? quoted_header : "",
             quoted_header[0] ? " " : "",
             quoted_url);
    FILE* stream = popen(command, "r");
    if (!stream) {
      fprintf(stderr, "events=popen_failed\n");
      sleep(10);
      continue;
    }

    char line_buffer[512];
    while (g_running && fgets(line_buffer, sizeof(line_buffer), stream)) {
      if (inSleepWindow(args->sleep_start_minute, args->sleep_end_minute)) {
        fprintf(stderr, "events=quiet close_stream=1\n");
        break;
      }
      if (strncmp(line_buffer, "event: planner", 14) == 0) {
        g_event_refresh = 1;
        fprintf(stderr, "events=planner refresh=1\n");
      } else if (strncmp(line_buffer, "event: planner-error", 20) == 0) {
        fprintf(stderr, "events=planner-error\n");
      }
    }

    const int status = pclose(stream);
    if (g_running) {
      fprintf(stderr, "events=reconnect status=%d\n", status);
      sleep(2);
    }
  }

  free(args);
  return NULL;
}

}  // namespace

int DashboardClient::fetchToCache(const char* url, const char* read_token, const char* cache) {
  const long long started = monotonicMs();
  char tmp[320];
  snprintf(tmp, sizeof(tmp), "%s.tmp", cache);
  char quoted_tmp[400];
  char quoted_url[400];
  char quoted_header[260];
  char command[1100];
  shellQuote(tmp, quoted_tmp, sizeof(quoted_tmp));
  shellQuote(url, quoted_url, sizeof(quoted_url));
  char header[200];
  header[0] = '\0';
  if (read_token && read_token[0]) {
    snprintf(header, sizeof(header), "X-Dashboard-Read-Token: %.150s", read_token);
    shellQuote(header, quoted_header, sizeof(quoted_header));
  } else {
    quoted_header[0] = '\0';
  }

  if (commandExists("curl")) {
    snprintf(command, sizeof(command), "curl -fsSL --connect-timeout 20 --max-time 55 --max-filesize %ld %s%s%s -o %s %s",
             kMaxDashboardPayloadBytes,
             quoted_header[0] ? "-H " : "",
             quoted_header[0] ? quoted_header : "",
             quoted_header[0] ? " " : "",
             quoted_tmp,
             quoted_url);
  } else if (commandExists("wget")) {
    snprintf(command, sizeof(command), "wget -q -T 55 %s%s%s -O %s %s",
             quoted_header[0] ? "--header=" : "",
             quoted_header[0] ? quoted_header : "",
             quoted_header[0] ? " " : "",
             quoted_tmp,
             quoted_url);
  } else {
    return 0;
  }

  if (system(command) != 0) {
    remove(tmp);
    fprintf(stderr, "timing=fetch ok=0 ms=%lld\n", monotonicMs() - started);
    return 0;
  }
  if (rename(tmp, cache) != 0) {
    remove(tmp);
    fprintf(stderr, "timing=fetch ok=0 ms=%lld\n", monotonicMs() - started);
    return 0;
  }
  char* payload_check = readFile(cache);
  if (!payload_check) {
    remove(cache);
    fprintf(stderr, "timing=fetch ok=0 oversized_or_unreadable ms=%lld\n", monotonicMs() - started);
    return 0;
  }
  free(payload_check);
  fprintf(stderr, "timing=fetch ok=1 ms=%lld\n", monotonicMs() - started);
  return 1;
}

int DashboardClient::postToggleItemAsync(const char* toggle_url, const char* toggle_token, const char* item_id, int done) {
  if (!toggle_url || !toggle_url[0] || !toggle_token || !toggle_token[0] || !item_id || !item_id[0]) {
    fprintf(stderr, "toggle=post-skipped missing_config id=%s\n", item_id ? item_id : "");
    return 0;
  }

  char escaped_id[120];
  json::jsonEscapeString(item_id, escaped_id, sizeof(escaped_id));
  char body[180];
  snprintf(body, sizeof(body), "{\"id\":\"%s\",\"done\":%s}", escaped_id, done ? "true" : "false");
  char header[200];
  snprintf(header, sizeof(header), "X-Dashboard-Toggle-Token: %.150s", toggle_token);

  char quoted_body[240];
  char quoted_header[260];
  char quoted_url[400];
  char command[900];
  shellQuote(body, quoted_body, sizeof(quoted_body));
  shellQuote(header, quoted_header, sizeof(quoted_header));
  shellQuote(toggle_url, quoted_url, sizeof(quoted_url));
  if (commandExists("curl")) {
    snprintf(command, sizeof(command),
             "curl -fsSL --connect-timeout 5 --max-time 12 -X POST -H 'Content-Type: application/json' -H %s -d %s %s >/dev/null 2>&1 &",
             quoted_header, quoted_body, quoted_url);
  } else if (commandExists("wget")) {
    snprintf(command, sizeof(command),
             "wget -q -T 12 --header='Content-Type: application/json' --header=%s --post-data=%s -O /dev/null %s >/dev/null 2>&1 &",
             quoted_header, quoted_body, quoted_url);
  } else {
    fprintf(stderr, "toggle=post-skipped missing_http_client id=%s\n", item_id);
    return 0;
  }

  const int status = system(command);
  if (status != 0) fprintf(stderr, "toggle=post-start-failed status=%d id=%s\n", status, item_id);
  else fprintf(stderr, "toggle=post-background id=%s done=%d\n", item_id, done);
  return status == 0;
}

int DashboardClient::postDeleteItemAsync(const char* delete_url, const char* toggle_token, const char* item_id) {
  if (!delete_url || !delete_url[0] || !toggle_token || !toggle_token[0] || !item_id || !item_id[0]) {
    fprintf(stderr, "delete=post-skipped missing_config id=%s\n", item_id ? item_id : "");
    return 0;
  }

  char escaped_id[120];
  json::jsonEscapeString(item_id, escaped_id, sizeof(escaped_id));
  char body[180];
  snprintf(body, sizeof(body), "{\"id\":\"%s\"}", escaped_id);
  // Reuse the toggle token as the shared write secret; the delete function
  // accepts the same X-Dashboard-Toggle-Token header.
  char header[200];
  snprintf(header, sizeof(header), "X-Dashboard-Toggle-Token: %.150s", toggle_token);

  char quoted_body[240];
  char quoted_header[260];
  char quoted_url[400];
  char command[900];
  shellQuote(body, quoted_body, sizeof(quoted_body));
  shellQuote(header, quoted_header, sizeof(quoted_header));
  shellQuote(delete_url, quoted_url, sizeof(quoted_url));
  if (commandExists("curl")) {
    snprintf(command, sizeof(command),
             "curl -fsSL --connect-timeout 5 --max-time 12 -X POST -H 'Content-Type: application/json' -H %s -d %s %s >/dev/null 2>&1 &",
             quoted_header, quoted_body, quoted_url);
  } else if (commandExists("wget")) {
    snprintf(command, sizeof(command),
             "wget -q -T 12 --header='Content-Type: application/json' --header=%s --post-data=%s -O /dev/null %s >/dev/null 2>&1 &",
             quoted_header, quoted_body, quoted_url);
  } else {
    fprintf(stderr, "delete=post-skipped missing_http_client id=%s\n", item_id);
    return 0;
  }

  const int status = system(command);
  if (status != 0) fprintf(stderr, "delete=post-start-failed status=%d id=%s\n", status, item_id);
  else fprintf(stderr, "delete=post-background id=%s\n", item_id);
  return status == 0;
}

void DashboardClient::startEventWatcher(const char* events_url, const char* read_token, int sleep_start_minute, int sleep_end_minute) {
  if (!events_url || !events_url[0]) {
    fprintf(stderr, "events=disabled empty_url\n");
    return;
  }

  EventWatcherArgs* args = static_cast<EventWatcherArgs*>(calloc(1, sizeof(EventWatcherArgs)));
  if (!args) {
    fprintf(stderr, "events=alloc_failed\n");
    return;
  }
  copyText(args->events_url, sizeof(args->events_url), events_url);
  if (read_token) copyText(args->read_token, sizeof(args->read_token), read_token);
  args->sleep_start_minute = sleep_start_minute;
  args->sleep_end_minute = sleep_end_minute;

  pthread_t thread;
  if (pthread_create(&thread, NULL, eventWatcherMain, args) != 0) {
    fprintf(stderr, "events=thread_failed\n");
    free(args);
    return;
  }
  pthread_detach(thread);
}
