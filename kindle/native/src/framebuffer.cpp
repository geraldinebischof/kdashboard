#include "framebuffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "constants.h"

#ifdef __linux__
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#endif

int writePgm(const char* path, const Canvas* canvas) {
  FILE* file = fopen(path, "wb");
  if (!file) return 0;
  fprintf(file, "P5\n%d %d\n255\n", canvas->width, canvas->height);
  const size_t bytes = static_cast<size_t>(canvas->width) * static_cast<size_t>(canvas->height);
  const int ok = fwrite(canvas->pixels, 1, bytes, file) == bytes;
  fclose(file);
  return ok;
}

#ifdef __linux__

namespace {

void putFramebufferPixel(unsigned char* fb, const fb_var_screeninfo* vinfo, const fb_fix_screeninfo* finfo, int x, int y, unsigned char gray) {
  if (x < 0 || y < 0 || x >= static_cast<int>(vinfo->xres) || y >= static_cast<int>(vinfo->yres)) return;
  if (vinfo->bits_per_pixel == 4) {
    const long location = static_cast<long>(y + vinfo->yoffset) * finfo->line_length +
                          static_cast<long>(x + vinfo->xoffset) / 2;
    const unsigned char nibble = static_cast<unsigned char>(gray >> 4);
    if (((x + vinfo->xoffset) & 1) == 0) {
      fb[location] = static_cast<unsigned char>((fb[location] & 0x0f) | (nibble << 4));
    } else {
      fb[location] = static_cast<unsigned char>((fb[location] & 0xf0) | nibble);
    }
    return;
  }

  if (vinfo->bits_per_pixel == 1) {
    const long location = static_cast<long>(y + vinfo->yoffset) * finfo->line_length +
                          static_cast<long>(x + vinfo->xoffset) / 8;
    const unsigned char mask = static_cast<unsigned char>(0x80 >> ((x + vinfo->xoffset) & 7));
    if (gray < 128) fb[location] &= static_cast<unsigned char>(~mask);
    else fb[location] |= mask;
    return;
  }

  const long location = static_cast<long>(x + vinfo->xoffset) * (vinfo->bits_per_pixel / 8) +
                        static_cast<long>(y + vinfo->yoffset) * finfo->line_length;
  if (vinfo->bits_per_pixel == 8) {
    fb[location] = gray;
  } else if (vinfo->bits_per_pixel == 16) {
    const unsigned short value = static_cast<unsigned short>(((gray >> 3) << 11) | ((gray >> 2) << 5) | (gray >> 3));
    memcpy(fb + location, &value, sizeof(value));
  } else if (vinfo->bits_per_pixel == 32) {
    const unsigned int value = 0xff000000u | (static_cast<unsigned int>(gray) << 16) | (static_cast<unsigned int>(gray) << 8) | gray;
    memcpy(fb + location, &value, sizeof(value));
  }
}

unsigned char getFramebufferPixel(unsigned char* fb, const fb_var_screeninfo* vinfo, const fb_fix_screeninfo* finfo, int x, int y) {
  if (x < 0 || y < 0 || x >= static_cast<int>(vinfo->xres) || y >= static_cast<int>(vinfo->yres)) return 255;

  if (vinfo->bits_per_pixel == 4) {
    const long location = static_cast<long>(y + vinfo->yoffset) * finfo->line_length +
                          static_cast<long>(x + vinfo->xoffset) / 2;
    const unsigned char value = fb[location];
    const unsigned char nibble = ((x + vinfo->xoffset) & 1) == 0
      ? static_cast<unsigned char>(value >> 4)
      : static_cast<unsigned char>(value & 0x0f);
    return static_cast<unsigned char>(nibble * 17);
  }

  if (vinfo->bits_per_pixel == 1) {
    const long location = static_cast<long>(y + vinfo->yoffset) * finfo->line_length +
                          static_cast<long>(x + vinfo->xoffset) / 8;
    const unsigned char mask = static_cast<unsigned char>(0x80 >> ((x + vinfo->xoffset) & 7));
    return (fb[location] & mask) ? 255 : 0;
  }

  const long location = static_cast<long>(x + vinfo->xoffset) * (vinfo->bits_per_pixel / 8) +
                        static_cast<long>(y + vinfo->yoffset) * finfo->line_length;
  if (vinfo->bits_per_pixel == 8) return fb[location];
  if (vinfo->bits_per_pixel == 16) {
    unsigned short value = 0;
    memcpy(&value, fb + location, sizeof(value));
    const int red = ((value >> 11) & 31) * 255 / 31;
    const int green = ((value >> 5) & 63) * 255 / 63;
    const int blue = (value & 31) * 255 / 31;
    return static_cast<unsigned char>((red + green + blue) / 3);
  }
  if (vinfo->bits_per_pixel == 32) {
    const unsigned char red = fb[location + 2];
    const unsigned char green = fb[location + 1];
    const unsigned char blue = fb[location];
    return static_cast<unsigned char>((static_cast<int>(red) + green + blue) / 3);
  }
  return 255;
}

void invertFramebufferArea(unsigned char* fb, const fb_var_screeninfo* vinfo, const fb_fix_screeninfo* finfo, int left, int top, int right, int bottom, int inset) {
  for (int y = top; y < bottom; y++) {
    for (int x = left; x < right; x++) {
      if (inset > 0 && (x < left + inset || x >= right - inset || y < top + inset || y >= bottom - inset)) continue;
      const unsigned char current = getFramebufferPixel(fb, vinfo, finfo, x, y);
      putFramebufferPixel(fb, vinfo, finfo, x, y, static_cast<unsigned char>(255 - current));
    }
  }
}

}  // namespace

