#include "panel.h"

#include <stdio.h>

#include "constants.h"
#include "util.h"

using namespace util;

Rect exitButtonRectForScreen(int width, int) {
  const int shell_w = width;
  const int shell_x = 0;
  Rect rect;
  rect.w = 172;
  rect.h = 96;
  rect.x = shell_x + shell_w - rect.w - 28;
  rect.y = kKindleStatusBarHeight + 20;
  return rect;
}

namespace {

void drawListCard(Canvas& canvas, int x, int y, int w, int h, const List* list, int list_index, RenderContext& ctx) {
  const int card_radius = 22;
  canvas.strokeRoundedRect(x, y, w, h, card_radius, 3, 0);
  Rect card_rect = {x, y, w, h};
  ctx.touch.add(card_rect, kTouchOpenList, list_index, -1, "", 0);

  const char* title = displayListTitleForIndex(list, list_index);

  if (h < 112) {
    canvas.drawTextCentered(x + w / 2, y + 14, w - 24, title, 4, 0);
    canvas.line(x + 10, y + 52, x + w - 10, y + 52, 2, 0);
    if (list->item_count > 0) {
      const int box_size = 20;
      canvas.drawCheckbox(x + 18, y + 68, box_size, list->items[0].done);
      char item_text[96];
      upperCopy(item_text, sizeof(item_text), list->items[0].text);
      canvas.drawTextClipped(x + 18 + box_size + 8, y + 66, w - 36 - box_size - 8, item_text, 2, 0);
    } else {
      canvas.drawTextCentered(x + w / 2, y + 66, w - 36, "NO ITEMS", 2, 0);
    }
    return;
  }

  canvas.drawTextCentered(x + w / 2, y + 14, w - 24, title, 4, 0);
  canvas.line(x + 10, y + 52, x + w - 10, y + 52, 2, 0);
  int row_capacity = (h - 124) / 42;
  if (row_capacity < 1) row_capacity = 1;
  const int max_preview_rows = list_index == 1 ? 8 : 5;
  if (row_capacity > max_preview_rows) row_capacity = max_preview_rows;
  const int shown = list->item_count > row_capacity ? row_capacity : list->item_count;
  const int box_size = 26;
  for (int i = 0; i < shown; i++) {
    const int row_y = y + 74 + i * 42;
    canvas.drawCheckbox(x + 18, row_y + 3, box_size, list->items[i].done);
    char item_text[96];
    upperCopy(item_text, sizeof(item_text), list->items[i].text);
    canvas.drawTextClipped(x + 18 + box_size + 10, row_y, w - 36 - box_size - 10, item_text, 3, 0);
  }
  if (list->item_count > shown && h >= 190) {
    char more[48];
    snprintf(more, sizeof(more), "+%d MORE", list->item_count - shown);
    canvas.drawTextClipped(x + 18, y + h - 68, 180, more, 3, 0);
  }
  if (h >= 160) {
    const int hint_scale = h < 172 ? 2 : 3;
    const char* hint = "TAP TO OPEN";
    const int icon_w = 10 * hint_scale;
    const int hint_gap = 8;
    const int total_w = icon_w + hint_gap + textWidth(hint, hint_scale);
    const int hint_y = y + h - 34;
    const int start_x = x + w / 2 - total_w / 2;
    canvas.drawHeartIcon(start_x, hint_y - 2 * hint_scale, hint_scale);
    canvas.drawText(start_x + icon_w + hint_gap, hint_y, hint, hint_scale, 0);
  }
}

void drawCookbookTile(Canvas& canvas, int x, int y, int w, int h, RenderContext& ctx) {
  const int tile_radius = 22;
  canvas.strokeRoundedRect(x, y, w, h, tile_radius, 3, 0);
  Rect tile_rect = {x, y, w, h};
  ctx.touch.add(tile_rect, kTouchOpenRecipes, -1, -1, "", 0);

  // Fit the whole cookbook image inside the tile (contain, not cover) so its
  // tall book shape isn't cropped to fill the box. A small margin keeps it off
  // the rounded border.
  const int margin = 8;
  const int img_x = x + margin;
  const int img_y = y + margin;
  const int img_w = w - 2 * margin;
  const int img_h = h - 2 * margin;
  if (img_w > 0 && img_h > 0) {
    canvas.drawPgmImageContain(img_x, img_y, img_w, img_h,
                        kCookbookIconPath, kCookbookIconLocalPath,
                        ctx.invert_images, ctx.pgm_cache);
  }
}

}  // namespace

