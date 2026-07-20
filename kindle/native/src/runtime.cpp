#include "runtime.h"

volatile sig_atomic_t g_running = 1;
volatile sig_atomic_t g_event_refresh = 0;
volatile sig_atomic_t g_manual_fetch_refresh = 0;

void handleSignal(int) {
  g_running = 0;
}
