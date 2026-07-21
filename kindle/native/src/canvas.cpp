#include "canvas.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"
#include "util.h"

namespace {

int clampRoundedRadius(int w, int h, int radius) {
  const int min_side = w < h ? w : h;
  const int max_radius = min_side / 2;
  if (radius < 0) return 0;
  if (radius > max_radius) return max_radius;
  return radius;
}

// True if the local point (xx, yy) inside a w x h box falls within a
// rounded-rectangle of corner radius r (same distance-formula technique
// circleRing already uses, applied per-corner).
int insideRoundedRect(int xx, int yy, int w, int h, int r) {
  if (xx < 0 || yy < 0 || xx >= w || yy >= h) return 0;
  if (r <= 0) return 1;
  int dx = 0;
  int dy = 0;
  if (xx < r && yy < r) {
    dx = r - 1 - xx;
    dy = r - 1 - yy;
  } else if (xx >= w - r && yy < r) {
    dx = xx - (w - r);
    dy = r - 1 - yy;
  } else if (xx < r && yy >= h - r) {
    dx = r - 1 - xx;
    dy = yy - (h - r);
  } else if (xx >= w - r && yy >= h - r) {
    dx = xx - (w - r);
    dy = yy - (h - r);
  } else {
    return 1;
  }
  return dx * dx + dy * dy <= r * r;
}

unsigned char glyphRow(char ch, int row) {
  static const unsigned char digits[10][7] = {
    {14, 17, 19, 21, 25, 17, 14}, {4, 12, 4, 4, 4, 4, 14}, {14, 17, 1, 2, 4, 8, 31}, {30, 1, 1, 14, 1, 1, 30}, {2, 6, 10, 18, 31, 2, 2},
    {31, 16, 30, 1, 1, 17, 14}, {6, 8, 16, 30, 17, 17, 14}, {31, 1, 2, 4, 8, 8, 8}, {14, 17, 17, 14, 17, 17, 14}, {14, 17, 17, 15, 1, 2, 12}
  };
  static const unsigned char letters[26][7] = {
    {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},{30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
    {14,17,16,23,17,17,14},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},{7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},{30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4},{31,1,2,4,8,16,31}
  };
  if (ch >= '0' && ch <= '9') return digits[ch - '0'][row];
  if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
  if (ch >= 'A' && ch <= 'Z') return letters[ch - 'A'][row];
  switch (ch) {
    case ' ': return 0;
    case '/': { static const unsigned char g[7] = {1,1,2,4,8,16,16}; return g[row]; }
    case ':': { static const unsigned char g[7] = {0,4,4,0,4,4,0}; return g[row]; }
    case '-': { static const unsigned char g[7] = {0,0,0,31,0,0,0}; return g[row]; }
    case '_': { static const unsigned char g[7] = {0,0,0,0,0,0,31}; return g[row]; }
    case '.': { static const unsigned char g[7] = {0,0,0,0,0,12,12}; return g[row]; }
    case ',': { static const unsigned char g[7] = {0,0,0,0,0,4,8}; return g[row]; }
    case '%': { static const unsigned char g[7] = {17,18,4,8,19,17,0}; return g[row]; }
    case '[': { static const unsigned char g[7] = {14,8,8,8,8,8,14}; return g[row]; }
    case ']': { static const unsigned char g[7] = {14,2,2,2,2,2,14}; return g[row]; }
    case '+': { static const unsigned char g[7] = {0,4,4,31,4,4,0}; return g[row]; }
    case '|': { static const unsigned char g[7] = {4,4,4,4,4,4,4}; return g[row]; }
    case '!': { static const unsigned char g[7] = {4,4,4,4,4,0,4}; return g[row]; }
    case '#': { static const unsigned char g[7] = {10,31,10,10,31,10,0}; return g[row]; }
    default: { static const unsigned char g[7] = {31,1,2,4,4,0,4}; return g[row]; }
  }
}

void recipePhotoPath(const char* base_dir, const char* recipe_id, char* out, size_t out_size) {
  char safe_id[64];
  size_t j = 0;
  for (size_t i = 0; recipe_id && recipe_id[i] && j + 1 < sizeof(safe_id); i++) {
    const char ch = recipe_id[i];
    if (isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_') safe_id[j++] = ch;
  }
  safe_id[j] = '\0';
  if (!safe_id[0]) {
    if (out_size > 0) out[0] = '\0';
    return;
  }
  snprintf(out, out_size, "%s/%s.pgm", base_dir, safe_id);
}

}  // namespace

int textWidth(const char* text, int scale) {
  return static_cast<int>(strlen(text ? text : "")) * 6 * scale;
}

void Canvas::clear(unsigned char color) {
  if (!pixels) return;
  memset(pixels, color, static_cast<size_t>(width) * static_cast<size_t>(height));
}

void Canvas::setPixel(int x, int y, unsigned char color) {
  if (!pixels) return;
  if (x < 0 || y < 0 || x >= width || y >= height) return;
  pixels[y * width + x] = color;
}

void Canvas::fillRect(int x, int y, int w, int h, unsigned char color) {
  for (int yy = y; yy < y + h; yy++) {
    if (yy < 0 || yy >= height) continue;
    for (int xx = x; xx < x + w; xx++) setPixel(xx, yy, color);
  }
}

void Canvas::strokeRect(int x, int y, int w, int h, int thickness, unsigned char color) {
  fillRect(x, y, w, thickness, color);
  fillRect(x, y + h - thickness, w, thickness, color);
  fillRect(x, y, thickness, h, color);
  fillRect(x + w - thickness, y, thickness, h, color);
}

void Canvas::fillRoundedRect(int x, int y, int w, int h, int radius, unsigned char color) {
  if (w <= 0 || h <= 0) return;
  const int r = clampRoundedRadius(w, h, radius);
  for (int yy = 0; yy < h; yy++) {
    const int py = y + yy;
    if (py < 0 || py >= height) continue;
    for (int xx = 0; xx < w; xx++) {
      if (insideRoundedRect(xx, yy, w, h, r)) setPixel(x + xx, py, color);
    }
  }
}

void Canvas::strokeRoundedRect(int x, int y, int w, int h, int radius, int thickness, unsigned char color) {
  if (w <= 0 || h <= 0 || thickness <= 0) return;
  const int r = clampRoundedRadius(w, h, radius);
  const int inner_w = w - 2 * thickness;
  const int inner_h = h - 2 * thickness;
  const int inner_r = r > thickness ? r - thickness : 0;
  for (int yy = 0; yy < h; yy++) {
    const int py = y + yy;
    if (py < 0 || py >= height) continue;
    for (int xx = 0; xx < w; xx++) {
      if (!insideRoundedRect(xx, yy, w, h, r)) continue;
      if (inner_w > 0 && inner_h > 0 && insideRoundedRect(xx - thickness, yy - thickness, inner_w, inner_h, inner_r)) continue;
      setPixel(x + xx, py, color);
    }
  }
}

void Canvas::doubleRoundedRect(int x, int y, int w, int h, int radius, unsigned char color) {
  const int r = clampRoundedRadius(w, h, radius);
  strokeRoundedRect(x, y, w, h, r, 3, color);
  const int inset = 7;
  const int inner_r = r > inset ? r - inset : 0;
  strokeRoundedRect(x + inset, y + inset, w - inset * 2, h - inset * 2, inner_r, 2, color);
}

void Canvas::line(int x0, int y0, int x1, int y1, int thickness, unsigned char color) {
  const int dx = abs(x1 - x0);
  const int sx = x0 < x1 ? 1 : -1;
  const int dy = -abs(y1 - y0);
  const int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  while (1) {
    fillRect(x0 - thickness / 2, y0 - thickness / 2, thickness, thickness, color);
    if (x0 == x1 && y0 == y1) break;
    const int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

void Canvas::circleRing(int cx, int cy, int radius, int thickness, int percent, unsigned char color) {
  const int outer = radius;
  const int inner = radius - thickness;
  const int outer2 = outer * outer;
  const int inner2 = inner * inner;
  const double progress = percent < 0 ? 0.0 : (percent > 100 ? 1.0 : percent / 100.0);
  for (int y = cy - outer; y <= cy + outer; y++) {
    for (int x = cx - outer; x <= cx + outer; x++) {
      const int dx = x - cx;
      const int dy = y - cy;
      const int d2 = dx * dx + dy * dy;
      if (d2 > outer2 || d2 < inner2) continue;
      double angle = atan2(static_cast<double>(dy), static_cast<double>(dx)) + M_PI / 2.0;
      if (angle < 0) angle += M_PI * 2.0;
      if (angle / (M_PI * 2.0) <= progress) setPixel(x, y, color);
    }
  }
}

void Canvas::drawText(int x, int y, const char* text, int scale, unsigned char color) {
  int cursor = x;
  for (size_t i = 0; text && text[i]; i++) {
    char ch = text[i];
    if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
    for (int row = 0; row < 7; row++) {
      const unsigned char bits = glyphRow(ch, row);
      for (int col = 0; col < 5; col++) {
        if (bits & (1 << (4 - col))) fillRect(cursor + col * scale, y + row * scale, scale, scale, color);
      }
    }
    cursor += 6 * scale;
  }
}

void Canvas::drawTextClipped(int x, int y, int max_width, const char* text, int scale, unsigned char color) {
  char clipped[128];
  util::copyText(clipped, sizeof(clipped), text);
  const int max_chars = max_width / (6 * scale);
  if (max_chars > 0 && static_cast<int>(strlen(clipped)) > max_chars) clipped[max_chars] = '\0';
  drawText(x, y, clipped, scale, color);
}

int Canvas::drawTextWrapped(int x, int y, int max_width, const char* text, int scale, unsigned char color, int max_lines) {
  if (!text || !text[0] || max_lines <= 0) return 0;
  char source[256];
  util::copyText(source, sizeof(source), text);
  const int max_chars = max_width / (6 * scale);
  if (max_chars <= 0) return 0;

  int lines = 0;
  char line_text[128] = "";
  char* cursor = source;
  while (*cursor && lines < max_lines) {
    while (*cursor && isspace(static_cast<unsigned char>(*cursor))) cursor++;
    if (!*cursor) break;
    char* word = cursor;
    while (*cursor && !isspace(static_cast<unsigned char>(*cursor))) cursor++;
    const char saved = *cursor;
    *cursor = '\0';

    const int line_len = static_cast<int>(strlen(line_text));
    const int word_len = static_cast<int>(strlen(word));
    if (line_len > 0 && line_len + 1 + word_len > max_chars) {
      drawText(x, y + lines * (8 * scale + 6), line_text, scale, color);
      lines++;
      line_text[0] = '\0';
    }
    if (lines >= max_lines) break;
    if (word_len > max_chars) {
      char clipped[128];
      util::copyText(clipped, sizeof(clipped), word);
      clipped[max_chars] = '\0';
      drawText(x, y + lines * (8 * scale + 6), clipped, scale, color);
      lines++;
    } else {
      if (line_text[0]) strncat(line_text, " ", sizeof(line_text) - strlen(line_text) - 1);
      strncat(line_text, word, sizeof(line_text) - strlen(line_text) - 1);
    }

    *cursor = saved;
  }
  if (line_text[0] && lines < max_lines) {
    drawText(x, y + lines * (8 * scale + 6), line_text, scale, color);
    lines++;
  }
  return lines;
}

void Canvas::drawTextCentered(int cx, int y, int max_width, const char* text, int scale, unsigned char color) {
  char clipped[128];
  util::copyText(clipped, sizeof(clipped), text);
  const int max_chars = max_width / (6 * scale);
  if (max_chars > 0 && static_cast<int>(strlen(clipped)) > max_chars) clipped[max_chars] = '\0';
  drawText(cx - textWidth(clipped, scale) / 2, y, clipped, scale, color);
}

void Canvas::drawStarIcon(int x, int y, int scale, int filled) {
  static const char* filled_mask[] = {
    "......#......",
    ".....###.....",
    ".....###.....",
    "#############",
    ".###########.",
    "..#########..",
    "...#######...",
    "...#######...",
    "..###...###..",
    ".##.......##.",
    "##.........##"
  };
  static const char* empty_mask[] = {
    "......#......",
    ".....#.#.....",
    ".....#.#.....",
    "###..#.#..###",
    ".##.....##..",
    "..##...##...",
    "...#...#....",
    "...#...#....",
    "..##...##...",
    ".##.....##..",
    "##.......##."
  };
  const char** mask = filled ? filled_mask : empty_mask;
  for (int row = 0; row < 11; row++) {
    for (int col = 0; mask[row][col]; col++) {
      if (mask[row][col] == '#') fillRect(x + col * scale, y + row * scale, scale, scale, 0);
    }
  }
}

void Canvas::drawStarRating(int x, int y, int rating_tenths, int scale) {
  int filled = (rating_tenths + 5) / 10;
  if (filled < 0) filled = 0;
  if (filled > 5) filled = 5;
  const int star_w = 13 * scale;
  const int gap = 4 * scale;
  for (int i = 0; i < 5; i++) {
    drawStarIcon(x + i * (star_w + gap), y, scale, i < filled);
  }
}

void Canvas::drawCheckbox(int x, int y, int size, int checked) {
  if (checked) {
    fillRoundedRect(x, y, size, size, size / 4, 0);
    const int cx0 = x + size * 2 / 10;
    const int cy0 = y + size * 5 / 10;
    const int cx1 = x + size * 4 / 10;
    const int cy1 = y + size * 7 / 10;
    const int cx2 = x + size * 8 / 10;
    const int cy2 = y + size * 3 / 10;
    const int thickness = size < 24 ? 2 : 3;
    line(cx0, cy0, cx1, cy1, thickness, 255);
    line(cx1, cy1, cx2, cy2, thickness, 255);
  } else {
    strokeRoundedRect(x, y, size, size, size / 4, 2, 0);
  }
}

void Canvas::drawHeartIcon(int x, int y, int scale) {
  static const char* mask[] = {
    ".##....##.",
    "####..####",
    "##########",
    "##########",
    ".########.",
    ".########.",
    "..######..",
    "..######..",
    "...####...",
    "....##....",
    "....##...."
  };
  for (int row = 0; row < 11; row++) {
    for (int col = 0; mask[row][col]; col++) {
      if (mask[row][col] == '#') fillRect(x + col * scale, y + row * scale, scale, scale, 0);
    }
  }
}

void Canvas::drawPgmImageCover(int x, int y, int w, int h, const char* primary_path, const char* fallback_path, int invert, PgmCache& cache) {
  int image_w = 0;
  int image_h = 0;
  const unsigned char* pixels = cache.load(primary_path, fallback_path, &image_w, &image_h);
  if (!pixels) {
    strokeRect(x, y, w, h, 2, 0);
    drawTextCentered(x + w / 2, y + h / 2 - 12, w - 20, "IMAGE", 3, 0);
    return;
  }

  fillRect(x, y, w, h, invert ? 0 : 255);
  int content_left = 0;
  int content_top = 0;
  int content_right = image_w;
  int content_bottom = image_h;
  int found_content = 0;
  for (int yy = 0; yy < image_h; yy++) {
    for (int xx = 0; xx < image_w; xx++) {
      const unsigned char value = pixels[yy * image_w + xx];
      if (value > 246) continue;
      if (!found_content) {
        content_left = xx;
        content_right = xx + 1;
        content_top = yy;
        content_bottom = yy + 1;
        found_content = 1;
      } else {
        if (xx < content_left) content_left = xx;
        if (xx + 1 > content_right) content_right = xx + 1;
        if (yy < content_top) content_top = yy;
        if (yy + 1 > content_bottom) content_bottom = yy + 1;
      }
    }
  }
  if (!found_content || content_right <= content_left || content_bottom <= content_top) {
    content_left = 0;
    content_top = 0;
    content_right = image_w;
    content_bottom = image_h;
  }
  const int source_w = content_right - content_left;
  const int source_h = content_bottom - content_top;
  for (int yy = 0; yy < h; yy++) {
    const int source_y = source_w * h > w * source_h
      ? content_top + (yy * source_h) / h
      : content_top + ((yy + ((w * source_h) / source_w - h) / 2) * source_w) / w;
    if (source_y < 0 || source_y >= image_h) continue;
    for (int xx = 0; xx < w; xx++) {
      const int source_x = source_w * h > w * source_h
        ? content_left + ((xx + ((h * source_w) / source_h - w) / 2) * source_h) / h
        : content_left + (xx * source_w) / w;
      if (source_x < 0 || source_x >= image_w) continue;
      const unsigned char value = pixels[source_y * image_w + source_x];
      setPixel(x + xx, y + yy, invert ? static_cast<unsigned char>(255 - value) : value);
    }
  }
}

void Canvas::drawPgmImageContain(int x, int y, int w, int h, const char* primary_path, const char* fallback_path, int invert, PgmCache& cache) {
  int image_w = 0;
  int image_h = 0;
  const unsigned char* pixels = cache.load(primary_path, fallback_path, &image_w, &image_h);
  if (!pixels) {
    strokeRect(x, y, w, h, 2, 0);
    drawTextCentered(x + w / 2, y + h / 2 - 12, w - 20, "IMAGE", 3, 0);
    return;
  }

  fillRect(x, y, w, h, invert ? 0 : 255);
  // Auto-trim near-white borders (pixels > 246) so the image's own content
  // drives the aspect ratio, matching drawPgmImageCover.
  int content_left = 0;
  int content_top = 0;
  int content_right = image_w;
  int content_bottom = image_h;
  int found_content = 0;
  for (int yy = 0; yy < image_h; yy++) {
    for (int xx = 0; xx < image_w; xx++) {
      const unsigned char value = pixels[yy * image_w + xx];
      if (value > 246) continue;
      if (!found_content) {
        content_left = xx;
        content_right = xx + 1;
        content_top = yy;
        content_bottom = yy + 1;
        found_content = 1;
      } else {
        if (xx < content_left) content_left = xx;
        if (xx + 1 > content_right) content_right = xx + 1;
        if (yy < content_top) content_top = yy;
        if (yy + 1 > content_bottom) content_bottom = yy + 1;
      }
    }
  }
  if (!found_content || content_right <= content_left || content_bottom <= content_top) {
    content_left = 0;
    content_top = 0;
    content_right = image_w;
    content_bottom = image_h;
  }
  const int source_w = content_right - content_left;
  const int source_h = content_bottom - content_top;

  // Fit the entire trimmed image inside the box (contain), letterboxing the
  // overflow dimension. No content is cropped.
  int dst_w = w;
  int dst_h = h;
  if (source_w * h > w * source_h) {
    dst_h = (w * source_h) / source_w;
  } else {
    dst_w = (h * source_w) / source_h;
  }
  const int dst_x = x + (w - dst_w) / 2;
  const int dst_y = y + (h - dst_h) / 2;

  for (int yy = 0; yy < dst_h; yy++) {
    const int source_y = content_top + (yy * source_h) / dst_h;
    if (source_y < 0 || source_y >= image_h) continue;
    for (int xx = 0; xx < dst_w; xx++) {
      const int source_x = content_left + (xx * source_w) / dst_w;
      if (source_x < 0 || source_x >= image_w) continue;
      const unsigned char value = pixels[source_y * image_w + source_x];
      setPixel(dst_x + xx, dst_y + yy, invert ? static_cast<unsigned char>(255 - value) : value);
    }
  }
}

void Canvas::drawRecipeLocalImage(int x, int y, int w, int h, const RecipeRecord* recipe, int invert, PgmCache& cache) {
  char primary[192];
  char fallback[192];
  recipePhotoPath(kRecipeAssetsPath, recipe ? recipe->id : "", primary, sizeof(primary));
  recipePhotoPath(kRecipeAssetsLocalPath, recipe ? recipe->id : "", fallback, sizeof(fallback));
  drawPgmImageCover(x, y, w, h, primary, fallback, invert, cache);
}
