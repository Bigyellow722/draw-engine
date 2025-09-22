#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>

#include "../display.h"
#include "window-wayland.h"
#include "../utils/utils.h"
#include "shm.h"
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
  /* the context of buffer */
  int fd; // shm fd
  uint8_t *pool_data; // shm buffer pool
  int offset; //
  int shm_pool_size; // move it to wayland context
  struct wl_shm_pool *pool; // move it to wayland context
  struct wl_buffer *buffer; // move it to wayland context
  uint32_t *pixels;         // move it to wayland context
  const char* name;
  /* states */
  bool configured;
  bool should_close;
};


/* xdg interfaces */
/* The client need to send back a pong request */
static void xdg_wm_base_handle_ping(void *data, struct xdg_wm_base *xdg_wm_base,
                                    uint32_t serial) {
  xdg_wm_base_pong(xdg_wm_base, serial);
}


static const struct xdg_wm_base_listener xdg_wm_base_listener = {
  .ping = xdg_wm_base_handle_ping,
};


static void xdg_surface_handle_configure(void *data,
                                         struct xdg_surface *xdg_surface,
                                         uint32_t serial) {
  struct wayland_context *ctx = (struct wayland_context *)data;
  xdg_surface_ack_configure(xdg_surface, serial);
  if (ctx->configured) {
    wl_surface_commit(ctx->surface);
  } else {
    log("recv the first configure event\n");
  }
  ctx->configured = true;
}

static const struct xdg_surface_listener xdg_surface_listener = {
  .configure = xdg_surface_handle_configure,
};

static void xdg_toplevel_handle_configure(void *data,
                                          struct xdg_toplevel *xdg_toplevel,
                                          int32_t width, int32_t height,
                                          struct wl_array *states) {}

static void xdg_toplevel_handle_close(void *data,
                                      struct xdg_toplevel *xdg_toplevel) {
  struct wayland_context *ctx = (struct wayland_context *)data;
  ctx->should_close = true;
}


static const struct xdg_toplevel_listener xdg_toplevel_listener = {
  .configure = xdg_toplevel_handle_configure,
  .close = xdg_toplevel_handle_close,
};


/* callbacks for registry */
static void handle_global(void *data, struct wl_registry *registry,
                          uint32_t name, const char *interface,
                          uint32_t version) {
  struct wayland_context *ctx = (struct wayland_context *)data;

  if (strcmp(interface, wl_shm_interface.name) == 0) {
    ctx->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
  } else if (strcmp(interface, wl_compositor_interface.name) == 0) {
    ctx->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 1);
  } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
    ctx->xdg_wm_base =
      wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
    xdg_wm_base_add_listener(ctx->xdg_wm_base, &xdg_wm_base_listener, NULL);
  }
}

static void handle_global_remove(void *data, struct wl_registry *registry,
                                 uint32_t name) {
  log("%s: name: %u\n", __func__, name);
}

static const struct wl_registry_listener registry_listener = {
  .global = handle_global,
  .global_remove = handle_global_remove,
};

/* callbacks for buffer */
static void
wl_buffer_release(void *data, struct wl_buffer *wl_buffer)
{
    /* Sent by the compositor when it's no longer using this buffer */
    wl_buffer_destroy(wl_buffer);
}

static const struct wl_buffer_listener wl_buffer_listener = {
    .release = wl_buffer_release,
};


/* callbacks for frame */
static const struct wl_callback_listener wl_surface_frame_listener;


static void wl_surface_frame_done(void *data, struct wl_callback *cb,
                                  uint32_t time)
{
  /*
  Now, with each frame, we'll
  1. Destroy the now-used frame callback.
  2. Request a new callback for the next frame.
  3. Render and submit the new frame.
  The third step, broken down, is:
  1. Update our state with a new offset, using the time since the last frame to scroll at a consistent rate.
  2. Prepare a new wl_buffer and render a frame for it.
  3. Attach the new wl_buffer to our surface.
  4. Damage the entire surface.
  5. Commit the surface.
*/
  wl_callback_destroy(cb);
  struct wayland_context *ctx = (struct wayland_context *)data;
  cb = wl_surface_frame(ctx->surface);
  wl_callback_add_listener(cb, &wl_surface_frame_listener, ctx);
  log("%s: time: %u\n", __func__, time);
}

static const struct wl_callback_listener wl_surface_frame_listener = {
  .done = wl_surface_frame_done,
};