void Panel::drawTopHeader(Canvas& canvas, const Dashboard& dashboard, const char* status, int shell_x, int shell_y, int shell_w, RenderContext& ctx) const {
  const int header_h = 132;
  canvas.doubleRoundedRect(shell_x + 10, shell_y + 10, shell_w - 20, header_h, 26, 0);

  Rect exit_rect = exitButtonRectForScreen(canvas.width, canvas.height);
  const int refresh_w = 150;
  const int refresh_gap = 16;
  Rect refresh_rect = {exit_rect.x - refresh_gap - refresh_w, exit_rect.y, refresh_w, exit_rect.h};

  canvas.drawTextClipped(shell_x + 28, shell_y + 24, refresh_rect.x - shell_x - 44, "HOME SWEET HOME", 4, 0);

  Rect exit_hit_rect = {exit_rect.x - 20, kKindleStatusBarHeight, exit_rect.w + 40, exit_rect.y - kKindleStatusBarHeight + exit_rect.h + 20};
  canvas.strokeRoundedRect(exit_rect.x, exit_rect.y, exit_rect.w, exit_rect.h, 18, 2, 0);
  canvas.drawTextCentered(exit_rect.x + exit_rect.w / 2, exit_rect.y + 34, exit_rect.w - 16, "EXIT", 3, 0);
  ctx.touch.add(exit_hit_rect, kTouchExit, -1, -1, "", 0);
  ctx.touch.add(exit_rect, kTouchExit, -1, -1, "", 0);

  canvas.strokeRoundedRect(refresh_rect.x, refresh_rect.y, refresh_rect.w, refresh_rect.h, 18, 2, 0);
  const int refresh_icon_cx = refresh_rect.x + refresh_rect.w / 2;
  const int refresh_icon_cy = refresh_rect.y + 30;
  canvas.circleRing(refresh_icon_cx, refresh_icon_cy, 16, 4, 78, 0);
  canvas.line(refresh_icon_cx - 4, refresh_icon_cy - 20, refresh_icon_cx + 9, refresh_icon_cy - 15, 3, 0);
  canvas.line(refresh_icon_cx + 9, refresh_icon_cy - 15, refresh_icon_cx + 1, refresh_icon_cy - 4, 3, 0);
  canvas.drawTextCentered(refresh_icon_cx, refresh_rect.y + refresh_rect.h - 30, refresh_rect.w - 16, "REFRESH", 2, 0);
  ctx.touch.add(refresh_rect, kTouchRefresh, -1, -1, "", 0);

  canvas.line(shell_x + 20, shell_y + 88, refresh_rect.x - 8, shell_y + 88, 2, 0);
  char updated[96];
  formatDisplayDate(dashboard.generated_at, status, updated, sizeof(updated));
  canvas.drawTextClipped(shell_x + 28, shell_y + 102, refresh_rect.x - shell_x - 44, updated, 2, 0);
}

void Panel::drawSubHeader(Canvas& canvas, int shell_x, int y, int shell_w, const char* title, RenderContext& ctx) const {
  const int list_header_h = 80;
  canvas.doubleRoundedRect(shell_x + 10, y, shell_w - 20, list_header_h, 20, 0);
  const int title_w = shell_w - 310;
  const int title_scale = textWidth(title, 5) <= title_w ? 5 : (textWidth(title, 4) <= title_w ? 4 : 3);
  const int title_y = y + (title_scale == 5 ? 22 : (title_scale == 4 ? 26 : 30));
  canvas.drawTextClipped(shell_x + 28, title_y, title_w, title, title_scale, 0);

  Rect back_rect = {shell_x + shell_w - 136, y + 14, 104, 52};
  Rect home_rect = {back_rect.x - 116, y + 14, 104, 52};
  canvas.strokeRoundedRect(home_rect.x, home_rect.y, home_rect.w, home_rect.h, 14, 2, 0);
  canvas.drawTextCentered(home_rect.x + home_rect.w / 2, home_rect.y + 16, home_rect.w - 12, "HOME", 2, 0);
  ctx.touch.add(home_rect, kTouchHome, -1, -1, "", 0);

  canvas.strokeRoundedRect(back_rect.x, back_rect.y, back_rect.w, back_rect.h, 14, 2, 0);
  canvas.drawTextCentered(back_rect.x + back_rect.w / 2, back_rect.y + 16, back_rect.w - 12, "BACK", 2, 0);
  ctx.touch.add(back_rect, kTouchBack, -1, -1, "", 0);
}

