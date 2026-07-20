#include "eips.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

namespace {

void fit(char* line) {
  size_t length = strlen(line);
  for (size_t i = 0; i < length; i++) {
    if (line[i] == '\n' || line[i] == '\r' || line[i] == '\t') line[i] = ' ';
  }
  if (length > static_cast<size_t>(kScreenColumns)) {
    line[kScreenColumns - 3] = '.';
    line[kScreenColumns - 2] = '.';
    line[kScreenColumns - 1] = '.';
    line[kScreenColumns] = '\0';
  }
}

}  // namespace

void EipsRenderer::reset() {
  count_ = 0;
}

void EipsRenderer::addLine(const char* text) {
  if (count_ >= kMaxRows) return;
  util::copyText(lines_[count_], 96, text);
  fit(lines_[count_]);
  count_++;
}

void EipsRenderer::addRule() {
  addLine("+--------------------------------------+");
}

void EipsRenderer::addCardText(const char* text) {
  char line[96];
  char clipped[64];
  util::copyText(clipped, sizeof(clipped), text);
  clipped[kCardInnerWidth] = '\0';
  snprintf(line, sizeof(line), "| %-36s |", clipped);
  addLine(line);
}

void EipsRenderer::addSectionTitle(const char* title) {
  char upper[48];
  util::upperCopy(upper, sizeof(upper), title);
  char text[64];
  snprintf(text, sizeof(text), " %s", upper);
  addCardText(text);
}

void EipsRenderer::flush() const {
  if (!util::commandExists("eips")) {
    for (int i = 0; i < count_; i++) printf("%s\n", lines_[i]);
    fflush(stdout);
    return;
  }
  for (int i = 0; i < count_; i++) {
    char quoted[180];
    char command[240];
    util::shellQuote(lines_[i], quoted, sizeof(quoted));
    snprintf(command, sizeof(command), "eips 1 %d %s >/dev/null 2>&1", i + 3, quoted);
    system(command);
  }
}
