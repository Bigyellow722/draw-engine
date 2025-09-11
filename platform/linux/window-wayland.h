#ifndef _WINDOW_WAYLAND_H_
#define _WINDOW_WAYLAND_H_


#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"


struct wayland_context {
  /* Global objects */
  struct wl_display *display;
  struct wl_registry *registry;
  struct wl_shm *shm; // provide a format interface to set pixel format
  void *shm_data;
  struct wl_compositor *compositor;
  struct xdg_wm_base *xdg_wm_base;
  struct wl_seat *seat; // for handling events from input source, such as keyboard, pointer, touch and so on
  /* the context of surface */
  struct wl_surface *surface;
  struct xdg_surface *xdg_surface;
  struct xdg_toplevel *xdg_toplevel;
  bool configured;
  bool should_close;
};

struct wayland_context *wayland_ctx_make(void);
void wayland_ctx_free(struct wayland_context **pctx);
int wayland_ctx_setup(struct wayland_context *ctx);
void wayland_ctx_cleanup(struct wayland_context *ctx);

struct window_context {
  struct wayland_context *wayland_ctx;
  int width;
  int height;
  int stride;
  /* pixel related struct */
  int fd;
  uint8_t *pool_data;
  int shm_pool_size;
  struct wl_shm_pool *pool;
  int index;
  int offset;
  struct wl_buffer *buffer;
  uint32_t *pixels;
};

struct window_context *win_ctx_make(struct wayland_context *ctx, int width,
                                    int height);
void win_ctx_free(struct window_context **pwin_ctx);
int win_context_setup(struct window_context *ctx);
void win_context_cleanup(struct window_context *ctx);

#endif
