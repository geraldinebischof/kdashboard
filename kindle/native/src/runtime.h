#ifndef KINDLE_DASHBOARD_RUNTIME_H
#define KINDLE_DASHBOARD_RUNTIME_H

#include <signal.h>

// Process-wide, signal-safe flags shared by the signal handler, the touch and
// event-watcher pthreads, and the main loop. Deliberately kept as file-scope
// `volatile sig_atomic_t` (never a class or std::atomic) so writes from the
// signal handler stay async-signal-safe.
extern volatile sig_atomic_t g_running;
extern volatile sig_atomic_t g_event_refresh;
extern volatile sig_atomic_t g_manual_fetch_refresh;

void handleSignal(int signum);

#endif  // KINDLE_DASHBOARD_RUNTIME_H
