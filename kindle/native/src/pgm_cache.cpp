#include "pgm_cache.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

namespace {

int readPgmToken(FILE* file, char* out, size_t out_size) {
  if (!file || !out || out_size == 0) return 0;
  int ch = 0;
  do {
    ch = fgetc(file);
    if (ch == '#') {
      while (ch != EOF && ch != '\n') ch = fgetc(file);
    }
  } while (ch != EOF && isspace(ch));
  if (ch == EOF) return 0;

  size_t index = 0;
  while (ch != EOF && !isspace(ch)) {
    if (index + 1 < out_size) out[index++] = static_cast<char>(ch);
    ch = fgetc(file);
  }
  out[index] = '\0';
  return index > 0;
}

unsigned char* loadPgmPixels(const char* primary_path, const char* fallback_path, int* width, int* height) {
  FILE* file = fopen(primary_path, "rb");
  if (!file && fallback_path) file = fopen(fallback_path, "rb");
  if (!file) return NULL;

  char token[32];
  if (!readPgmToken(file, token, sizeof(token)) || strcmp(token, "P5") != 0) {
    fclose(file);
    return NULL;
  }
  if (!readPgmToken(file, token, sizeof(token))) {
    fclose(file);
    return NULL;
  }
  const int image_w = atoi(token);
  if (!readPgmToken(file, token, sizeof(token))) {
    fclose(file);
    return NULL;
  }
  const int image_h = atoi(token);
  if (!readPgmToken(file, token, sizeof(token))) {
    fclose(file);
    return NULL;
  }
  const int max_value = atoi(token);
  if (image_w <= 0 || image_h <= 0 || max_value <= 0 || max_value > 255) {
    fclose(file);
    return NULL;
  }

  const size_t size = static_cast<size_t>(image_w) * static_cast<size_t>(image_h);
  unsigned char* pixels = static_cast<unsigned char*>(malloc(size));
  if (!pixels) {
    fclose(file);
    return NULL;
  }
  if (fread(pixels, 1, size, file) != size) {
    free(pixels);
    fclose(file);
    return NULL;
  }
  fclose(file);
  *width = image_w;
  *height = image_h;
  return pixels;
}

}  // namespace

PgmCache::~PgmCache() {
  clear();
}

const unsigned char* PgmCache::load(const char* primary_path, const char* fallback_path, int* width, int* height) {
  for (int i = 0; i < count_; i++) {
    Entry* cached = &entries_[i];
    if (strcmp(cached->primary_path, primary_path ? primary_path : "") == 0 &&
        strcmp(cached->fallback_path, fallback_path ? fallback_path : "") == 0) {
      *width = cached->width;
      *height = cached->height;
      return cached->pixels;
    }
  }

  const long long started = util::monotonicMs();
  unsigned char* pixels = loadPgmPixels(primary_path, fallback_path, width, height);
  fprintf(stderr, "timing=image-load path=%s ok=%d ms=%lld\n", primary_path ? primary_path : "", pixels ? 1 : 0, util::monotonicMs() - started);
  if (!pixels) return NULL;
  if (count_ >= kCapacity) return pixels;

  Entry* cached = &entries_[count_++];
  util::copyText(cached->primary_path, sizeof(cached->primary_path), primary_path ? primary_path : "");
  util::copyText(cached->fallback_path, sizeof(cached->fallback_path), fallback_path ? fallback_path : "");
  cached->pixels = pixels;
  cached->width = *width;
  cached->height = *height;
  return cached->pixels;
}

void PgmCache::clear() {
  for (int i = 0; i < count_; i++) {
    free(entries_[i].pixels);
    entries_[i].pixels = NULL;
  }
  count_ = 0;
}