void HomePanel::render(Canvas& canvas, const Dashboard& dashboard, const char* status, const ViewState&, RenderContext& ctx) const {
  canvas.clear(255);
  ctx.touch.clear();
  const int shell_w = canvas.width;
  const int shell_x = 0;
  const int shell_y = kKindleStatusBarHeight;
  const int shell_h = canvas.height - shell_y;
  canvas.strokeRoundedRect(shell_x, shell_y, shell_w, shell_h, 24, 3, 0);

  drawTopHeader(canvas, dashboard, status, shell_x, shell_y, shell_w, ctx);

  const int gap = 8;
  const int header_h = 132;
  const int footer_h = 44;
  const int lists_y = shell_y + 10 + header_h + gap;
  const int lists_h = shell_y + shell_h - lists_y - footer_h - gap - 10;
  // 2×2 home grid: top row = TO DO (0) + GROCERY (1), bottom row = DAILY
  // CHORES (2, if present) + COOKBOOK tile. Even quadrants so every card has
  // the same footprint; the layout degrades cleanly when fewer lists exist.
  const int list_w = (shell_w - 20 - gap) / 2;
  const int row_h = (lists_h - gap) / 2;
  const int left_x = shell_x + 10;
  const int right_x = left_x + list_w + gap;
  const int top_y = lists_y;
  const int bottom_y = lists_y + row_h + gap;

  if (dashboard.list_count > 0) {
    drawListCard(canvas, left_x, top_y, list_w, row_h, &dashboard.lists[0], 0, ctx);
  }
  if (dashboard.list_count > 1) {
    drawListCard(canvas, right_x, top_y, list_w, row_h, &dashboard.lists[1], 1, ctx);
  }
  if (dashboard.list_count > 2) {
    drawListCard(canvas, left_x, bottom_y, list_w, row_h, &dashboard.lists[2], 2, ctx);
  }
  // Cookbook lives in the bottom-right quadrant, opposite DAILY CHORES.
  drawCookbookTile(canvas, right_x, bottom_y, list_w, row_h, ctx);

  canvas.doubleRoundedRect(shell_x + 10, shell_y + shell_h - footer_h - 10, shell_w - 20, footer_h, 16, 0);
  canvas.drawTextClipped(shell_x + 28, shell_y + shell_h - footer_h - 2, shell_w - 56, "TELEGRAM KEEPS LISTS IN SYNC // TAP REFRESH ANYTIME", 3, 0);
}

void ListPanel::render(Canvas& canvas, const Dashboard& dashboard, const char* status, const ViewState& state, RenderContext& ctx) const {
  canvas.clear(255);
  ctx.touch.clear();
  const int shell_w = canvas.width;
  const int shell_x = 0;
  const int shell_y = kKindleStatusBarHeight;
  const int shell_h = canvas.height - shell_y;
  canvas.strokeRoundedRect(shell_x, shell_y, shell_w, shell_h, 24, 3, 0);

  int list_index = state.list_index;
  if (list_index < 0 || list_index >= dashboard.list_count) list_index = 0;
  const List* list = &dashboard.lists[list_index];

  drawTopHeader(canvas, dashboard, status, shell_x, shell_y, shell_w, ctx);

  const int list_header_y = shell_y + 10 + 132 + 8;
  const int list_header_h = 80;

  const char* title = displayListTitleForIndex(list, list_index);
  drawSubHeader(canvas, shell_x, list_header_y, shell_w, title, ctx);

  const int row_x = shell_x + 18;
  const int row_w = shell_w - 36;
  const int row_h = 72;
  const int row_gap = 10;
  const int first_y = list_header_y + list_header_h + 18;
  const int max_rows = (shell_y + shell_h - first_y - 24) / (row_h + row_gap);
  const int shown = list->item_count < max_rows ? list->item_count : max_rows;
  const int box_size = 30;
  for (int i = 0; i < shown; i++) {
    const int row_y = first_y + i * (row_h + row_gap);
    Rect row_rect = {row_x, row_y, row_w, row_h};
    canvas.strokeRoundedRect(row_rect.x, row_rect.y, row_rect.w, row_rect.h, 14, 2, 0);
    ctx.touch.add(row_rect, kTouchToggleItem, list_index, i, list->items[i].id, list->items[i].done);
    canvas.drawCheckbox(row_x + 18, row_y + (row_h - box_size) / 2, box_size, list->items[i].done);
    char item_text[96];
    upperCopy(item_text, sizeof(item_text), list->items[i].text);
    canvas.drawTextClipped(row_x + 18 + box_size + 14, row_y + 20, row_w - 36 - box_size - 14, item_text, 3, 0);
  }
}

