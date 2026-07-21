#include "util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "constants.h"

namespace util {

long long monotonicMs() {
  timeval tv;
  gettimeofday(&tv, NULL);
  return static_cast<long long>(tv.tv_sec) * 1000LL + static_cast<long long>(tv.tv_usec / 1000);
}

void copyText(char* dest, size_t size, const char* source) {
  if (size == 0) return;
  if (!source) source = "";
  snprintf(dest, size, "%s", source);
}

char* readFile(const char* path) {
  FILE* file = fopen(path, "rb");
  if (!file) return NULL;
  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return NULL;
  }
  long size = ftell(file);
  if (size < 0 || size > kMaxDashboardPayloadBytes) {
    fclose(file);
    return NULL;
  }
  rewind(file);
  char* data = static_cast<char*>(calloc(static_cast<size_t>(size) + 1, 1));
  if (!data) {
    fclose(file);
    return NULL;
  }
  if (fread(data, 1, static_cast<size_t>(size), file) != static_cast<size_t>(size)) {
    free(data);
    fclose(file);
    return NULL;
  }
  fclose(file);
  return data;
}

int writeTextFileAtomic(const char* path, const char* data, size_t size) {
  if (!path || !path[0] || !data) return 0;
  char tmp[320];
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  FILE* file = fopen(tmp, "wb");
  if (!file) return 0;
  const int ok = fwrite(data, 1, size, file) == size;
  fclose(file);
  if (!ok) {
    remove(tmp);
    return 0;
  }
  if (rename(tmp, path) != 0) {
    remove(tmp);
    return 0;
  }
  return 1;
}

int commandExists(const char* command) {
  char probe[160];
  snprintf(probe, sizeof(probe), "command -v '%s' >/dev/null 2>&1", command);
  return system(probe) == 0;
}

void shellQuote(const char* text, char* out, size_t out_size) {
  size_t j = 0;
  if (j + 1 < out_size) out[j++] = '\'';
  for (size_t i = 0; text[i] && j + 5 < out_size; i++) {
    if (text[i] == '\'') {
      out[j++] = '\'';
      out[j++] = '\\';
      out[j++] = '\'';
      out[j++] = '\'';
    } else {
      out[j++] = text[i];
    }
  }
  if (j + 1 < out_size) out[j++] = '\'';
  out[j] = '\0';
}

int parseClockMinute(const char* text, int* minute) {
  int hour = -1;
  int min = -1;
  char tail = '\0';
  if (!text || sscanf(text, "%d:%d%c", &hour, &min, &tail) != 2) return 0;
  if (hour < 0 || hour > 23 || min < 0 || min > 59) return 0;
  *minute = hour * 60 + min;
  return 1;
}

int parseSleepWindow(const char* text, int* start_minute, int* end_minute) {
  if (!text || !text[0] || strcmp(text, "off") == 0 || strcmp(text, "none") == 0) {
    *start_minute = -1;
    *end_minute = -1;
    return 1;
  }

  const char* dash = strchr(text, '-');
  if (!dash) return 0;

  char start[8];
  char end[8];
  const size_t start_len = static_cast<size_t>(dash - text);
  if (start_len == 0 || start_len >= sizeof(start)) return 0;
  const size_t end_len = strlen(dash + 1);
  if (end_len == 0 || end_len >= sizeof(end)) return 0;

  memcpy(start, text, start_len);
  start[start_len] = '\0';
  memcpy(end, dash + 1, end_len + 1);
  return parseClockMinute(start, start_minute) && parseClockMinute(end, end_minute);
}

int currentLocalMinute() {
  time_t now = time(NULL);
  struct tm local_time;
#if defined(_POSIX_THREAD_SAFE_FUNCTIONS) || defined(__linux__)
  localtime_r(&now, &local_time);
#else
  struct tm* local_ptr = localtime(&now);
  if (!local_ptr) return 0;
  local_time = *local_ptr;
#endif
  return local_time.tm_hour * 60 + local_time.tm_min;
}

int inSleepWindow(int start_minute, int end_minute) {
  if (start_minute < 0 || end_minute < 0 || start_minute == end_minute) return 0;
  const int now = currentLocalMinute();
  if (start_minute < end_minute) return now >= start_minute && now < end_minute;
  return now >= start_minute || now < end_minute;
}

void upperCopy(char* dest, size_t size, const char* source) {
  copyText(dest, size, source);
  for (size_t i = 0; dest[i]; i++) dest[i] = static_cast<char>(toupper(static_cast<unsigned char>(dest[i])));
}

void formatDisplayDate(const char* iso, const char* status, char* out, size_t size) {
  static const char* months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  static const char* weekdays[] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};
  int year = 0;
  int month = 0;
  int day = 0;
  if (!iso || sscanf(iso, "%4d-%2d-%2d", &year, &month, &day) != 3 || month < 1 || month > 12 || day < 1 || day > 31) {
    snprintf(out, size, "UPDATED UNKNOWN // %s", status ? status : "UNKNOWN");
    return;
  }

  int y = year;
  int m = month;
  if (m < 3) {
    m += 12;
    y--;
  }
  const int k = y % 100;
  const int j = y / 100;
  const int h = (day + (13 * (m + 1)) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
  const int weekday = (h + 6) % 7;
  snprintf(out, size, "%s, %s %d // %s", weekdays[weekday], months[month - 1], day, status ? status : "UNKNOWN");
}

const char* displayListTitle(const List* list) {
  if (!list) return "";
  if (strcmp(list->key, "todo") == 0) return "TO DO";
  if (strcmp(list->key, "grocery") == 0) return "GROCERY";
  return list->title[0] ? list->title : list->key;
}

const char* displayListTitleForIndex(const List* list, int list_index) {
  if (list_index == 0) return "TO DO";
  if (list_index == 1) return "GROCERY";
  return displayListTitle(list);
}

}  // namespace util
