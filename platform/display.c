#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include "linux/window-wayland.h"
#include "display.h"

#define WIDTH 800
#define HEIGHT 600

struct win_ctx *g_ctx = NULL;

void window_system_init(void) {
  window_wayland_init();
}

void win_ctx_ops_register(struct win_ctx_ops *ops) {
  assert(ops != NULL);
  g_ctx->ops = ops;
}


int win_ctx_init(void) {
  g_ctx = malloc(sizeof(struct win_ctx));
  if (!g_ctx) {
    return 1;
  }
  window_system_init();
  g_ctx->ctx = g_ctx->ops->ctx_make();
  return 0;
}

int win_ctx_create_window(const char *name, int width, int height) {
  assert(name != NULL);
  return g_ctx->ops->create_window(g_ctx->ctx, name, height, width, width * 4);
}

void win_ctx_close_window(void) {
  g_ctx->ops->close_window(g_ctx->ctx);
}

static void pixel_buffer_init(uint32_t *buf, int height, int width, uint32_t value) {
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if ((x + y / 8 * 8) % 16 < 8) {
	buf[y * width + x] = value;
      } else {
	buf[y * width + x] = 0xFFEEEEEE;
      }
    }
  }
}

void win_context_buffer_draw(int height, int width, uint32_t value) {
  uint32_t *pixels = g_ctx->ops->get_pixel_buffer_ptr(g_ctx->ctx);
  pixel_buffer_init(pixels, height, width, value);
  g_ctx->ops->attach_buffer(g_ctx->ctx, 0, 0);
  g_ctx->ops->commit_buffer(g_ctx->ctx);
}


int win_ctx_poll_events(struct win_ctx *ctx) {
  return g_ctx->ops->poll_events(ctx->ctx);
}

int main(void) {
  int ret = 0;
  ret = win_ctx_init();
  if (ret)
    return 1;
  ret = win_ctx_create_window("helloworld", WIDTH, HEIGHT);
  win_context_buffer_draw(HEIGHT, WIDTH, 0xFF666666);
  while (!g_ctx->ops->window_should_close(g_ctx->ctx)) {
    win_ctx_poll_events(g_ctx);
  }
  //win_ctx_poll_events(g_ctx);
  win_ctx_close_window();
  return ret;
}
