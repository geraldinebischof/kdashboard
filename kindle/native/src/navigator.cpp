#include "navigator.h"

#include <string.h>

const Panel* Navigator::activePanel(const Dashboard& dashboard) const {
  switch (current_) {
    case View::Recipe:
      return &recipe_panel_;
    case View::Cookbook:
      return &cookbook_panel_;
    case View::List:
      // Fall back to the home grid if the requested list no longer exists.
      if (list_index_ >= 0 && list_index_ < dashboard.list_count) return &list_panel_;
      return &home_panel_;
    case View::Home:
    default:
      return &home_panel_;
  }
}

void Navigator::render(Canvas& canvas, const Dashboard& dashboard, const char* status, RenderContext& ctx) const {
  ViewState state{list_index_, recipe_index_};
  activePanel(dashboard)->render(canvas, dashboard, status, state, ctx);
}

void Navigator::applyInitialView(const char* view) {
  if (!view || !view[0]) return;
  goHome();
  if (strcmp(view, "cookbook") == 0) {
    openRecipes();
  } else if (strcmp(view, "recipe") == 0) {
    openRecipes();
    openRecipe(0);
  } else if (strcmp(view, "todo") == 0) {
    openList(0);
  } else if (strcmp(view, "grocery") == 0) {
    openList(1);
  } else if (strcmp(view, "daily_chores") == 0) {
    openList(2);
  }
}

void Navigator::goHome() {
  current_ = View::Home;
  list_index_ = -1;
  recipe_index_ = -1;
}

void Navigator::goBack() {
  switch (current_) {
    case View::Recipe:
      current_ = View::Cookbook;
      recipe_index_ = -1;
      break;
    case View::Cookbook:
      current_ = View::Home;
      break;
    case View::List:
      current_ = View::Home;
      list_index_ = -1;
      break;
    case View::Home:
    default:
      break;
  }
}

void Navigator::openList(int list_index) {
  current_ = View::List;
  list_index_ = list_index;
}

void Navigator::openRecipe(int recipe_index) {
  current_ = View::Recipe;
  recipe_index_ = recipe_index;
}

void Navigator::openRecipes() {
  current_ = View::Cookbook;
}
