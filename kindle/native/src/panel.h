#ifndef KINDLE_DASHBOARD_PANEL_H
#define KINDLE_DASHBOARD_PANEL_H

#include "canvas.h"
#include "model.h"
#include "pgm_cache.h"
#include "touch_registry.h"

// Which record a data-driven view should show. A panel reads only the field it
// cares about (ListPanel -> list_index, RecipePanel -> recipe_index); the
// others are ignored.
struct ViewState {
  int list_index;
  int recipe_index;
  int delete_item_index;  // -1 = no overlay; otherwise the row being delete-confirmed
};

// Cross-cutting rendering dependencies handed to every panel: where to register
// tappable regions, the image cache to draw photos from, and whether images
// should be inverted for dark mode.
struct RenderContext {
  TouchRegionRegistry& touch;
  PgmCache& pgm_cache;
  int invert_images;
};

// Geometry of the always-present EXIT button. Shared by the header chrome and
// the touch-input fallback, so it lives as a free function rather than on Panel.
Rect exitButtonRectForScreen(int width, int height);

// Abstract base for every full-screen view. Concrete panels implement render();
// the shared top-header / sub-header chrome lives here as protected helpers so
// all panels draw it identically.
//
// Extensibility: add a new screen by deriving a new Panel, implementing
// render(), and registering an instance with the Navigator. Existing panels and
// the touch pipeline need no changes.
class Panel {
 public:
  virtual ~Panel() = default;

  virtual void render(Canvas& canvas, const Dashboard& dashboard, const char* status,
                      const ViewState& state, RenderContext& ctx) const = 0;

 protected:
  // Title bar with the EXIT (X) button and the last-sync line.
  void drawTopHeader(Canvas& canvas, const Dashboard& dashboard, const char* status,
                     int shell_x, int shell_y, int shell_w, RenderContext& ctx) const;
  // Secondary header with a title plus HOME / BACK buttons.
  void drawSubHeader(Canvas& canvas, int shell_x, int y, int shell_w, const char* title,
                     RenderContext& ctx) const;
};

// The home grid: two list cards plus the cookbook tile.
class HomePanel : public Panel {
 public:
  void render(Canvas& canvas, const Dashboard& dashboard, const char* status,
              const ViewState& state, RenderContext& ctx) const override;
};

// A single list shown full-screen (state.list_index).
class ListPanel : public Panel {
 public:
  void render(Canvas& canvas, const Dashboard& dashboard, const char* status,
              const ViewState& state, RenderContext& ctx) const override;
};

// The recipe grid (cookbook).
class CookbookPanel : public Panel {
 public:
  void render(Canvas& canvas, const Dashboard& dashboard, const char* status,
              const ViewState& state, RenderContext& ctx) const override;
};

// A single recipe's detail view (state.recipe_index).
class RecipePanel : public Panel {
 public:
  void render(Canvas& canvas, const Dashboard& dashboard, const char* status,
              const ViewState& state, RenderContext& ctx) const override;
};

#endif  // KINDLE_DASHBOARD_PANEL_H
