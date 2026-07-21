#include "options.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"
#include "util.h"

using namespace util;

void initOptions(Options* options) {
  copyText(options->url, sizeof(options->url), kDefaultUrl);
  copyText(options->events_url, sizeof(options->events_url), kDefaultEventsUrl);
  copyText(options->toggle_url, sizeof(options->toggle_url), kDefaultToggleUrl);
  options->read_token[0] = '\0';
  options->toggle_token[0] = '\0';
  copyText(options->cache, sizeof(options->cache), kDefaultCache);
  options->render_only[0] = '\0';
  options->view[0] = '\0';
  options->dump_pgm[0] = '\0';
  options->save_pgm[0] = '\0';
  options->dump_width = kBitmapFallbackWidth;
  options->dump_height = kBitmapFallbackHeight;
  options->interval = kDefaultIntervalSeconds;
  parseSleepWindow(kDefaultSleepWindow, &options->sleep_start_minute, &options->sleep_end_minute);
  options->once = 0;
  options->invert_images = 0;
}

int parseOptions(int argc, char** argv, Options* options) {
  initOptions(options);
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--url") == 0 && i + 1 < argc) copyText(options->url, sizeof(options->url), argv[++i]);
    else if (strcmp(argv[i], "--events-url") == 0 && i + 1 < argc) copyText(options->events_url, sizeof(options->events_url), argv[++i]);
    else if (strcmp(argv[i], "--toggle-url") == 0 && i + 1 < argc) copyText(options->toggle_url, sizeof(options->toggle_url), argv[++i]);
    else if (strcmp(argv[i], "--read-token") == 0 && i + 1 < argc) copyText(options->read_token, sizeof(options->read_token), argv[++i]);
    else if (strcmp(argv[i], "--toggle-token") == 0 && i + 1 < argc) copyText(options->toggle_token, sizeof(options->toggle_token), argv[++i]);
    else if (strcmp(argv[i], "--cache") == 0 && i + 1 < argc) copyText(options->cache, sizeof(options->cache), argv[++i]);
    else if (strcmp(argv[i], "--interval") == 0 && i + 1 < argc) {
      options->interval = atoi(argv[++i]);
      if (options->interval < 5) options->interval = 5;
    } else if (strcmp(argv[i], "--sleep-window") == 0 && i + 1 < argc) {
      if (!parseSleepWindow(argv[++i], &options->sleep_start_minute, &options->sleep_end_minute)) {
        fprintf(stderr, "Invalid sleep window. Use HH:MM-HH:MM or off, for example 00:00-08:00.\n");
        return 0;
      }
    } else if (strcmp(argv[i], "--once") == 0) options->once = 1;
    else if (strcmp(argv[i], "--invert-images") == 0) {
      options->invert_images = 1;
    }
    else if (strcmp(argv[i], "--render") == 0 && i + 1 < argc) copyText(options->render_only, sizeof(options->render_only), argv[++i]);
    else if (strcmp(argv[i], "--view") == 0 && i + 1 < argc) copyText(options->view, sizeof(options->view), argv[++i]);
    else if (strcmp(argv[i], "--dump-pgm") == 0 && i + 1 < argc) copyText(options->dump_pgm, sizeof(options->dump_pgm), argv[++i]);
    else if (strcmp(argv[i], "--dump-size") == 0 && i + 1 < argc) {
      if (sscanf(argv[++i], "%dx%d", &options->dump_width, &options->dump_height) != 2 ||
          options->dump_width < 240 || options->dump_height < 320) {
        fprintf(stderr, "Invalid dump size. Use WIDTHxHEIGHT, for example 1072x1448.\n");
        return 0;
      }
    }
    else if (strcmp(argv[i], "--save-pgm") == 0 && i + 1 < argc) copyText(options->save_pgm, sizeof(options->save_pgm), argv[++i]);
    else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      printf("Usage: %s [--url URL] [--events-url URL] [--toggle-url URL] [--read-token TOKEN] [--toggle-token TOKEN] [--cache PATH] [--interval SECONDS] [--sleep-window HH:MM-HH:MM|off] [--once] [--invert-images]\n", argv[0]);
      printf("       %s --render PATH [--view cookbook|recipe|todo|grocery] [--dump-pgm PATH] [--dump-size WIDTHxHEIGHT] [--save-pgm PATH]\n", argv[0]);
      exit(0);
    } else {
      fprintf(stderr, "Unknown or incomplete argument: %s\n", argv[i]);
      return 0;
    }
  }
  return 1;
}
