#include "app.h"

// Entry point. All behavior lives in App and the subsystem modules
// (canvas, panels, navigator, touch_registry, input, framebuffer, client,
// json_parser, pgm_cache, eips, util). See panel.h for how to add a new screen.
int main(int argc, char** argv) {
  App app;
  return app.run(argc, argv);
}