/* wayland context interfaces */
int wayland_ctx_create_surface(void *vctx, const char *name) {
  struct wayland_context *ctx = (struct wayland_context *)vctx;
  ctx->surface = wl_compositor_create_surface(ctx->compositor);
  ctx->xdg_surface =
    xdg_wm_base_get_xdg_surface(ctx->xdg_wm_base, ctx->surface);
  ctx->xdg_toplevel = xdg_surface_get_toplevel(ctx->xdg_surface);
  xdg_toplevel_set_title(ctx->xdg_toplevel, name);
  xdg_surface_add_listener(ctx->xdg_surface, &xdg_surface_listener, ctx);
  xdg_toplevel_add_listener(ctx->xdg_toplevel, &xdg_toplevel_listener, ctx);
  wl_surface_commit(ctx->surface);

  struct wl_callback *frame_cb = wl_surface_frame(ctx->surface);
  wl_callback_add_listener(frame_cb, &wl_surface_frame_listener, ctx);

  while ((wl_display_dispatch(ctx->display)) != -1 && !ctx->configured) {
    log("Waiting for the configure event\n");
  }
  return 0;
}


void wayland_ctx_free_surface(void *vctx) {
  struct wayland_context *ctx = (struct wayland_context *)vctx;
  wl_surface_destroy(ctx->surface);
}

int wayland_ctx_create_shm_pool(void *vctx, int shm_pool_size) {
  struct wayland_context *ctx = (struct wayland_context *)vctx;
  ctx->fd = allocate_shm_file(shm_pool_size);
  if (ctx->fd == -1) {
    err_log("failed to alloc shm file\n");
    return EXIT_FAILURE;
  }
  ctx->pool_data = mmap(NULL, shm_pool_size, PROT_READ | PROT_WRITE,
                        MAP_SHARED, ctx->fd, 0);
  if (ctx->pool_data == MAP_FAILED) {
    err_log("failed to mmap %d for pool_data", shm_pool_size);
    return EXIT_FAILURE;
  }
  ctx->pool = wl_shm_create_pool(ctx->shm, ctx->fd, shm_pool_size);
  ctx->shm_pool_size = shm_pool_size;
  return 0;
}

void wayland_ctx_free_shm_pool(void *vctx, int shm_pool_size) {
  struct wayland_context *ctx = (struct wayland_context *)vctx;
  wl_shm_pool_destroy(ctx->pool);
  munmap(ctx->pool_data, shm_pool_size);
  close(ctx->fd);
}

// need a shm manager to record the allocations of buffer
// WL_SHM_FORMAT_XRGB8888
int wayland_ctx_create_buffer(void *vctx, int height, int width, int stride, int offset, uint32_t format) {
  struct wayland_context *ctx = (struct wayland_context *)vctx;
  ctx->buffer =
      wl_shm_pool_create_buffer(ctx->pool, offset, width, height, stride, format);
  wl_buffer_add_listener(ctx->buffer, &wl_buffer_listener, NULL);
  ctx->pixels = (uint32_t *)&ctx->pool_data[offset];
  return 0;
}

uint32_t* wayland_ctx_get_pixel_buffer_ptr(void *vctx) {
  struct wayland_context *ctx = (struct wayland_context *)vctx;
  return ctx->pixels;
}

void wayland_ctx_attach_buffer(void *vctx, int x, int y) {
  struct wayland_context *ctx = (struct wayland_context *)vctx;
  wl_surface_attach(ctx->surface, ctx->buffer, x, y);
  wl_surface_damage(ctx->surface, x, y, UINT32_MAX, UINT32_MAX);
}

void wayland_ctx_commit_buffer(void *vctx) {
  struct wayland_context *ctx = (struct wayland_context *)vctx;
  log("%s, begin\n", __func__);
  wayland_ctx_attach_buffer(vctx, 0, 0);
  wl_surface_commit(ctx->surface);
  log("%s, end\n", __func__);
}

int wayland_ctx_poll_events(void *vctx) {
  struct wayland_context *ctx = (struct wayland_context *)vctx;
  return wl_display_dispatch(ctx->display);
}

/* wayland context interfaces */
void *wayland_ctx_make(void) {
  struct wayland_context *new = NULL;
  new = malloc(sizeof(struct wayland_context));
  if (!new)
    return NULL;
  new->display = NULL;
  new->registry = NULL;
  new->shm = NULL;
  new->shm_data = NULL;
  new->compositor = NULL;
  new->xdg_wm_base = NULL;
  new->seat = NULL;
  new->surface = NULL;
  new->xdg_surface = NULL;
  new->xdg_toplevel = NULL;
  new->configured = false;
  new->should_close = false;
  new->fd = -1;
  new->pool_data = NULL;
  new->shm_pool_size = 0;
  new->pool = NULL;
  new->buffer = NULL;
  new->pixels = NULL;
  return new;
}

