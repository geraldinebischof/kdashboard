#ifndef KINDLE_DASHBOARD_INPUT_H
#define KINDLE_DASHBOARD_INPUT_H

#include "touch_registry.h"

#ifdef __linux__
#include <pthread.h>
#include <signal.h>
#endif

// Reads Linux touch input devices and resolves taps into pending actions on a
// shared TouchRegionRegistry. On non-Linux hosts every method is a no-op stub
// (the platform-specific bodies live behind #ifdef in input.cpp), so the rest
// of the program links and runs unchanged for the local render check.
//
// A single instance is owned by the app; startWatcher() spins up a polling
// pthread that runs until stop() sets running_=0. close() joins the thread and
// only then releases the exclusive EVIOCGRAB and the fds, so a SIGTERM-driven
// shutdown always leaves the kernel input pipeline in a clean state instead of
// wedging the framework's touchscreen routing for the next launch.
class InputManager {
 public:
  // touch: registry to record resolved taps into.
  // screen_width/height: live last-rendered dimensions (updated by the render
  // path) used to scale raw device coordinates.
  InputManager(TouchRegionRegistry& touch, const int& screen_width, const int& screen_height);
  ~InputManager();

  void open();          // discover + grab touch devices
  void close();         // signal watcher, join it, ungrab + close devices
  int poll();           // drain pending events; returns 1 if a tap resolved
  void startWatcher();  // spawn the polling thread

  int deviceCount() const { return count_; }
#ifdef __linux__
  // Read by the watcher thread each loop iteration; flipped to 0 by close().
  bool running() const { return running_ != 0; }
#endif

 private:
#ifdef __linux__
  int applyTouchWithDebounce();
#endif

  struct Device {
    int fd;
    int grabbed;
    int min_x;
    int max_x;
    int min_y;
    int max_y;
    int has_x_range;
    int has_y_range;
    // True when the device reports a real contact signal (BTN_TOUCH or a
    // multitouch tracking id). Such devices arm/disarm resolution per tap; a
    // bare ABS+SYN device has no finger-up signal and is throttled by debounce.
    int has_contact_signal;
  };

  // These are exercised only by the Linux implementations in input.cpp; the
  // non-Linux stub build leaves them unused, hence [[maybe_unused]].
  [[maybe_unused]] Device devices_[16] = {};
  int count_ = 0;
  [[maybe_unused]] int x_ = -1;
  [[maybe_unused]] int y_ = -1;
  [[maybe_unused]] int has_x_ = 0;
  [[maybe_unused]] int has_y_ = 0;
  // Finger is currently down. Cleared on a real lift (BTN_TOUCH up / multitouch
  // tracking-id < 0), or — for devices with no lift signal — by a quiet gap.
  [[maybe_unused]] int in_contact_ = 0;
  // A fresh contact has begun and not yet been resolved. Set only on the
  // up->down transition (NOT on every frame), so a held finger or a driver that
  // re-sends BTN_TOUCH every sync resolves the tap exactly once.
  [[maybe_unused]] int armed_ = 0;
  // Timestamp of the most recent contact-indicating event; drives the synthetic
  // lift used for bare ABS+SYN devices that never report a finger-up.
  [[maybe_unused]] long long last_contact_ms_ = 0;
  // True if ANY opened device reports a real contact signal (BTN_TOUCH / MT).
  [[maybe_unused]] int any_contact_signal_ = 0;
  // Screen orientation locked after the first confirmed tap (index 0..7 into
  // transformForOrientation), or -1 while still unknown. Locking stops empty-
  // space taps from matching a button under a different orientation.
  [[maybe_unused]] int locked_transform_ = -1;
  [[maybe_unused]] long long last_action_ms_ = 0;

#ifdef __linux__
  // Set to 0 by close() before joining watcher_; the watcher loop watches it
  // instead of g_running so the input subsystem shuts down independently of the
  // main loop and before App::run() returns and destroys this object.
  volatile sig_atomic_t running_ = 1;
  pthread_t watcher_thread_ = 0;
  bool watcher_started_ = false;
#endif

  [[maybe_unused]] TouchRegionRegistry& touch_;
  [[maybe_unused]] const int& screen_width_;
  [[maybe_unused]] const int& screen_height_;
};

#endif  // KINDLE_DASHBOARD_INPUT_H
