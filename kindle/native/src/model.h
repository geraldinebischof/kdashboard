#ifndef KINDLE_DASHBOARD_MODEL_H
#define KINDLE_DASHBOARD_MODEL_H

#include "constants.h"

// Plain-old-data records for the dashboard. These stay POD (fixed-size char
// buffers, no STL / std::string / heap) on purpose: the program targets a
// memory-constrained Kindle and copies these structs by value.

enum TouchAction {
  kTouchNone = 0,
  kTouchExit = 1,
  kTouchBack = 2,
  kTouchOpenList = 3,
  kTouchToggleItem = 4,
  kTouchOpenRecipe = 6,
  kTouchOpenRecipes = 7,
  kTouchHome = 8,
  kTouchDeleteItem = 9,      // X button on a list row -> open confirm overlay
  kTouchConfirmDelete = 10,  // YES on the confirm overlay -> delete the item
  kTouchCancelDelete = 11,   // NO (or backdrop) on the confirm overlay -> dismiss
  kTouchListPrevPage = 12,   // list footer PREV -> show the previous page of items
  kTouchListNextPage = 13,   // list footer NEXT -> show the next page of items
  kTouchCookbookPrevPage = 14,  // cookbook footer PREV -> previous page of recipes
  kTouchCookbookNextPage = 15   // cookbook footer NEXT -> next page of recipes
};

struct Item {
  char id[48];
  char text[96];
  int done;
};

struct List {
  char key[24];
  char title[40];
  Item items[kMaxItems];
  int item_count;
};

struct RecipeIngredientRecord {
  char name[64];
  char amount[32];
};

struct RecipeRecord {
  char id[48];
  char title[64];
  char instructions[160];
  RecipeIngredientRecord ingredients[kMaxRecipeIngredients];
  int ingredient_count;
};

struct Dashboard {
  char generated_at[40];
  char version[32];
  List lists[kMaxLists];
  int list_count;
  RecipeRecord recipes[kMaxRecipes];
  int recipe_count;
};

struct Options {
  char url[256];
  char events_url[256];
  char toggle_url[256];
  char delete_url[256];
  char read_token[160];
  char toggle_token[160];
  char cache[256];
  char render_only[256];
  char view[32];
  char dump_pgm[256];
  char save_pgm[256];
  int dump_width;
  int dump_height;
  int interval;
  int sleep_start_minute;
  int sleep_end_minute;
  int once;
  int invert_images;
};

struct Rect {
  int x;
  int y;
  int w;
  int h;
};

#endif  // KINDLE_DASHBOARD_MODEL_H