void wayland_ctx_free(void **pctx) {
  struct wayland_context *ctx = *(struct wayland_context **)pctx;
  if (ctx) {
    free(ctx);
    ctx = NULL;
  }
}

static int is_context_noready(struct wayland_context *ctx) {
  return (ctx->shm == NULL || ctx->compositor == NULL || ctx->xdg_wm_base == NULL);
}

int wayland_ctx_create_window(void *vctx, const char *name, int height, int width,
                      int stride) {
  int shm_pool_size = stride * height * 2;
  struct wayland_context *ctx = (struct wayland_context *)vctx;
  /* use WAYLAND_DISPLAY (the default value is wayland-0) if passing NULL to this function */
  ctx->display = wl_display_connect(NULL);
  if (!ctx->display) {
    err_log("%s: failed to create display\n", __func__);
    return EXIT_FAILURE;
  }

  /* The wl_registry object used to manage the global objects,
   * such as wl_compositor, wl_seat, wl_output and so on
   */
  ctx->registry = wl_display_get_registry(ctx->display);
  if (!ctx->registry) {
    err_log("%s: failed to create ctx->registry\n", __func__);
    wl_display_disconnect(ctx->display);
    return EXIT_FAILURE;
  }

  wl_registry_add_listener(ctx->registry, &registry_listener, ctx);
  /* client will suspend till all request from client being handled by compositor */
  if (wl_display_roundtrip(ctx->display) == -1) {
    err_log("%s: failed to get other global objects\n", __func__);
    wl_registry_destroy(ctx->registry);
    wl_display_disconnect(ctx->display);
    return EXIT_FAILURE;
  }

  /* check if context we require are available */
  if (is_context_noready(ctx)) {
    err_log("%s: required objects are not ready\n", __func__);
    wl_registry_destroy(ctx->registry);
    wl_display_disconnect(ctx->display);
    return EXIT_FAILURE;
  }

  int ret = wayland_ctx_create_surface(ctx, name);
  if (ret) {
    err_log("%s: failed to create surface\n", __func__);
    wl_registry_destroy(ctx->registry);
    wl_display_disconnect(ctx->display);
    return EXIT_FAILURE;
  }

  ret = wayland_ctx_create_shm_pool(ctx, shm_pool_size);
  if (ret) {
    err_log("%s: failed to create surface\n", __func__);
    wayland_ctx_free_surface(ctx);
    wl_registry_destroy(ctx->registry);
    wl_display_disconnect(ctx->display);
    return EXIT_FAILURE;
  }

  ret = wayland_ctx_create_buffer(ctx, height, width, stride, 0, WL_SHM_FORMAT_XRGB8888);
  if (ret) {
    err_log("%s: failed to create surface\n", __func__);
    wayland_ctx_free_shm_pool(ctx, shm_pool_size);
    wayland_ctx_free_surface(ctx);
    wl_registry_destroy(ctx->registry);
    wl_display_disconnect(ctx->display);
    return EXIT_FAILURE;
  }
  return 0;
}

void wayland_ctx_close_window(void* vctx) {
  struct wayland_context *ctx = (struct wayland_context *)vctx;

  wayland_ctx_free_shm_pool(ctx, ctx->shm_pool_size);
  wayland_ctx_free_surface(ctx);
  wl_registry_destroy(ctx->registry);
  wl_display_disconnect(ctx->display);
}

bool wayland_ctx_window_should_close(void *vctx) {
  struct wayland_context *ctx = (struct wayland_context *)vctx;
  return ctx->should_close;
}

static struct win_ctx_ops wayland_ctx_ops = {
    .ctx_make = wayland_ctx_make,
    .ctx_free = wayland_ctx_free,
    .create_window = wayland_ctx_create_window,
    .close_window = wayland_ctx_close_window,
    .window_should_close = wayland_ctx_window_should_close,
    .get_pixel_buffer_ptr = wayland_ctx_get_pixel_buffer_ptr,
    .attach_buffer = wayland_ctx_attach_buffer,
    .commit_buffer = wayland_ctx_commit_buffer,
    .poll_events = wayland_ctx_poll_events,
};

void window_wayland_init(void) { win_ctx_ops_register(&wayland_ctx_ops); }