void CookbookPanel::render(Canvas& canvas, const Dashboard& dashboard, const char* status, const ViewState&, RenderContext& ctx) const {
  canvas.clear(255);
  ctx.touch.clear();
  const int shell_w = canvas.width;
  const int shell_x = 0;
  const int shell_y = kKindleStatusBarHeight;
  const int shell_h = canvas.height - shell_y;
  canvas.strokeRoundedRect(shell_x, shell_y, shell_w, shell_h, 24, 3, 0);
  drawTopHeader(canvas, dashboard, status, shell_x, shell_y, shell_w, ctx);

  const int sub_y = shell_y + 10 + 132 + 8;
  drawSubHeader(canvas, shell_x, sub_y, shell_w, "RECIPES", ctx);

  const int gap = 10;
  const int card_w = (shell_w - 36 - gap) / 2;
  const int card_h = 132;
  const int first_y = sub_y + 98;
  for (int i = 0; i < dashboard.recipe_count && i < kMaxRecipes; i++) {
    const int column = i % 2;
    const int row = i / 2;
    const int card_x = shell_x + 18 + column * (card_w + gap);
    const int card_y = first_y + row * (card_h + gap);
    if (card_y + card_h > shell_y + shell_h - 18) break;
    Rect card_rect = {card_x, card_y, card_w, card_h};
    canvas.strokeRoundedRect(card_rect.x, card_rect.y, card_rect.w, card_rect.h, 16, 3, 0);
    ctx.touch.add(card_rect, kTouchOpenRecipe, -1, i, "", 0);
    canvas.drawTextClipped(card_x + 14, card_y + 14, card_w - 28, dashboard.recipes[i].title, 3, 0);
    canvas.line(card_x + 10, card_y + 54, card_x + card_w - 10, card_y + 54, 2, 0);
    char macro_text[96];
    snprintf(macro_text, sizeof(macro_text), "%d CAL C%d F%d P%d", dashboard.recipes[i].calories, dashboard.recipes[i].carbs, dashboard.recipes[i].fat, dashboard.recipes[i].protein);
    canvas.drawTextClipped(card_x + 14, card_y + 72, card_w - 28, macro_text, 2, 0);
    canvas.drawStarRating(card_x + 14, card_y + 102, dashboard.recipes[i].rating_tenths, 1);
    canvas.drawHeartIcon(card_x + card_w - 92, card_y + 98, 2);
    canvas.drawText(card_x + card_w - 70, card_y + 104, "OPEN", 2, 0);
  }
}

