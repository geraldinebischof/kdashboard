#include "json_parser.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"
#include "util.h"

namespace json {

using namespace util;

namespace {

const char* skipWhitespace(const char* cursor) {
  while (cursor && *cursor && isspace(static_cast<unsigned char>(*cursor))) cursor++;
  return cursor;
}

const char* findKeyInRange(const char* start, const char* end, const char* key) {
  char pattern[80];
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  const size_t pattern_len = strlen(pattern);
  const char* cursor = start;
  while (cursor && (!end || cursor + pattern_len <= end)) {
    cursor = strstr(cursor, pattern);
    if (!cursor || (end && cursor + pattern_len > end)) return NULL;
    const char* colon = skipWhitespace(cursor + pattern_len);
    if (*colon == ':') return skipWhitespace(colon + 1);
    cursor += pattern_len;
  }
  return NULL;
}

int parseJsonString(const char* cursor, char* out, size_t out_size, const char** after) {
  cursor = skipWhitespace(cursor);
  if (!cursor || *cursor != '"') return 0;
  cursor++;
  size_t length = 0;
  while (*cursor && *cursor != '"') {
    char ch = *cursor++;
    if (ch == '\\') {
      ch = *cursor++;
      if (ch == 'n') ch = ' ';
      else if (ch == 'r') ch = ' ';
      else if (ch == 't') ch = ' ';
      else if (ch == 'u') {
        ch = '?';
        for (int i = 0; i < 4 && *cursor; i++) cursor++;
      }
    }
    if (length + 1 < out_size) out[length++] = ch;
  }
  if (*cursor != '"') return 0;
  if (out_size > 0) out[length] = '\0';
  if (after) *after = cursor + 1;
  return 1;
}

int extractString(const char* start, const char* end, const char* key, char* out, size_t out_size, const char* fallback) {
  const char* value = findKeyInRange(start, end, key);
  if (value && strncmp(value, "null", 4) == 0) value = NULL;
  if (value && parseJsonString(value, out, out_size, NULL)) return 1;
  copyText(out, out_size, fallback);
  return 0;
}

int extractInt(const char* start, const char* end, const char* key, int fallback) {
  const char* value = findKeyInRange(start, end, key);
  if (!value) return fallback;
  return static_cast<int>(strtol(value, NULL, 10));
}

int extractScaledInt(const char* start, const char* end, const char* key, int scale, int fallback) {
  const char* value = findKeyInRange(start, end, key);
  if (!value) return fallback;
  const double parsed = strtod(value, NULL);
  return static_cast<int>(parsed * scale + (parsed >= 0 ? 0.5 : -0.5));
}

int extractBool(const char* start, const char* end, const char* key, int fallback) {
  const char* value = findKeyInRange(start, end, key);
  if (!value) return fallback;
  if (strncmp(value, "true", 4) == 0) return 1;
  if (strncmp(value, "false", 5) == 0) return 0;
  return fallback;
}

const char* matchingClose(const char* open, char close_char) {
  const char open_char = *open;
  int depth = 0;
  int in_string = 0;
  int escaped = 0;
  for (const char* cursor = open; *cursor; cursor++) {
    const char ch = *cursor;
    if (in_string) {
      if (escaped) escaped = 0;
      else if (ch == '\\') escaped = 1;
      else if (ch == '"') in_string = 0;
      continue;
    }
    if (ch == '"') {
      in_string = 1;
      continue;
    }
    if (ch == open_char) depth++;
    else if (ch == close_char) {
      depth--;
      if (depth == 0) return cursor;
    }
  }
  return NULL;
}

int parseItems(const char* list_start, const char* list_end, List* list) {
  const char* items_value = findKeyInRange(list_start, list_end, "items");
  if (!items_value || *items_value != '[') return 0;
  const char* items_end = matchingClose(items_value, ']');
  if (!items_end || items_end > list_end) return 0;

  const char* cursor = items_value + 1;
  while (cursor < items_end && list->item_count < kMaxItems) {
    const char* object_start = strchr(cursor, '{');
    if (!object_start || object_start >= items_end) break;
    const char* object_end = matchingClose(object_start, '}');
    if (!object_end || object_end > items_end) break;

    Item* item = &list->items[list->item_count];
    extractString(object_start, object_end, "id", item->id, sizeof(item->id), "");
    extractString(object_start, object_end, "text", item->text, sizeof(item->text), "");
    item->done = extractBool(object_start, object_end, "done", 0);
    if (item->text[0]) list->item_count++;
    cursor = object_end + 1;
  }
  return 1;
}

int parseRecipeIngredients(const char* recipe_start, const char* recipe_end, RecipeRecord* recipe) {
  const char* ingredients_value = findKeyInRange(recipe_start, recipe_end, "ingredients");
  if (!ingredients_value || *ingredients_value != '[') return 0;
  const char* ingredients_end = matchingClose(ingredients_value, ']');
  if (!ingredients_end || ingredients_end > recipe_end) return 0;

  const char* cursor = ingredients_value + 1;
  while (cursor < ingredients_end && recipe->ingredient_count < kMaxRecipeIngredients) {
    const char* object_start = strchr(cursor, '{');
    if (!object_start || object_start >= ingredients_end) break;
    const char* object_end = matchingClose(object_start, '}');
    if (!object_end || object_end > ingredients_end) break;

    RecipeIngredientRecord* ingredient = &recipe->ingredients[recipe->ingredient_count];
    extractString(object_start, object_end, "name", ingredient->name, sizeof(ingredient->name), "");
    extractString(object_start, object_end, "amount", ingredient->amount, sizeof(ingredient->amount), "");
    if (ingredient->name[0] || ingredient->amount[0]) recipe->ingredient_count++;
    cursor = object_end + 1;
  }
  return 1;
}

int parseRecipes(const char* json, Dashboard* dashboard) {
  const char* recipes_value = findKeyInRange(json, NULL, "recipes");
  if (!recipes_value || *recipes_value != '[') return 0;
  const char* recipes_end = matchingClose(recipes_value, ']');
  if (!recipes_end) return 0;

  const char* cursor = recipes_value + 1;
  while (cursor < recipes_end && dashboard->recipe_count < kMaxRecipes) {
    const char* object_start = strchr(cursor, '{');
    if (!object_start || object_start >= recipes_end) break;
    const char* object_end = matchingClose(object_start, '}');
    if (!object_end || object_end > recipes_end) break;

    RecipeRecord* recipe = &dashboard->recipes[dashboard->recipe_count];
    extractString(object_start, object_end, "id", recipe->id, sizeof(recipe->id), "");
    extractString(object_start, object_end, "title", recipe->title, sizeof(recipe->title), "");
    extractString(object_start, object_end, "instructions", recipe->instructions, sizeof(recipe->instructions), "");
    recipe->calories = extractInt(object_start, object_end, "total_calories", 0);
    recipe->carbs = extractInt(object_start, object_end, "carbs_g", 0);
    recipe->fat = extractInt(object_start, object_end, "fat_g", 0);
    recipe->protein = extractInt(object_start, object_end, "protein_g", 0);
    recipe->rating_tenths = extractScaledInt(object_start, object_end, "rating", 10, 0);
    parseRecipeIngredients(object_start, object_end, recipe);
    if (recipe->title[0]) dashboard->recipe_count++;
    cursor = object_end + 1;
  }
  return 1;
}

const char* findItemObjectById(const char* payload, const char* item_id, const char** object_end) {
  if (!payload || !item_id || !item_id[0]) return NULL;
  const char* cursor = payload;
  while ((cursor = strstr(cursor, item_id)) != NULL) {
    const char* object_start = cursor;
    while (object_start > payload && *object_start != '{') object_start--;
    if (*object_start != '{') {
      cursor += strlen(item_id);
      continue;
    }

    const char* end = matchingClose(object_start, '}');
    if (!end || cursor > end) {
      cursor += strlen(item_id);
      continue;
    }

    char parsed_id[48];
    extractString(object_start, end, "id", parsed_id, sizeof(parsed_id), "");
    if (strcmp(parsed_id, item_id) == 0) {
      if (object_end) *object_end = end;
      return object_start;
    }
    cursor += strlen(item_id);
  }
  return NULL;
}

}  // namespace

void jsonEscapeString(const char* input, char* out, size_t out_size) {
  if (!out || out_size == 0) return;
  if (!input) input = "";
  size_t written = 0;
  for (const char* cursor = input; *cursor && written + 1 < out_size; cursor++) {
    const unsigned char ch = static_cast<unsigned char>(*cursor);
    const char* replacement = NULL;
    if (ch == '\\') replacement = "\\\\";
    else if (ch == '"') replacement = "\\\"";
    else if (ch == '\n') replacement = "\\n";
    else if (ch == '\r') replacement = "\\r";
    else if (ch == '\t') replacement = "\\t";

    if (replacement) {
      for (const char* r = replacement; *r && written + 1 < out_size; r++) out[written++] = *r;
    } else if (ch >= 0x20) {
      out[written++] = static_cast<char>(ch);
    }
  }
  out[written] = '\0';
}

int parseDashboard(const char* json, Dashboard* dashboard) {
  memset(dashboard, 0, sizeof(*dashboard));

  if (!json || !extractBool(json, NULL, "ok", 0)) return 0;
  extractString(json, NULL, "generated_at", dashboard->generated_at, sizeof(dashboard->generated_at), "unknown");
  extractString(json, NULL, "version", dashboard->version, sizeof(dashboard->version), "");

  const char* lists_value = findKeyInRange(json, NULL, "lists");
  if (!lists_value || *lists_value != '[') return 1;
  const char* lists_end = matchingClose(lists_value, ']');
  if (!lists_end) return 1;

  const char* cursor = lists_value + 1;
  while (cursor < lists_end && dashboard->list_count < kMaxLists) {
    const char* object_start = strchr(cursor, '{');
    if (!object_start || object_start >= lists_end) break;
    const char* object_end = matchingClose(object_start, '}');
    if (!object_end || object_end > lists_end) break;

    List* list = &dashboard->lists[dashboard->list_count];
    extractString(object_start, object_end, "key", list->key, sizeof(list->key), "");
    extractString(object_start, object_end, "title", list->title, sizeof(list->title), list->key);
    parseItems(object_start, object_end, list);
    if (list->key[0] || list->title[0]) dashboard->list_count++;
    cursor = object_end + 1;
  }
  parseRecipes(json, dashboard);
  return 1;
}

void freeDashboard(Dashboard* dashboard) {
  (void)dashboard;
}

int patchCachedItemDone(const char* cache, const char* item_id, int done) {
  char* payload = readFile(cache);
  if (!payload) return 0;

  const char* object_end = NULL;
  const char* object_start = findItemObjectById(payload, item_id, &object_end);
  const char* done_value = object_start ? findKeyInRange(object_start, object_end, "done") : NULL;
  if (!done_value || (strncmp(done_value, "true", 4) != 0 && strncmp(done_value, "false", 5) != 0)) {
    free(payload);
    return 0;
  }

  const char* replacement = done ? "true" : "false";
  const size_t old_value_len = strncmp(done_value, "true", 4) == 0 ? 4 : 5;
  const size_t replacement_len = strlen(replacement);
  const size_t payload_len = strlen(payload);
  const size_t prefix_len = static_cast<size_t>(done_value - payload);
  const size_t suffix_offset = prefix_len + old_value_len;
  const size_t suffix_len = payload_len - suffix_offset;
  const size_t next_len = prefix_len + replacement_len + suffix_len;

  char* next_payload = static_cast<char*>(malloc(next_len + 1));
  if (!next_payload) {
    free(payload);
    return 0;
  }
  memcpy(next_payload, payload, prefix_len);
  memcpy(next_payload + prefix_len, replacement, replacement_len);
  memcpy(next_payload + prefix_len + replacement_len, payload + suffix_offset, suffix_len);
  next_payload[next_len] = '\0';

  const int ok = writeTextFileAtomic(cache, next_payload, next_len);
  free(next_payload);
  free(payload);
  fprintf(stderr, "toggle=optimistic-cache ok=%d id=%s done=%d\n", ok, item_id, done);
  return ok;
}

}  // namespace json
