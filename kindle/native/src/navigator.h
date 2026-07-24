#ifndef KINDLE_DASHBOARD_NAVIGATOR_H
#define KINDLE_DASHBOARD_NAVIGATOR_H

#include "canvas.h"
#include "model.h"
#include "panel.h"

// Owns the set of panels and the current view state (which screen, and which
// list/recipe it points at), replacing the former g_active_* view flags. It
// selects the active panel and drives navigation transitions.
//
// Extensibility: to add a screen, add a View value, a Panel member, a case in
// activePanel(), and a transition method — nothing else in the app changes.
class Navigator {
 public:
  enum class View { Home, List, Cookbook, Recipe };

  // Draw the active view into `canvas`.
  void render(Canvas& canvas, const Dashboard& dashboard, const char* status, RenderContext& ctx) const;

  // Apply the initial view requested on the command line ("cookbook", "recipe",
  // "todo", "grocery", "daily_chores"); unknown/empty leaves the current view.
  void applyInitialView(const char* view);

  void goHome();
  void goBack();
  void openList(int list_index);
  void openRecipe(int recipe_index);
  void openRecipes();

  // Per-item delete confirmation overlay (ListPanel only). -1 = dismissed.
  void requestDeleteItem(int item_index) { delete_item_index_ = item_index; }
  void cancelDeleteItem() { delete_item_index_ = -1; }
  int deleteItemIndex() const { return delete_item_index_; }

 private:
  const Panel* activePanel(const Dashboard& dashboard) const;

  View current_ = View::Home;
  int list_index_ = -1;
  int recipe_index_ = -1;
  int delete_item_index_ = -1;

  HomePanel home_panel_;
  ListPanel list_panel_;
  CookbookPanel cookbook_panel_;
  RecipePanel recipe_panel_;
};

#endif  // KINDLE_DASHBOARD_NAVIGATOR_H