void FramebufferRenderer::flashRect(Rect rect) {
  if (rect.w <= 0 || rect.h <= 0) return;
  if (rect.y < kKindleStatusBarHeight) {
    const int shift = kKindleStatusBarHeight - rect.y;
    rect.y += shift;
    rect.h -= shift;
  }
  if (rect.w <= 0 || rect.h <= 0) return;

  int fd = open("/dev/fb0", O_RDWR);
  if (fd < 0) {
    fprintf(stderr, "visual-feedback=framebuffer open_failed\n");
    return;
  }

  fb_var_screeninfo vinfo;
  fb_fix_screeninfo finfo;
  if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) != 0 || ioctl(fd, FBIOGET_FSCREENINFO, &finfo) != 0) {
    fprintf(stderr, "visual-feedback=framebuffer ioctl_failed\n");
    close(fd);
    return;
  }

  const long screensize = static_cast<long>(finfo.line_length) * static_cast<long>(vinfo.yres_virtual ? vinfo.yres_virtual : vinfo.yres);
  unsigned char* fb = static_cast<unsigned char*>(mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
  if (fb == MAP_FAILED) {
    fprintf(stderr, "visual-feedback=framebuffer mmap_failed\n");
    close(fd);
    return;
  }

  const int left = rect.x < 0 ? 0 : rect.x;
  const int top = rect.y < kKindleStatusBarHeight ? kKindleStatusBarHeight : rect.y;
  const int right = rect.x + rect.w > static_cast<int>(vinfo.xres) ? static_cast<int>(vinfo.xres) : rect.x + rect.w;
  const int bottom = rect.y + rect.h > static_cast<int>(vinfo.yres) ? static_cast<int>(vinfo.yres) : rect.y + rect.h;
  invertFramebufferArea(fb, &vinfo, &finfo, left, top, right, bottom, 0);
  system("eips '' >/dev/null 2>&1 || true");
  usleep(120000);
  invertFramebufferArea(fb, &vinfo, &finfo, left, top, right, bottom, 0);
  munmap(fb, screensize);
  close(fd);
  system("eips '' >/dev/null 2>&1 || true");
  fprintf(stderr, "visual-feedback=blink rect=%d,%d,%d,%d\n", rect.x, rect.y, rect.w, rect.h);
}

