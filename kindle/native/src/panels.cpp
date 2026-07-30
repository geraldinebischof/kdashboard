#include "panel.h"

#include <stdio.h>

#include "constants.h"
#include "util.h"

using namespace util;

// The EXIT (X) button sits at the far left of the home panel header, sized to
// match the title row (scale 4). Width/height params are retained for the
// shared signature used by the touch-input fallback, even though the button no
// longer depends on screen dimensions.
Rect exitButtonRectForScreen(int, int) {
  const int shell_x = 0;
  Rect rect;
  rect.w = 52;
  rect.h = 52;
  rect.x = shell_x + 28;
  rect.y = kKindleStatusBarHeight + 14;
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
      canvas.drawBulletPoint(x + 18, y + 68, box_size);
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
  const int shown = list->item_count > row_capacity ? row_capacity : list->item_count;
  const int box_size = 26;
  for (int i = 0; i < shown; i++) {
    const int row_y = y + 74 + i * 42;
    canvas.drawBulletPoint(x + 18, row_y + 3, box_size);
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

// Geometry of the per-row X (delete) affordance. Kept in one spot so the row
// layout and the hit-test region stay in lockstep.
constexpr int kDeleteBtnSize = 48;
constexpr int kDeleteBtnGap = 14;

// Draw the small "X" delete button at the right edge of a row and register its
// touch region. Region is added AFTER the row's toggle region so the X wins on
// tap (registry resolves most-recently-added first).
void drawDeleteButton(Canvas& canvas, int row_x, int row_y, int row_w, int row_h,
                      int item_index, const char* item_id, RenderContext& ctx) {
  const int btn_x = row_x + row_w - kDeleteBtnSize - kDeleteBtnGap;
  const int btn_y = row_y + (row_h - kDeleteBtnSize) / 2;
  canvas.strokeRoundedRect(btn_x, btn_y, kDeleteBtnSize, kDeleteBtnSize, 12, 2, 0);
  canvas.drawTextCentered(btn_x + kDeleteBtnSize / 2, btn_y + (kDeleteBtnSize - 6 * 3) / 2 + 2,
                          kDeleteBtnSize - 8, "X", 3, 0);
  Rect btn_rect = {btn_x, btn_y, kDeleteBtnSize, kDeleteBtnSize};
  ctx.touch.add(btn_rect, kTouchDeleteItem, -1, item_index, item_id, 0);
}

// Pagination helpers shared by ListPanel and CookbookPanel. The scroll offset is
// the index of the first record shown; it is snapped down to a page boundary and
// clamped to the last valid page so a page always starts on a boundary even if
// the underlying list grew or shrank since the previous render.
int clampPageStart(int offset, int page_size, int count) {
  if (count <= 0 || page_size <= 0) return 0;
  if (offset < 0) return 0;
  const int snapped = (offset / page_size) * page_size;
  const int last_start = ((count - 1) / page_size) * page_size;
  return snapped > last_start ? last_start : snapped;
}

int pageCount(int page_size, int count) {
  if (count <= 0 || page_size <= 0) return 1;
  return (count + page_size - 1) / page_size;
}

// Draw a single PREV/NEXT control. Disabled controls (first page has no PREV,
// last page has no NEXT) are drawn dimmed and register no touch region, so a
// tap on them simply does nothing.
void drawPageButton(Canvas& canvas, int x, int y, int w, int h, const char* label,
                    int enabled, TouchAction action, RenderContext& ctx) {
  const unsigned char ink = enabled ? 0 : 200;
  canvas.strokeRoundedRect(x, y, w, h, 12, enabled ? 3 : 2, ink);
  canvas.drawTextCentered(x + w / 2, y + (h - 6 * 3) / 2 + 2, w - 12, label, 3, ink);
  if (enabled) {
    Rect rect = {x, y, w, h};
    ctx.touch.add(rect, action, -1, -1, "", 0);
  }
}

// Footer with PREV ... "page X / Y" ... NEXT. Shown only by panels whose record
// count exceeds one page. `shell_bottom` is the bottom edge of the content shell.
void drawPageFooter(Canvas& canvas, int shell_x, int shell_w, int shell_bottom,
                    int offset, int page_size, int count,
                    TouchAction prev_action, TouchAction next_action, RenderContext& ctx) {
  const int footer_h = kPageFooterHeight;
  const int footer_x = shell_x + 18;
  const int footer_w = shell_w - 36;
  const int footer_y = shell_bottom - footer_h - 14;
  const int btn_w = 120;
  const int btn_h = 40;
  const int btn_y = footer_y + (footer_h - btn_h) / 2;
  // Paging is circular (PREV on the first page wraps to the last, NEXT on the
  // last wraps to the first), so both controls are always active while a footer
  // is shown. The "page X / Y" indicator still tells the user where they are.
  drawPageButton(canvas, footer_x, btn_y, btn_w, btn_h, "PREV", 1, prev_action, ctx);
  drawPageButton(canvas, footer_x + footer_w - btn_w, btn_y, btn_w, btn_h, "NEXT", 1, next_action, ctx);

  const int page = page_size > 0 ? offset / page_size + 1 : 1;
  char indicator[24];
  snprintf(indicator, sizeof(indicator), "%d / %d", page, pageCount(page_size, count));
  canvas.drawTextCentered(footer_x + footer_w / 2, btn_y + (btn_h - 6 * 3) / 2 + 2,
                          footer_w - 2 * btn_w - 48, indicator, 3, 0);
}

// Modal-style confirmation overlay shown when the user taps a row's X. Draws on
// top of the list and registers NO / YES buttons (added last, so they take
// priority over every row region beneath them).
void drawDeleteConfirmOverlay(Canvas& canvas, int shell_w, int shell_y, int shell_h,
                              const char* item_text, const char* item_id, RenderContext& ctx) {
  const int overlay_w = 620;
  const int overlay_h = 320;
  const int overlay_x = (shell_w - overlay_w) / 2;
  const int overlay_y = shell_y + (shell_h - overlay_h) / 2;

  // Dim the screen behind the modal with a light grey wash before drawing the
  // card. 224 is a mid-light grey readable on e-ink without fully hiding the
  // list underneath.
  canvas.fillRect(0, shell_y, shell_w, shell_h, 224);

  canvas.fillRoundedRect(overlay_x, overlay_y, overlay_w, overlay_h, 24, 255);
  canvas.doubleRoundedRect(overlay_x, overlay_y, overlay_w, overlay_h, 24, 0);

  canvas.drawTextCentered(overlay_x + overlay_w / 2, overlay_y + 36, overlay_w - 48,
                          "DELETE THIS ITEM?", 4, 0);
  canvas.line(overlay_x + 40, overlay_y + 96, overlay_x + overlay_w - 40, overlay_y + 96, 2, 0);

  char upper_text[96];
  upperCopy(upper_text, sizeof(upper_text), item_text);
  canvas.drawTextClipped(overlay_x + 40, overlay_y + 130, overlay_w - 80, upper_text, 3, 0);

  const int btn_w = 220;
  const int btn_h = 72;
  const int btn_gap = 24;
  const int btn_y = overlay_y + overlay_h - btn_h - 36;
  const int btns_total = 2 * btn_w + btn_gap;
  const int btn_left_x = overlay_x + (overlay_w - btns_total) / 2;
  const int btn_right_x = btn_left_x + btn_w + btn_gap;

  // Backdrop: a tap anywhere in the shell outside the buttons dismisses the
  // overlay. Registered before NO/YES (so those still win) and after every row /
  // footer region, so underlying controls can never fire while the modal is up.
  Rect backdrop_rect = {0, shell_y, shell_w, shell_h};
  ctx.touch.add(backdrop_rect, kTouchCancelDelete, -1, -1, item_id, 0);

  // NO (left) -> cancel
  canvas.strokeRoundedRect(btn_left_x, btn_y, btn_w, btn_h, 16, 3, 0);
  canvas.drawTextCentered(btn_left_x + btn_w / 2, btn_y + (btn_h - 6 * 3) / 2 + 4,
                          btn_w - 16, "NO", 3, 0);
  Rect no_rect = {btn_left_x, btn_y, btn_w, btn_h};
  ctx.touch.add(no_rect, kTouchCancelDelete, -1, -1, item_id, 0);

  // YES (right) -> confirm
  canvas.fillRoundedRect(btn_right_x, btn_y, btn_w, btn_h, 16, 0);
  canvas.drawTextCentered(btn_right_x + btn_w / 2, btn_y + (btn_h - 6 * 3) / 2 + 4,
                          btn_w - 16, "YES", 3, 255);
  Rect yes_rect = {btn_right_x, btn_y, btn_w, btn_h};
  ctx.touch.add(yes_rect, kTouchConfirmDelete, -1, -1, item_id, 0);
}

}  // namespace

void Panel::drawTopHeader(Canvas& canvas, const Dashboard& dashboard, const char* status, int shell_x, int shell_y, int shell_w, RenderContext& ctx) const {
  const int header_h = 132;
  canvas.doubleRoundedRect(shell_x + 10, shell_y + 10, shell_w - 20, header_h, 26, 0);

  Rect exit_rect = exitButtonRectForScreen(canvas.width, canvas.height);

  // EXIT button: small box at the far left of the header, sized to the title
  // row. The X glyph is drawn at title scale (4) so the button reads as the
  // same weight as the title, not as a chunky control.
  Rect exit_hit_rect = {exit_rect.x - 20, kKindleStatusBarHeight, exit_rect.w + 40, exit_rect.y - kKindleStatusBarHeight + exit_rect.h + 20};
  canvas.strokeRoundedRect(exit_rect.x, exit_rect.y, exit_rect.w, exit_rect.h, 14, 2, 0);
  canvas.drawTextCentered(exit_rect.x + exit_rect.w / 2, exit_rect.y + 10, exit_rect.w - 16, "X", 4, 0);
  ctx.touch.add(exit_hit_rect, kTouchExit, -1, -1, "", 0);
  ctx.touch.add(exit_rect, kTouchExit, -1, -1, "", 0);

  // Title sits to the right of the EXIT button, vertically aligned with it.
  // The divider and updated line are below the button, so they span the full
  // header width as before.
  const int title_x = exit_rect.x + exit_rect.w + 16;
  const int title_w = shell_x + shell_w - title_x - 28;
  canvas.drawTextClipped(title_x, exit_rect.y + 10, title_w, "THE HORRORS PERSIST BUT SO DO WE", 4, 0);

  canvas.line(shell_x + 20, shell_y + 88, shell_x + shell_w - 28, shell_y + 88, 2, 0);
  char updated[96];
  formatDisplayDate(dashboard.generated_at, status, updated, sizeof(updated));
  canvas.drawTextClipped(shell_x + 28, shell_y + 102, shell_w - 56, updated, 2, 0);
}

void Panel::drawSubHeader(Canvas& canvas, int shell_x, int y, int shell_w, const char* title, RenderContext& ctx) const {
  const int list_header_h = 80;
  canvas.doubleRoundedRect(shell_x + 10, y, shell_w - 20, list_header_h, 20, 0);
  const int title_w = shell_w - 310;
  const int title_scale = textWidth(title, 5) <= title_w ? 5 : (textWidth(title, 4) <= title_w ? 4 : 3);
  const int title_y = y + (title_scale == 5 ? 22 : (title_scale == 4 ? 26 : 30));
  canvas.drawTextClipped(shell_x + 28, title_y, title_w, title, title_scale, 0);

  // Center HOME and BACK buttons in the title bar with a gap between them.
  const int btn_w = 104;
  const int btn_h = 52;
  const int btn_gap = 32;
  const int btns_total = 2 * btn_w + btn_gap;
  const int btn_y = y + 14;
  const int back_cx = shell_x + shell_w / 2 + btns_total / 2;
  const Rect back_rect = {back_cx - btn_w, btn_y, btn_w, btn_h};
  const Rect home_rect = {back_cx - btn_w - btn_gap - btn_w, btn_y, btn_w, btn_h};
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
  canvas.drawTextClipped(shell_x + 28, shell_y + shell_h - footer_h - 2, shell_w - 56, "TELEGRAM KEEPS LISTS IN SYNC", 3, 0);
}

void ListPanel::render(Canvas& canvas, const Dashboard& dashboard, const char* status, const ViewState& state, RenderContext& ctx) const {
  canvas.clear(255);
  ctx.touch.clear();
  const int shell_w = canvas.width;
  const int shell_x = 0;
  const int shell_y = kKindleStatusBarHeight;
  const int shell_h = canvas.height - shell_y;
  const int shell_bottom = shell_y + shell_h;
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
  const int row_stride = row_h + row_gap;
  const int first_y = list_header_y + list_header_h + 18;

  // Rows that fit on one screen with no paging footer. If the list overflows,
  // reserve footer space and recompute so every page (except the last) is full.
  int page_size = (shell_bottom - first_y - 24) / row_stride;
  if (page_size < 1) page_size = 1;
  const int item_count = list->item_count;
  const int needs_pages = item_count > page_size ? 1 : 0;
  if (needs_pages) {
    page_size = (shell_bottom - first_y - 24 - kPageFooterHeight) / row_stride;
    if (page_size < 1) page_size = 1;
  }

  // Preserve the page across refreshes but clamp/snapping it to a valid page
  // boundary in case items were added or removed.
  const int offset = clampPageStart(ctx.list_offset, page_size, item_count);
  ctx.list_offset = offset;
  ctx.list_page_size = page_size;
  ctx.list_item_count = item_count;

  const int remaining = item_count - offset;
  const int shown = remaining < page_size ? remaining : page_size;
  const int box_size = 30;
  const int delete_reserve = kDeleteBtnSize + kDeleteBtnGap * 2;  // room for X + breathing space
  for (int i = 0; i < shown; i++) {
    const int item_index = offset + i;
    const int row_y = first_y + i * row_stride;
    Rect row_rect = {row_x, row_y, row_w, row_h};
    canvas.strokeRoundedRect(row_rect.x, row_rect.y, row_rect.w, row_rect.h, 14, 2, 0);
    ctx.touch.add(row_rect, kTouchToggleItem, list_index, item_index, list->items[item_index].id, list->items[item_index].done);
    canvas.drawCheckbox(row_x + 18, row_y + (row_h - box_size) / 2, box_size, list->items[item_index].done);
    char item_text[96];
    upperCopy(item_text, sizeof(item_text), list->items[item_index].text);
    canvas.drawTextClipped(row_x + 18 + box_size + 14, row_y + 20,
                           row_w - 36 - box_size - 14 - delete_reserve, item_text, 3, 0);
    drawDeleteButton(canvas, row_x, row_y, row_w, row_h, item_index, list->items[item_index].id, ctx);
  }

  if (needs_pages) {
    drawPageFooter(canvas, shell_x, shell_w, shell_bottom, offset, page_size, item_count,
                   kTouchListPrevPage, kTouchListNextPage, ctx);
  }

  // delete_item_index is an absolute item index (the row X registers offset + i).
  if (state.delete_item_index >= 0 && state.delete_item_index < item_count) {
    const Item& target = list->items[state.delete_item_index];
    drawDeleteConfirmOverlay(canvas, shell_w, shell_y, shell_h, target.text, target.id, ctx);
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
  const int card_h = 96;
  const int card_stride = card_h + gap;
  const int first_y = sub_y + 98;
  const int shell_bottom = shell_y + shell_h;

  // Two recipe cards per row; page size is rows * 2. Reserve a paging footer
  // only when recipes overflow a single screen.
  int card_rows = (shell_bottom - first_y - 18) / card_stride;
  if (card_rows < 1) card_rows = 1;
  int page_size = card_rows * 2;
  const int recipe_count = dashboard.recipe_count;
  const int needs_pages = recipe_count > page_size ? 1 : 0;
  if (needs_pages) {
    card_rows = (shell_bottom - first_y - 18 - kPageFooterHeight) / card_stride;
    if (card_rows < 1) card_rows = 1;
    page_size = card_rows * 2;
  }

  const int offset = clampPageStart(ctx.cookbook_offset, page_size, recipe_count);
  ctx.cookbook_offset = offset;
  ctx.cookbook_page_size = page_size;
  ctx.recipe_count = recipe_count;

  const int remaining = recipe_count - offset;
  const int shown = remaining < page_size ? remaining : page_size;
  // Use the same bottom reserve the page-size math used, so every row the loop
  // claims to fit actually fits above the footer.
  const int page_bottom = shell_bottom - 18 - (needs_pages ? kPageFooterHeight : 0);
  for (int i = 0; i < shown; i++) {
    const int recipe_index = offset + i;
    if (recipe_index >= kMaxRecipes) break;
    const int column = i % 2;
    const int row = i / 2;
    const int card_x = shell_x + 18 + column * (card_w + gap);
    const int card_y = first_y + row * card_stride;
    if (card_y + card_h > page_bottom) break;
    Rect card_rect = {card_x, card_y, card_w, card_h};
    canvas.strokeRoundedRect(card_rect.x, card_rect.y, card_rect.w, card_rect.h, 16, 3, 0);
    ctx.touch.add(card_rect, kTouchOpenRecipe, -1, recipe_index, "", 0);
    canvas.drawTextClipped(card_x + 14, card_y + 14, card_w - 28, dashboard.recipes[recipe_index].title, 3, 0);
    canvas.line(card_x + 10, card_y + 54, card_x + card_w - 10, card_y + 54, 2, 0);
    canvas.drawHeartIcon(card_x + card_w - 92, card_y + 66, 2);
    canvas.drawText(card_x + card_w - 70, card_y + 72, "OPEN", 2, 0);
  }

  if (needs_pages) {
    drawPageFooter(canvas, shell_x, shell_w, shell_bottom, offset, page_size, recipe_count,
                   kTouchCookbookPrevPage, kTouchCookbookNextPage, ctx);
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

  const int content_x = card_x + 20;
  const int content_w = card_w - 40;
  const int photo_h = 180;
  canvas.strokeRoundedRect(content_x, card_y + 96, content_w, photo_h, 14, 2, 0);
  canvas.drawRecipeLocalImage(content_x + 8, card_y + 104, content_w - 16, photo_h - 16, recipe, ctx.invert_images, ctx.pgm_cache);

  const int ingredients_title_y = card_y + 96 + photo_h + 28;
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
