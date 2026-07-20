#ifndef KINDLE_DASHBOARD_PGM_CACHE_H
#define KINDLE_DASHBOARD_PGM_CACHE_H

// Decodes binary (P5) grayscale PGM assets and memoizes the decoded pixel
// buffers keyed by (primary, fallback) path, so repeated renders of the same
// recipe photo / icon avoid re-reading and re-decoding the file. The cache owns
// the decoded buffers; clear() and the destructor free them.
class PgmCache {
 public:
  PgmCache() = default;
  ~PgmCache();

  PgmCache(const PgmCache&) = delete;
  PgmCache& operator=(const PgmCache&) = delete;

  // Returns cached-or-freshly-loaded pixels (owned by the cache — do not free)
  // and writes the dimensions to *width/*height. Returns nullptr on failure.
  const unsigned char* load(const char* primary_path, const char* fallback_path, int* width, int* height);

  // Frees every cached buffer and resets the cache to empty.
  void clear();

 private:
  struct Entry {
    char primary_path[192];
    char fallback_path[192];
    unsigned char* pixels;
    int width;
    int height;
  };

  static constexpr int kCapacity = 32;
  Entry entries_[kCapacity] = {};
  int count_ = 0;
};

#endif  // KINDLE_DASHBOARD_PGM_CACHE_H