void RecipePanel::render(Canvas& canvas, const Dashboard& dashboard, const char* status, const ViewState& state, RenderContext& ctx) const {
  canvas.clear(255);
  ctx.touch.clear();
  const int shell_w = canvas.width;
  const int shell_x = 0;
  const int shell_y = kKindleStatusBarHeight;
  const int shell_h = canvas.height - shell_y;
  canvas.strokeRoundedRect(shell_x, shell_y, shell_w, shell_h, 24, 3, 0);
  drawTopHeader(canvas, dashboard, status, shell_x, shell_y, shell_w, ctx);

  int recipe_index = state.recipe_index;
  if (recipe_index < 0 || recipe_index >= dashboard.recipe_count) recipe_index = 0;
  const RecipeRecord* recipe = &dashboard.recipes[recipe_index];
  const int sub_y = shell_y + 10 + 132 + 8;
  drawSubHeader(canvas, shell_x, sub_y, shell_w, "RECIPE", ctx);

  const int card_x = shell_x + 18;
  const int card_y = sub_y + 98;
  const int card_w = shell_w - 36;
  const int card_h = shell_y + shell_h - card_y - 18;
  canvas.strokeRoundedRect(card_x, card_y, card_w, card_h, 20, 3, 0);
  canvas.drawTextClipped(card_x + 20, card_y + 22, card_w - 40, recipe->title, 5, 0);
  canvas.line(card_x + 14, card_y + 76, card_x + card_w - 14, card_y + 76, 2, 0);
  canvas.drawTextClipped(card_x + 20, card_y + 86, 126, "RATING", 3, 0);
  canvas.drawStarRating(card_x + 164, card_y + 84, recipe->rating_tenths, 2);

  const int content_x = card_x + 20;
  const int content_w = card_w - 40;
  const int top_y = card_y + 126;
  const int column_gap = 14;
  const int photo_w = (content_w - column_gap) / 2;
  const int photo_h = photo_w;
  const int macro_x = content_x + photo_w + column_gap;
  const int macro_w = content_w - photo_w - column_gap;
  canvas.strokeRoundedRect(content_x, top_y, photo_w, photo_h, 14, 2, 0);
  canvas.drawRecipeLocalImage(content_x + 8, top_y + 8, photo_w - 16, photo_h - 16, recipe, ctx.invert_images, ctx.pgm_cache);

  const int macro_gap = 8;
  const int macro_box_h = (photo_h - macro_gap) / 2;
  const int macro_box_w = (macro_w - macro_gap) / 2;
  const char* labels[4] = {"CAL", "CARBS", "FAT", "PROT"};
  const int values[4] = {recipe->calories, recipe->carbs, recipe->fat, recipe->protein};
  for (int i = 0; i < 4; i++) {
    const int column = i % 2;
    const int row = i / 2;
    const int box_x = macro_x + column * (macro_box_w + macro_gap);
    const int box_y = top_y + row * (macro_box_h + macro_gap);
    canvas.strokeRoundedRect(box_x, box_y, macro_box_w, macro_box_h, 12, 2, 0);
    canvas.drawTextCentered(box_x + macro_box_w / 2, box_y + 22, macro_box_w - 8, labels[i], 2, 0);
    char value_text[24];
    snprintf(value_text, sizeof(value_text), i == 0 ? "%d" : "%dG", values[i]);
    canvas.drawTextCentered(box_x + macro_box_w / 2, box_y + 64, macro_box_w - 8, value_text, 4, 0);
  }

  const int ingredients_title_y = top_y + photo_h + 28;
  canvas.drawTextClipped(content_x, ingredients_title_y, content_w, "INGREDIENTS", 3, 0);
  const int ingredient_y = ingredients_title_y + 40;
  const int ingredient_row_h = 34;
  const int amount_w = 180;
  int ingredients_shown = 0;
  for (int i = 0; i < recipe->ingredient_count && i < kMaxRecipeIngredients; i++) {
    const int row_y = ingredient_y + i * ingredient_row_h;
    if (row_y + ingredient_row_h > card_y + card_h - 112) break;
    canvas.drawTextClipped(content_x + 4, row_y, content_w - amount_w - 12, recipe->ingredients[i].name, 3, 0);
    canvas.drawTextClipped(card_x + card_w - amount_w - 20, row_y, amount_w, recipe->ingredients[i].amount, 3, 0);
    ingredients_shown++;
  }
  const int steps_y = ingredient_y + ingredients_shown * ingredient_row_h + 24;
  if (steps_y + 58 < card_y + card_h) {
    canvas.drawTextClipped(content_x, steps_y, content_w, "STEPS", 3, 0);
    canvas.drawTextWrapped(content_x, steps_y + 36, content_w, recipe->instructions, 2, 0, 4);
  }
}
