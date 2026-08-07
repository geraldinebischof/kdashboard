#include "input.h"

#include <stdio.h>
#include <string.h>

#include "constants.h"
#include "runtime.h"  // g_running

#ifdef __linux__
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
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
  }
  return NULL;
}

// Map a raw touchscreen coordinate (x, y) into screen space for orientation
// index t (0..7): the 4 axis-aligned flips followed by the 4 transposed flips.
// The same set the resolver used to probe blindly on every tap; now it is only
// used to (a) find the correct orientation on the first tap and (b) apply the
// locked orientation thereafter.
void transformForOrientation(int t, int x, int y, int w, int h, int* ox, int* oy) {
  const int mx = w - 1 - x;
  const int my = h - 1 - y;
  const int sw = (h > 1) ? static_cast<int>(static_cast<long>(y) * w / h) : 0;
  const int sh = (w > 1) ? static_cast<int>(static_cast<long>(x) * h / w) : 0;
  switch (t) {
    case 0: *ox = x;   *oy = y;   break;
    case 1: *ox = mx;  *oy = y;   break;
    case 2: *ox = x;   *oy = my;  break;
    case 3: *ox = mx;  *oy = my;  break;
    case 4: *ox = sw;  *oy = sh;  break;
    case 5: *ox = w - 1 - sw; *oy = sh;  break;
    case 6: *ox = sw;  *oy = h - 1 - sh; break;
    default: *ox = w - 1 - sw; *oy = h - 1 - sh; break;
  }
}

}  // namespace