int FramebufferRenderer::render(Navigator& navigator, const Dashboard& dashboard, const char* status, RenderContext& ctx, const char* save_pgm, int& last_width, int& last_height) {
  int fd = open("/dev/fb0", O_RDWR);
  if (fd < 0) {
    fprintf(stderr, "render=framebuffer open_failed\n");
    return 0;
  }
  fb_var_screeninfo vinfo;
  fb_fix_screeninfo finfo;
  if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) != 0 || ioctl(fd, FBIOGET_FSCREENINFO, &finfo) != 0) {
    fprintf(stderr, "render=framebuffer ioctl_failed\n");
    close(fd);
    return 0;
  }
  if (vinfo.bits_per_pixel != 1 && vinfo.bits_per_pixel != 4 && vinfo.bits_per_pixel != 8 &&
      vinfo.bits_per_pixel != 16 && vinfo.bits_per_pixel != 32) {
    fprintf(stderr, "render=framebuffer unsupported_bpp width=%d height=%d bpp=%d line=%d\n",
            static_cast<int>(vinfo.xres), static_cast<int>(vinfo.yres), static_cast<int>(vinfo.bits_per_pixel), static_cast<int>(finfo.line_length));
    close(fd);
    return 0;
  }
  const long screensize = static_cast<long>(finfo.line_length) * static_cast<long>(vinfo.yres_virtual ? vinfo.yres_virtual : vinfo.yres);
  unsigned char* fb = static_cast<unsigned char*>(mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
  if (fb == MAP_FAILED) {
    fprintf(stderr, "render=framebuffer mmap_failed\n");
    close(fd);
    return 0;
  }

  Canvas canvas;
  canvas.width = static_cast<int>(vinfo.xres);
  canvas.height = static_cast<int>(vinfo.yres);
  canvas.pixels = static_cast<unsigned char*>(calloc(static_cast<size_t>(canvas.width) * static_cast<size_t>(canvas.height), 1));
  if (!canvas.pixels) {
    fprintf(stderr, "render=framebuffer alloc_failed\n");
    munmap(fb, screensize);
    close(fd);
    return 0;
  }
  last_width = canvas.width;
  last_height = canvas.height;
  navigator.render(canvas, dashboard, status, ctx);
  if (save_pgm && save_pgm[0]) {
    writePgm(save_pgm, &canvas);
    fprintf(stderr, "render=save-pgm %s width=%d height=%d\n", save_pgm, canvas.width, canvas.height);
  }
  for (int y = kKindleStatusBarHeight; y < canvas.height; y++) {
    for (int x = 0; x < canvas.width; x++) putFramebufferPixel(fb, &vinfo, &finfo, x, y, canvas.pixels[y * canvas.width + x]);
  }
  free(canvas.pixels);
  munmap(fb, screensize);
  close(fd);
  system("eips '' >/dev/null 2>&1 || true");
  fprintf(stderr, "render=framebuffer ok width=%d height=%d bpp=%d\n", static_cast<int>(vinfo.xres), static_cast<int>(vinfo.yres), static_cast<int>(vinfo.bits_per_pixel));
  return 1;
}

#else  // !__linux__

void FramebufferRenderer::flashRect(Rect) {}

int FramebufferRenderer::render(Navigator&, const Dashboard&, const char*, RenderContext&, const char*, int&, int&) {
  fprintf(stderr, "render=framebuffer unavailable\n");
  return 0;
}

#endif

void FramebufferRenderer::showTouchVisualFeedback(TouchAction action, int x, int y, int pending_rect_valid, Rect pending_rect) {
  if (action == kTouchNone) return;
  fprintf(stderr, "visual-feedback=tap action=%d x=%d y=%d\n", static_cast<int>(action), x, y);
  if (pending_rect_valid) flashRect(pending_rect);
}

void FramebufferRenderer::returnToKindleHome() {
  fprintf(stderr, "exit=return-home\n");
  system(
    "lipc-set-prop com.lab126.powerd preventScreenSaver 0 >/dev/null 2>&1 || true; "
    "lipc-set-prop com.lab126.appmgrd start app://com.lab126.booklet.home >/dev/null 2>&1 || "
    "lipc-set-prop com.lab126.appmgrd start app://com.lab126.booklet.home/ >/dev/null 2>&1 || true; "
    "sleep 1; "
    "eips '' >/dev/null 2>&1 || true"
  );
}
