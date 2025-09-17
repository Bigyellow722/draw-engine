#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include "linux/window-wayland.h"
#include "display.h"

#define WIDTH 800
#define HEIGHT 600


struct win_ctx_ops *g_win_ctx_ops = NULL;

void window_system_init(void) {
  window_wayland_init();
}

void win_ctx_ops_register(struct win_ctx_ops *ops) {
  assert(ops != NULL);
  g_win_ctx_ops = ops;
}


struct window_context *win_ctx_make(const char* name, int width,
                                    int height) {
  assert(name != NULL);
  struct window_context *new = NULL;
  new = malloc(sizeof(struct window_context));
  if (!new) {
    return NULL;
  }
  new->width = width;
  new->height = height;
  new->stride = width * 4;
  new->priv = g_win_ctx_ops->priv_make(name, height, width, width * 4);
  return new;
}

void win_ctx_free(struct window_context** pwin_ctx) {
  struct window_context *win_ctx = *pwin_ctx;
  if (win_ctx) {
    g_win_ctx_ops->priv_free(&win_ctx->priv);
    free(win_ctx);
    win_ctx = NULL;
  }
}


int win_context_setup(struct window_context *ctx) {
  int ret = 0;
  ret = g_win_ctx_ops->priv_setup(ctx->priv);
  return ret;
}

void win_context_cleanup(struct window_context *ctx) {
  g_win_ctx_ops->priv_cleanup(ctx->priv);
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

void win_context_buffer_draw(struct window_context *ctx, void* bitmap, int len) {
  g_win_ctx_ops->fill_buffer(ctx->priv, bitmap, len);
  g_win_ctx_ops->commit_buffer(ctx->priv);
}

void win_ctx_sync(struct window_context *ctx) {
  g_win_ctx_ops->sync(ctx->priv);
}

int main(void) {
  int ret = 0;
  window_system_init();
  struct window_context *win_ctx = NULL;
  win_ctx = win_ctx_make("helloworld", WIDTH, HEIGHT);
  ret = win_context_setup(win_ctx);
  if (ret)
    return 1;
  uint32_t *pixels = malloc(win_ctx->height * win_ctx->stride);
  if (pixels) {
    memset(pixels, 0, win_ctx->height * win_ctx->stride);
    pixel_buffer_init(pixels, win_ctx->height, win_ctx->width, 0xFF666666);
    win_context_buffer_draw(win_ctx, pixels, win_ctx->height * win_ctx->stride);
    win_ctx_sync(win_ctx);
    printf("0000000000\n");
  }
  win_context_cleanup(win_ctx);
  win_ctx_free(&win_ctx);
  return ret;
}