int InputManager::applyTouchWithDebounce() {
  // Hold off while the main thread is mid-render (writing the framebuffer and
  // driving the e-ink refresh). Resolving another tap now would queue a second
  // flash+refresh on top of the in-flight one and can wedge the e-ink driver.
  if (touch_.busy) return 0;
  // Don't queue a second action while the main loop still has one pending and
  // unconsumed; rapid taps then collapse to a single dispatch per render.
  if (touch_.pending_action != kTouchNone) return 0;
  const long long now = nowMs();
  if (now - last_action_ms_ < 100) return 0;
  const int w = screen_width_;
  const int h = screen_height_;
  const int x = x_;
  const int y = y_;

  // Every tappable control (including EXIT) is a registered touch region, so we
  // resolve via applyTouchAt() in the correct screen orientation. The
  // orientation is probed once (first confirmed hit) and then locked: trying all
  // 8 transforms on every tap meant a touch on empty space would match a button
  // under some *other* orientation and fire a random action.
  int hit = -1;
  if (locked_transform_ >= 0) {
    int tx, ty;
    transformForOrientation(locked_transform_, x, y, w, h, &tx, &ty);
    if (touch_.applyTouchAt(tx, ty)) hit = locked_transform_;
  } else {
    for (int t = 0; t < 8; t++) {
      int tx, ty;
      transformForOrientation(t, x, y, w, h, &tx, &ty);
      if (touch_.applyTouchAt(tx, ty)) {
        hit = t;
        break;
      }
    }
  }
  if (hit < 0) {
    fprintf(stderr, "input=miss x=%d y=%d width=%d height=%d regions=%d\n", x, y, w, h, touch_.count());
    return 0;
  }
  locked_transform_ = hit;

  touch_.pending_touch_x = x;
  touch_.pending_touch_y = y;
  last_action_ms_ = now;
  // Nudge the wake pipe so the main loop's select() dispatches this tap
  // immediately instead of waiting for its next timer tick.
  if (wake_fd_ >= 0) {
    char c = 1;
    const ssize_t w = ::write(wake_fd_, &c, 1);
    (void)w;  // EAGAIN (pipe full) is harmless: an earlier byte still wakes it
  }
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

    // A device "has a contact signal" if it advertises BTN_TOUCH or reports a
    // multitouch tracking id. We use this to decide whether to gate resolution
    // on a clean finger-down/up (so a held finger resolves once) instead of
    // resolving on every sync frame.
    unsigned char key_bits[64] = {0};
    int has_btn_touch = 0;
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) == 0 &&
        BTN_TOUCH < static_cast<int>(sizeof(key_bits) * 8)) {
      has_btn_touch = (key_bits[BTN_TOUCH / 8] >> (BTN_TOUCH % 8)) & 1;
    }
    int mt_min = 0;
    int mt_max = 0;
    device->has_contact_signal = has_btn_touch || readAbsRange(fd, ABS_MT_TRACKING_ID, &mt_min, &mt_max);

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
    fprintf(stderr, "input=device path=%s grabbed=%d xrange=%d..%d yrange=%d..%d contact=%d\n",
            path, device->grabbed, device->min_x, device->max_x, device->min_y, device->max_y, device->has_contact_signal);
    if (device->has_contact_signal) any_contact_signal_ = 1;
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

  // Synthetic lift for bare ABS+SYN devices that never report a finger-up: if
  // no contact-indicating event has arrived for a short while, treat the finger
  // as lifted so the next contact can arm a fresh tap.
  if (in_contact_ && !any_contact_signal_ && nowMs() - last_contact_ms_ > 150) {
    in_contact_ = 0;
  }

  // Block until at least one device has input ready, instead of burning a fixed
  // delay each loop: the kernel wakes us the instant a touch arrives. A bounded
  // timeout keeps shutdown responsive — close() flips running_ then joins, and
  // join latency is bounded by this value rather than an indefinite block.
  struct pollfd pfds[16];
  int nfds = 0;
  for (int i = 0; i < count_ && nfds < 16; i++) {
    pfds[nfds].fd = devices_[i].fd;
    pfds[nfds].events = POLLIN;
    pfds[nfds].revents = 0;
    nfds++;
  }
  const int ready = ::poll(pfds, nfds, 250);
  if (ready <= 0) return 0;

  for (int i = 0; i < count_; i++) {
    if (!(pfds[i].revents & (POLLIN | POLLERR | POLLHUP))) continue;
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
          last_contact_ms_ = nowMs();
          // Arm only on the up->down transition, not on every coordinate frame.
          if (!in_contact_) { in_contact_ = 1; armed_ = 1; }
        } else if (event.code == ABS_Y || event.code == ABS_MT_POSITION_Y) {
          y_ = scaleAbsValue(event.value, device->min_y, device->max_y, screen_height_);
          has_y_ = 1;
          last_contact_ms_ = nowMs();
          if (!in_contact_) { in_contact_ = 1; armed_ = 1; }
        } else if (event.code == ABS_MT_TRACKING_ID) {
          last_contact_ms_ = nowMs();
          if (event.value >= 0) {
            if (!in_contact_) { in_contact_ = 1; armed_ = 1; }  // new multitouch contact
          } else {
            in_contact_ = 0;  // contact ended
          }
        }
      } else if (event.type == EV_KEY && (event.code == BTN_TOUCH || event.code == BTN_LEFT)) {
        last_contact_ms_ = nowMs();
        if (event.value > 0) {
          // Arm one resolution on the genuine up->down transition only. This
          // ignores drivers that re-send BTN_TOUCH on every sync frame while
          // held, which previously re-dispatched the action repeatedly.
          if (!in_contact_) { in_contact_ = 1; armed_ = 1; }
        } else {
          in_contact_ = 0;  // finger lifted
          if (armed_ && has_x_ && has_y_ && applyTouchWithDebounce()) {
            armed_ = 0;
            fprintf(stderr, "input=action tap action=%d x=%d y=%d\n", static_cast<int>(touch_.pending_action), x_, y_);
            return 1;
          }
        }
      } else if (event.type == EV_SYN && armed_ && has_x_ && has_y_) {
        // Resolve at most once per contact: the arm set on the up->down
        // transition is consumed here, so a held finger never re-dispatches
        // the action (and thus can't drift onto a neighbouring button).
        if (applyTouchWithDebounce()) {
          armed_ = 0;
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
