#ifndef KINDLE_DASHBOARD_JSON_PARSER_H
#define KINDLE_DASHBOARD_JSON_PARSER_H

#include <stddef.h>

#include "model.h"

// Minimal, allocation-light JSON reading tailored to the dashboard payload.
// The parsing helpers (findKeyInRange, matchingClose, extract*, ...) are
// file-local to json_parser.cpp; only the operations other modules need are
// exposed here.
namespace json {

// Parse the dashboard payload into `dashboard`. Returns 1 on success (the
// payload had "ok": true), 0 otherwise. Zero-initializes `dashboard` first.
int parseDashboard(const char* json, Dashboard* dashboard);

// Release any owned resources in `dashboard` (currently a no-op; the model is
// POD, but callers pair it with parseDashboard for future-proofing).
void freeDashboard(Dashboard* dashboard);

// Optimistically flip an item's "done" flag directly in the cached payload on
// disk. Returns 1 if the file was rewritten.
int patchCachedItemDone(const char* cache, const char* item_id, int done);

// Optimistically remove an item object from the cached payload on disk. Returns
// 1 if the file was rewritten. Splices out the object plus a single adjacent
// comma so the surrounding "items" array stays valid JSON.
int removeCachedItem(const char* cache, const char* item_id);

// Escape a string for embedding inside a JSON body.
void jsonEscapeString(const char* input, char* out, size_t out_size);

}  // namespace json

#endif  // KINDLE_DASHBOARD_JSON_PARSER_H
