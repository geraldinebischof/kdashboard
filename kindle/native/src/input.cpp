#include "input.h"

#include <stdio.h>
#include <string.h>

#include "constants.h"
#include "panel.h"    // exitButtonRectForScreen
#include "runtime.h"  // g_running

#ifdef __linux__
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#endif

InputManager::InputManager(TouchRegionRegistry& touch, const int& screen_width, const int& screen_height)
    : touch_(touch), screen_width_(screen_width), screen_height_(screen_height) {}

#ifdef __linux__

namespace {

long long nowMs() {
  timeval tv;
  gettimeofday(&tv, NULL);
  // Cast to 64-bit before the multiply: on 32-bit targets (arm-linux-musleabi)
  // `long` is 32 bits, so tv.tv_sec * 1000L overflows (~1.7e9 s * 1000 wraps to
  // a negative int32), which makes the debounce window check always true and
  // silently rejects every tap.
  return static_cast<long long>(tv.tv_sec) * 1000LL + static_cast<long long>(tv.tv_usec / 1000);
}

int readAbsRange(int fd, int code, int* minimum, int* maximum) {
  input_absinfo abs_info;
  memset(&abs_info, 0, sizeof(abs_info));
  if (ioctl(fd, EVIOCGABS(code), &abs_info) != 0) return 0;
  if (abs_info.maximum <= abs_info.minimum) return 0;
  *minimum = abs_info.minimum;
  *maximum = abs_info.maximum;
  return 1;
}

int scaleAbsValue(int value, int minimum, int maximum, int screen_size) {
  if (maximum <= minimum || screen_size <= 1) return value;
  long scaled = (static_cast<long>(value - minimum) * static_cast<long>(screen_size - 1)) / static_cast<long>(maximum - minimum);
  if (scaled < 0) scaled = 0;
  if (scaled >= screen_size) scaled = screen_size - 1;
  return static_cast<int>(scaled);
}

void* watcherMain(void* raw) {
  InputManager* self = static_cast<InputManager*>(raw);
  if (!self) return NULL;
  while (self->running()) {
    self->poll();
    usleep(250000);
  }
  return NULL;
}

}  // namespace

int InputManager::applyTouchWithDebounce() {
  const long long now = nowMs();
  if (now - last_action_ms_ < 700) return 0;
  const int w = screen_width_;
  const int h = screen_height_;
  const int x = x_;
  const int y = y_;

  if (x >= w - 280 && y >= kKindleStatusBarHeight && y <= kKindleStatusBarHeight + 160) {
    touch_.pending_action = kTouchExit;
    touch_.setPendingRect(exitButtonRectForScreen(w, h));
  } else if (!touch_.applyTouchAt(x, y) &&
             !touch_.applyTouchAt(w - 1 - x, y) &&
             !touch_.applyTouchAt(x, h - 1 - y) &&
             !touch_.applyTouchAt(w - 1 - x, h - 1 - y) &&
             !touch_.applyTouchAt((static_cast<long>(y) * w) / (h > 1 ? h : 1), (static_cast<long>(x) * h) / (w > 1 ? w : 1)) &&
             !touch_.applyTouchAt(w - 1 - (static_cast<long>(y) * w) / (h > 1 ? h : 1), (static_cast<long>(x) * h) / (w > 1 ? w : 1)) &&
             !touch_.applyTouchAt((static_cast<long>(y) * w) / (h > 1 ? h : 1), h - 1 - (static_cast<long>(x) * h) / (w > 1 ? w : 1)) &&
             !touch_.applyTouchAt(w - 1 - (static_cast<long>(y) * w) / (h > 1 ? h : 1), h - 1 - (static_cast<long>(x) * h) / (w > 1 ? w : 1))) {
    fprintf(stderr, "input=miss x=%d y=%d width=%d height=%d regions=%d\n", x, y, w, h, touch_.count());
    return 0;
  }
  touch_.pending_touch_x = x;
  touch_.pending_touch_y = y;
  last_action_ms_ = now;
  return 1;
}

