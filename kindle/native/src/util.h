#ifndef KINDLE_DASHBOARD_UTIL_H
#define KINDLE_DASHBOARD_UTIL_H

#include <stddef.h>

#include "model.h"

// Stateless helpers shared across modules: timing, string/file I/O, shell
// quoting, sleep-window (quiet-hours) parsing, and small presentation helpers
// (upper-casing, date formatting, list-title resolution) used by both the
// bitmap panels and the text-mode fallback.
namespace util {

long long monotonicMs();
void copyText(char* dest, size_t size, const char* source);
char* readFile(const char* path);
int writeTextFileAtomic(const char* path, const char* data, size_t size);
void shellQuote(const char* text, char* out, size_t out_size);
int commandExists(const char* command);

int parseClockMinute(const char* text, int* minute);
int parseSleepWindow(const char* text, int* start_minute, int* end_minute);
int currentLocalMinute();
int inSleepWindow(int start_minute, int end_minute);

// Copy `source` into `dest`, upper-casing as it goes.
void upperCopy(char* dest, size_t size, const char* source);
// Format an ISO date + status into a human "WEEKDAY, MON D // STATUS" line.
void formatDisplayDate(const char* iso, const char* status, char* out, size_t size);
// Presentation title for a list (special-cases the todo/grocery keys).
const char* displayListTitle(const List* list);
const char* displayListTitleForIndex(const List* list, int list_index);

}  // namespace util

#endif  // KINDLE_DASHBOARD_UTIL_H
