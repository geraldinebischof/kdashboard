#ifndef KINDLE_DASHBOARD_OPTIONS_H
#define KINDLE_DASHBOARD_OPTIONS_H

#include "model.h"

// Populate `options` with defaults.
void initOptions(Options* options);

// Parse argv into `options`. Returns 1 on success, 0 on a usage error (message
// already printed). Exits the process on --help.
int parseOptions(int argc, char** argv, Options* options);

#endif  // KINDLE_DASHBOARD_OPTIONS_H
