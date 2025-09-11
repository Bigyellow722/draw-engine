#include <assert.h>

#include "window-wayland.h"

#define WIDTH 800
#define HEIGHT 600


int win_context_draw(struct window_context *ctx, uint32_t value) {
  assert(ctx);
  for (int y = 0; y < ctx->height; ++y) {
    for (int x = 0; x < ctx->width; ++x) {
      if ((x + y / 8 * 8) % 16 < 8) {
	ctx->pixels[y * ctx->width + x] = value;
      } else {
	ctx->pixels[y * ctx->width + x] = 0xFFEEEEEE;
      }
    }
  }
  return 0 ;
}

void win_buffer_commit(struct window_context *ctx) {
  wl_surface_attach(ctx->wayland_ctx->surface, ctx->buffer, 0, 0);
  wl_surface_damage(ctx->wayland_ctx->surface, 0, 0, UINT32_MAX, UINT32_MAX);
  wl_surface_commit(ctx->wayland_ctx->surface);
}

int main(void) {
  int ret = 0;
  struct wayland_context *g_ctx = NULL;
  struct window_context *win_ctx = NULL;
  g_ctx = wayland_ctx_make();
  ret = wayland_ctx_setup(g_ctx);
  win_ctx = win_ctx_make(g_ctx, WIDTH, HEIGHT);
  ret = win_context_setup(win_ctx);

  win_context_draw(win_ctx, 0xFF666666);
  win_buffer_commit(win_ctx);
  while (wl_display_dispatch(win_ctx->wayland_ctx->display)) {
    /* This space deliberately left blank */

  }
  win_context_cleanup(win_ctx);
  win_ctx_free(&win_ctx);
  wayland_ctx_cleanup(g_ctx);
  wayland_ctx_free(&g_ctx);
  return ret;
}
