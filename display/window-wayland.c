#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include "window-wayland.h"
#include "../utils/utils.h"
#include "../utils/shm.h"

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
  log("%s: name: %u interface: %s\n", __func__, name, interface);
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


/* wayland context interfaces */
struct wayland_context *wayland_ctx_make(void) {
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
  return new;
}

void wayland_ctx_free(struct wayland_context **pctx) {
  struct wayland_context *ctx = *pctx;
  if (ctx) {
    free(ctx);
    ctx = NULL;
  }
}

static int is_context_noready(struct wayland_context *ctx) {
  return (ctx->shm == NULL || ctx->compositor == NULL || ctx->xdg_wm_base == NULL);
}

int wayland_ctx_setup(struct wayland_context *ctx) {
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

  /*
    1. Bind to wl_compositor and use it to create a wl_surface.
    2. Bind to xdg_wm_base and use it to create an xdg_surface with your wl_surface.
    3. Create an xdg_toplevel from the xdg_surface with xdg_surface.get_toplevel.
    4. Configure a listener for the xdg_surface and await the configure event.
    5. Bind to the buffer allocation mechanism of your choosing (such as wl_shm) and allocate a shared buffer, then render your content to it.
    6. Use wl_surface.attach to attach the wl_buffer to the wl_surface.
    7. Use xdg_surface.ack_configure, passing it the serial from configure, acknowledging that you have prepared a suitable frame.
    8. Send a wl_surface.commit request.
   */

  ctx->surface = wl_compositor_create_surface(ctx->compositor);
  ctx->xdg_surface =
    xdg_wm_base_get_xdg_surface(ctx->xdg_wm_base, ctx->surface);
  ctx->xdg_toplevel = xdg_surface_get_toplevel(ctx->xdg_surface);
  xdg_surface_add_listener(ctx->xdg_surface, &xdg_surface_listener, ctx);
  xdg_toplevel_add_listener(ctx->xdg_toplevel, &xdg_toplevel_listener, ctx);
  wl_surface_commit(ctx->surface);
  while ((wl_display_dispatch(ctx->display)) != -1 && !ctx->configured) {
    log("Waiting for the configure event\n");
  }
  return 0;
}

void wayland_ctx_cleanup(struct wayland_context *ctx) {
  wl_surface_destroy(ctx->surface);
  wl_registry_destroy(ctx->registry);
  wl_display_disconnect(ctx->display);
}


struct window_context *win_ctx_make(struct wayland_context *ctx, int width,
                                    int height) {
  assert(ctx != NULL);
  struct window_context *new = NULL;
  new = malloc(sizeof(struct window_context));
  if (!new) {
    return NULL;
  }
  new->wayland_ctx = ctx;
  new->width = width;
  new->height = height;
  new->stride = width * 4;
  new->fd = -1;
  new->pool_data = NULL;
  new->shm_pool_size = new->stride * new->height * 2;
  new->pool = NULL;
  new->index = 0;
  new->offset = new->stride * new->height * new->index;
  new->buffer = NULL;
  new->pixels = NULL;
  return new;
}

void win_ctx_free(struct window_context** pwin_ctx) {
  struct window_context *win_ctx = *pwin_ctx;
  if (win_ctx) {
    free(win_ctx);
    win_ctx = NULL;
  }
}

static void
wl_buffer_release(void *data, struct wl_buffer *wl_buffer)
{
    /* Sent by the compositor when it's no longer using this buffer */
    wl_buffer_destroy(wl_buffer);
}

static const struct wl_buffer_listener wl_buffer_listener = {
    .release = wl_buffer_release,
};

int win_context_setup(struct window_context *ctx) {
  int ret = 0;
  ctx->fd = allocate_shm_file(ctx->shm_pool_size);
  ctx->pool_data = mmap(NULL, ctx->shm_pool_size, PROT_READ | PROT_WRITE,
                        MAP_SHARED, ctx->fd, 0);
  ctx->pool =
    wl_shm_create_pool(ctx->wayland_ctx->shm, ctx->fd, ctx->shm_pool_size);
  ctx->buffer =
      wl_shm_pool_create_buffer(ctx->pool, ctx->offset, ctx->width, ctx->height,
                                ctx->stride, WL_SHM_FORMAT_XRGB8888);
  wl_buffer_add_listener(ctx->buffer, &wl_buffer_listener, NULL);
  ctx->pixels = (uint32_t *)&ctx->pool_data[ctx->offset];
  return ret;
}

void win_context_cleanup(struct window_context *ctx) {
  wl_buffer_destroy(ctx->buffer);
  wl_shm_pool_destroy(ctx->pool);
  munmap(ctx->pool_data, ctx->shm_pool_size);
  close(ctx->fd);
}
