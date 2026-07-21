#ifndef KINDLE_DASHBOARD_CANVAS_H
#define KINDLE_DASHBOARD_CANVAS_H

#include <stddef.h>

#include "model.h"
#include "pgm_cache.h"

// An 8-bit grayscale drawing surface. `pixels` is a non-owning buffer supplied
// by the caller (the framebuffer path or an in-memory preview buffer); Canvas
// only draws into it. All rendering primitives, the bitmap font, the small
// widget icons, and PGM image compositing live here as methods.
class Canvas {
 public:
  // Public POD fields: the framebuffer/PGM code fills these in directly and
  // reads them back out, so they stay accessible rather than hidden behind
  // accessors.
  int width;
  int height;
  unsigned char* pixels;

  void clear(unsigned char color);
  void setPixel(int x, int y, unsigned char color);
  void fillRect(int x, int y, int w, int h, unsigned char color);
  void strokeRect(int x, int y, int w, int h, int thickness, unsigned char color);
  void fillRoundedRect(int x, int y, int w, int h, int radius, unsigned char color);
  void strokeRoundedRect(int x, int y, int w, int h, int radius, int thickness, unsigned char color);
  void doubleRoundedRect(int x, int y, int w, int h, int radius, unsigned char color);
  void line(int x0, int y0, int x1, int y1, int thickness, unsigned char color);
  void circleRing(int cx, int cy, int radius, int thickness, int percent, unsigned char color);

  void drawText(int x, int y, const char* text, int scale, unsigned char color);
  void drawTextClipped(int x, int y, int max_width, const char* text, int scale, unsigned char color);
  int drawTextWrapped(int x, int y, int max_width, const char* text, int scale, unsigned char color, int max_lines);
  void drawTextCentered(int cx, int y, int max_width, const char* text, int scale, unsigned char color);

  void drawCheckbox(int x, int y, int size, int checked);
  void drawHeartIcon(int x, int y, int scale);

  // Draw a PGM (loaded via `cache`) scaled to cover the box, trimming near-white
  // borders. `invert` flips grayscale for dark-mode rendering.
  void drawPgmImageCover(int x, int y, int w, int h, const char* primary_path, const char* fallback_path, int invert, PgmCache& cache);
  // Draw a PGM scaled to fit entirely inside the box (letterbox), trimming
  // near-white borders first. Nothing is cropped. `invert` flips grayscale for
  // dark-mode rendering.
  void drawPgmImageContain(int x, int y, int w, int h, const char* primary_path, const char* fallback_path, int invert, PgmCache& cache);
  // Resolve a recipe's photo path and draw it via drawPgmImageCover.
  void drawRecipeLocalImage(int x, int y, int w, int h, const RecipeRecord* recipe, int invert, PgmCache& cache);
};

// Pixel width of `text` rendered at `scale` (pure; independent of any canvas).
int textWidth(const char* text, int scale);

#endif  // KINDLE_DASHBOARD_CANVAS_H