void InputManager::open() {
  x_ = -1;
  y_ = -1;
  for (int i = 0; i < 16 && count_ < 16; i++) {
    char path[48];
    snprintf(path, sizeof(path), "/dev/input/event%d", i);
    int fd = ::open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) continue;

    Device* device = &devices_[count_];
    memset(device, 0, sizeof(*device));
    device->fd = fd;
    device->has_x_range = readAbsRange(fd, ABS_X, &device->min_x, &device->max_x) ||
                          readAbsRange(fd, ABS_MT_POSITION_X, &device->min_x, &device->max_x);
    device->has_y_range = readAbsRange(fd, ABS_Y, &device->min_y, &device->max_y) ||
                          readAbsRange(fd, ABS_MT_POSITION_Y, &device->min_y, &device->max_y);

    if (!device->has_x_range || !device->has_y_range) {
      ::close(fd);
      continue;
    }

    // EVIOCGRAB gives us exclusive ownership of the touchscreen so the Kindle
    // framework doesn't also page-forward/back on every tap. Default on; set
    // KINDLE_DASHBOARD_EXCLUSIVE_GRAB=0 to fall back to shared (non-exclusive)
    // access if a firmware ever stops delivering events to a grabbed reader.
    const char* grab_env = getenv("KINDLE_DASHBOARD_EXCLUSIVE_GRAB");
    const int want_grab = !(grab_env && grab_env[0] == '0');
    if (want_grab) {
      if (ioctl(fd, EVIOCGRAB, 1) == 0) {
        device->grabbed = 1;
      } else {
        fprintf(stderr, "input=grab_failed path=%s errno=%d; continuing non-exclusive\n", path, errno);
      }
    }
    fprintf(stderr, "input=device path=%s grabbed=%d xrange=%d..%d yrange=%d..%d\n",
            path, device->grabbed, device->min_x, device->max_x, device->min_y, device->max_y);
    count_++;
  }
  fprintf(stderr, "input=opened count=%d\n", count_);
}

void InputManager::close() {
  // Tell the watcher to stop, then join it so it is not mid-read() (and not
  // touching devices_/touch_) when we release grabs and close fds below.
  running_ = 0;
  if (watcher_started_) {
    pthread_join(watcher_thread_, NULL);
    watcher_started_ = false;
  }
  for (int i = 0; i < count_; i++) {
    if (devices_[i].grabbed) ioctl(devices_[i].fd, EVIOCGRAB, 0);
    ::close(devices_[i].fd);
  }
  count_ = 0;
  fprintf(stderr, "input=closed\n");
}

int InputManager::poll() {
  if (count_ <= 0) return 0;
  for (int i = 0; i < count_; i++) {
    Device* device = &devices_[i];
    while (1) {
      input_event event;
      const ssize_t bytes = read(device->fd, &event, sizeof(event));
      if (bytes != sizeof(event)) {
        // Keep the fd open; intermittent event read errors are not fatal to rendering.
        break;
      }

      if (event.type == EV_ABS) {
        if (event.code == ABS_X || event.code == ABS_MT_POSITION_X) {
          x_ = scaleAbsValue(event.value, device->min_x, device->max_x, screen_width_);
          has_x_ = 1;
          was_down_ = 1;
        } else if (event.code == ABS_Y || event.code == ABS_MT_POSITION_Y) {
          y_ = scaleAbsValue(event.value, device->min_y, device->max_y, screen_height_);
          has_y_ = 1;
          was_down_ = 1;
        } else if (event.code == ABS_MT_TRACKING_ID) {
          was_down_ = event.value >= 0 ? 1 : 0;
        }
      } else if (event.type == EV_KEY && (event.code == BTN_TOUCH || event.code == BTN_LEFT)) {
        if (event.value > 0) was_down_ = 1;
        if (event.value == 0 && was_down_ && has_x_ && has_y_) {
          was_down_ = 0;
          if (applyTouchWithDebounce()) {
            fprintf(stderr, "input=action tap action=%d x=%d y=%d\n", static_cast<int>(touch_.pending_action), x_, y_);
            return 1;
          }
        }
      } else if (event.type == EV_SYN && has_x_ && has_y_) {
        if (applyTouchWithDebounce()) {
          fprintf(stderr, "input=action touch action=%d x=%d y=%d\n", static_cast<int>(touch_.pending_action), x_, y_);
          return 1;
        }
      }
    }
  }
  return 0;
}

void InputManager::startWatcher() {
  if (count_ <= 0) return;
  if (watcher_started_) return;
  if (pthread_create(&watcher_thread_, NULL, watcherMain, this) != 0) {
    fprintf(stderr, "input=thread_failed\n");
    return;
  }
  watcher_started_ = true;
  fprintf(stderr, "input=thread_started\n");
}

InputManager::~InputManager() {
  close();
}

#else  // !__linux__

InputManager::~InputManager() {}
void InputManager::open() {}
void InputManager::close() {}
int InputManager::poll() { return 0; }
void InputManager::startWatcher() {}

#endif
