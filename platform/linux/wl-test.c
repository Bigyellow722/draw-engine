#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>

#include "window-wayland.h"
#include "../utils/utils.h"
#include "shm.h"
#include "xdg-shell-client-protocol.h"

#define WIDTH 800
#define HEIGHT 600

struct wayland_context;
struct wayland_surface_manager;
struct wayland_window;
struct wayland_buffer_manager;
struct wayland_buffer;

void wayland_window_attach_buffer(struct wayland_window *win, struct wayland_buffer *buf, int x, int y);
void wayland_window_commit_buffer(struct wayland_window *win, struct wayland_buffer *buf);
struct wayland_buffer *wayland_window_find_a_free_buffer(struct wayland_window *win);
int buffer_manager_resize_buffers(struct wayland_buffer_manager *buf_manager,
                                  int height, int width, int stride,
                                  uint32_t format);
void wayland_window_free_buffer_manager(
					struct wayland_buffer_manager **pbuf_manager);
struct wayland_buffer_manager *
wayland_window_create_buffer_manager(struct wayland_window *win);
void wayland_surface_manager_free_window(struct wayland_window **pwin);

struct wayland_context {
  /* Global objects */
  struct wl_display *display;
  struct wl_registry *registry;
  struct wl_shm *shm; // provide a format interface to set pixel format
  struct wl_compositor *compositor;
  struct xdg_wm_base *xdg_wm_base;
  struct wl_seat *seat; // for handling events from input source, such as keyboard, pointer, touch and so on
};

#define BUFFER_CAPS 1

struct wayland_buffer {
  struct wayland_window *win;
  int offset; // offset in shm pool
  struct wl_buffer *buffer; // move it to wayland context
  uint32_t *pixels;         // move it to wayland context
  int busy;
};

struct wayland_buffer_manager
{
  struct wayland_window *win;
  /* the context of buffer */
  int fd; // shm fd
  uint8_t *pool_data; // shm buffer pool
  int shm_pool_size; // move it to wayland context
  struct wl_shm_pool *pool; // move it to wayland context
  int buffer_caps;
  struct wayland_buffer bufs[BUFFER_CAPS];
  int index; // the index of buffer being used now
};

struct wayland_window
{
  struct wayland_surface_manager *surf_manager;
  struct wayland_buffer_manager *buf_manager;
  /* the context of surface */
  struct wl_surface *surface;
  struct xdg_surface *xdg_surface;
  struct xdg_toplevel *xdg_toplevel;
  const char* name;
  /* states */
  bool configured;
  bool should_close;
  int actual_height;
  int actual_width;
  int height;
  int width;
  int stride;
  uint32_t format;
};


struct wayland_surface_manager
{
  struct wayland_context *g_ctx;
  struct wayland_window *win;
};

struct wayland_surface_manager *
wayland_surface_manager_make(struct wayland_context *ctx) {
  struct wayland_surface_manager *new = NULL;
  new = malloc(sizeof(struct wayland_surface_manager));
  if (!new)
    return NULL;
  new->g_ctx = ctx;
  return new;
}


void wayland_surface_manager_free(struct wayland_surface_manager **psm) {
  struct wayland_surface_manager *sm = *psm;
  if (sm) {
    free(sm);
    sm = NULL;
  }
}

/* xdg interfaces */
/* The client need to send back a pong request */
static void xdg_wm_base_handle_ping(void *data, struct xdg_wm_base *xdg_wm_base,
                                    uint32_t serial) {
  (void)data;
  xdg_wm_base_pong(xdg_wm_base, serial);
  log("%s: end\n", __func__);
}


static const struct xdg_wm_base_listener xdg_wm_base_listener = {
  .ping = xdg_wm_base_handle_ping,
};


static void xdg_surface_handle_configure(void *data,
                                         struct xdg_surface *xdg_surface,
                                         uint32_t serial) {
  struct wayland_window *win = (struct wayland_window *)data;
  xdg_surface_ack_configure(xdg_surface, serial);
  if (win->configured) {
    wl_surface_commit(win->surface);
    log("%s: end\n", __func__);
  } else {
    log("recv the first configure event\n");
  }
  win->configured = true;
}

static const struct xdg_surface_listener xdg_surface_listener = {
  .configure = xdg_surface_handle_configure,
};

static void xdg_toplevel_handle_configure(void *data,
                                          struct xdg_toplevel *xdg_toplevel,
                                          int32_t width, int32_t height,
                                          struct wl_array *states) {
  (void)xdg_toplevel;
  (void)states;
  struct wayland_window *win = (struct wayland_window *)data;
  log("%s: width: %d, height: %d\n", __func__, width, height);
  win->actual_width = width;
  win->actual_height = height;
}

static void xdg_toplevel_handle_close(void *data,
                                      struct xdg_toplevel *xdg_toplevel) {
  struct wayland_window *win = (struct wayland_window *)data;
  (void)xdg_toplevel;
  win->should_close = true;
  log("%s: end\n", __func__);
}

static void
xdg_toplevel_handle_wm_capabilities(void *data,
                                    struct xdg_toplevel *xdg_toplevel,
                                    struct wl_array *capabilities) {

}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_handle_configure,
    .close = xdg_toplevel_handle_close,
    .wm_capabilities = xdg_toplevel_handle_wm_capabilities,
};


/* callbacks for registry */
static void handle_global(void *data, struct wl_registry *registry,
                          uint32_t name, const char *interface,
                          uint32_t version) {
  struct wayland_context *ctx = (struct wayland_context *)data;

  if (strcmp(interface, wl_shm_interface.name) == 0) {
    ctx->shm = wl_registry_bind(registry, name, &wl_shm_interface, version);
  } else if (strcmp(interface, wl_compositor_interface.name) == 0) {
    ctx->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, version);
  } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
    ctx->xdg_wm_base =
      wl_registry_bind(registry, name, &xdg_wm_base_interface, version);
    xdg_wm_base_add_listener(ctx->xdg_wm_base, &xdg_wm_base_listener, NULL);
  }
}

static void handle_global_remove(void *data, struct wl_registry *registry,
                                 uint32_t name) {
  (void)data;
  log("%s: name: %u\n", __func__, name);
  if (registry)
      wl_registry_destroy(registry);
}

static const struct wl_registry_listener registry_listener = {
  .global = handle_global,
  .global_remove = handle_global_remove,
};

static int is_context_noready(struct wayland_context *ctx) {
  return (ctx->shm == NULL || ctx->compositor == NULL || ctx->xdg_wm_base == NULL);
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
  new->compositor = NULL;
  new->xdg_wm_base = NULL;
  new->seat = NULL;
  return new;
}

int wayland_ctx_setup(struct wayland_context *ctx)
{
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
  return 0;
}

void wayland_ctx_cleanup(struct wayland_context *ctx)
{
  if (ctx) {
    if (ctx->registry)
      wl_registry_destroy(ctx->registry);
    if (ctx->display)
      wl_display_disconnect(ctx->display);
  }
}

void wayland_ctx_free(void **pctx) {
  struct wayland_context *ctx = *(struct wayland_context **)pctx;
  if (ctx) {
    free(ctx);
    ctx = NULL;
  }
}



/* callbacks for buffer */
static void wl_buffer_release(void *data, struct wl_buffer *wl_buffer) {
  (void)wl_buffer;
  struct wayland_buffer *buf =
    (struct wayland_buffer *)data;
  /* Sent by the compositor when it's no longer using this buffer */
  log("%s: begin\n", __func__);
  buf->busy = 0;
  log("buf[%d] busy: %d\n", buf->offset, buf->busy);
  //wl_buffer_destroy(wl_buffer);
  log("%s: end\n", __func__);
}

static const struct wl_buffer_listener wl_buffer_listener = {
    .release = wl_buffer_release,
};


/* callbacks for frame */
static const struct wl_callback_listener wl_surface_frame_listener;


static void wl_surface_frame_done(void *data, struct wl_callback *cb,
                                  uint32_t time)
{
  struct wayland_window *win = (struct wayland_window *)data;
  log("%s: begin\n", __func__);
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
  struct wayland_buffer *free_buf = wayland_window_find_a_free_buffer(win);
  if (!free_buf) {
    log("skipping this frame\n");
    struct wl_callback *frame_cb = wl_surface_frame(win->surface);
    wl_callback_add_listener(frame_cb, &wl_surface_frame_listener, win);
    wl_surface_commit(win->surface);
    return;
  }
  log("%s: time: %u\n", __func__, time);
  log("free_buf: %p\n", (void *)free_buf);
  log("free_buf->buffer: %p\n", (void *)free_buf->buffer);
  for (int i = 0; i < win->buf_manager->buffer_caps; i++) {
    log("win->buf_manager->bufs[%d] address: %p\n", i, (void *)&win->buf_manager->bufs[i]);
    log("win->buf_manager->bufs[%d].buffer address: %p\n", i, (void *)win->buf_manager->bufs[i].buffer);
  }

  for (int y = 0; y < HEIGHT; ++y) {
    for (int x = 0; x < WIDTH; ++x) {
      if ((x + y / 8 * 8) % 16 < 8) {
	free_buf->pixels[y * HEIGHT + x] = 0xFF000000 | (((time % 256) & 0xff) << 8);
      } else {
	free_buf->pixels[y * HEIGHT + x] = 0xFFFFFFFF;
      }
    }
  }
  //wl_surface_attach(win->surface, free_buf->buffer, 0, 0);
  //wl_surface_damage_buffer(win->surface, 0, 0, INT32_MAX, INT32_MAX);
  wayland_window_attach_buffer(win, free_buf, 0, 0);
  struct wl_callback *frame_cb = wl_surface_frame(win->surface);
  wl_callback_add_listener(frame_cb, &wl_surface_frame_listener, win);
  wayland_window_commit_buffer(win, free_buf);
  log("%s: end\n", __func__);
}

static const struct wl_callback_listener wl_surface_frame_listener = {
  .done = wl_surface_frame_done,
};

/* wayland context interfaces */
struct wayland_window *wayland_surface_manager_create_window(
    struct wayland_surface_manager *surf_manager, const char *name, int height,
							     int width, int stride, uint32_t format) {
  struct wayland_context *ctx = surf_manager->g_ctx;
  struct wayland_window *win = NULL;
  win = malloc(sizeof(struct wayland_window));
  if (!win)
    return NULL;
  win->surf_manager = surf_manager;
  win->height = height;
  win->width = width;
  win->format = format;
  win->stride = stride;
  win->surface = wl_compositor_create_surface(ctx->compositor);
  if (!win->surface) {
    free(win);
    return NULL;
  }
  win->xdg_surface =
    xdg_wm_base_get_xdg_surface(ctx->xdg_wm_base, win->surface);
  if (!win->xdg_surface) {
    wl_surface_destroy(win->surface);
    free(win);
    return NULL;
  }
  win->xdg_toplevel = xdg_surface_get_toplevel(win->xdg_surface);
  if (!win->xdg_toplevel) {
    xdg_surface_destroy(win->xdg_surface);
    wl_surface_destroy(win->surface);
    free(win);
    return NULL;
  }
  xdg_toplevel_set_title(win->xdg_toplevel, name);
  xdg_surface_add_listener(win->xdg_surface, &xdg_surface_listener, win);
  xdg_toplevel_add_listener(win->xdg_toplevel, &xdg_toplevel_listener, win);
  /* no buffer attached */
  wl_surface_commit(win->surface);

  struct wl_callback *frame_cb = wl_surface_frame(win->surface);
  wl_callback_add_listener(frame_cb, &wl_surface_frame_listener, win);

  while ((wl_display_dispatch(ctx->display)) != -1 && !win->configured) {
    log("Waiting for the configure event\n");
  }
  struct wayland_buffer_manager *buf_manager =
    wayland_window_create_buffer_manager(win);
  if (!buf_manager) {
    wayland_surface_manager_free_window(&win);
    return NULL;
  }
  win->buf_manager = buf_manager;
  log("%s, end\n", __func__);
  return win;
}

void wayland_surface_manager_free_window(struct wayland_window **pwin) {
  struct wayland_window *win = *pwin;
  if (win) {
    if (win->xdg_toplevel)
      xdg_toplevel_destroy(win->xdg_toplevel);
    if (win->xdg_surface)
      xdg_surface_destroy(win->xdg_surface);
    if (win->surface)
      wl_surface_destroy(win->surface);
    if (win->buf_manager)
      wayland_window_free_buffer_manager(&win->buf_manager);
    free(win);
    win = NULL;
  }
}


// need a shm manager to record the allocations of buffer
// WL_SHM_FORMAT_XRGB8888
struct wayland_buffer_manager *
wayland_window_create_buffer_manager(struct wayland_window *win) {
  struct wayland_buffer_manager *new = NULL;
  new = malloc(sizeof(struct wayland_buffer_manager));
  if (!new)
    return NULL;
  new->win = win;
  new->buffer_caps = BUFFER_CAPS;
  int ret = buffer_manager_resize_buffers(new, win->height, win->width, win->stride, win->format);
  if (ret) {
    wayland_window_free_buffer_manager(&new);
    return NULL;
  }
  return new;
}

void wayland_window_free_buffer_manager(
					struct wayland_buffer_manager **pbuf_manager) {
  struct wayland_buffer_manager *buf_manager = *pbuf_manager;
  if (buf_manager) {
    if (buf_manager->pool)
      wl_shm_pool_destroy(buf_manager->pool);
    if (buf_manager->pool_data && buf_manager->shm_pool_size != 0) {
      munmap(buf_manager->pool_data, buf_manager->shm_pool_size);
      buf_manager->shm_pool_size = 0;
    }
    if (buf_manager->fd > 0)
      close(buf_manager->fd);
    free(buf_manager);
    buf_manager = NULL;
  }
}

int buffer_manager_resize_buffers(struct wayland_buffer_manager *buf_manager,
                                  int height, int width, int stride, uint32_t format) {
  int new_size = height * stride * buf_manager->buffer_caps;
  buf_manager->fd = allocate_shm_file(new_size);
  if (buf_manager->fd == -1) {
    err_log("failed to alloc shm file\n");
    return 1;
  }
  buf_manager->pool_data = mmap(NULL, new_size, PROT_READ | PROT_WRITE,
                        MAP_SHARED, buf_manager->fd, 0);
  if (buf_manager->pool_data == MAP_FAILED) {
    err_log("failed to mmap %d for pool_data\n", new_size);
    close(buf_manager->fd);
    return 1;
  }
  buf_manager->pool = wl_shm_create_pool(buf_manager->win->surf_manager->g_ctx->shm, buf_manager->fd, new_size);
  if (!buf_manager->pool) {
    close(buf_manager->fd);
    munmap(buf_manager->pool_data, new_size);
    return 1;
  }
  buf_manager->shm_pool_size = new_size;
  for (int i = 0; i < buf_manager->buffer_caps; i++) {
    int offset = i * height * stride;
    buf_manager->bufs[i].buffer = wl_shm_pool_create_buffer(
							    buf_manager->pool, offset, width, height, stride, format);
    if (!buf_manager->bufs[i].buffer) {
      err_log("failed to create wl_buffer\n");
    }
    wl_buffer_add_listener(buf_manager->bufs[i].buffer, &wl_buffer_listener, &buf_manager->bufs[i]);
    buf_manager->bufs[i].pixels = (uint32_t *)&buf_manager->pool_data[offset];
    buf_manager->bufs[i].offset = i;
    buf_manager->bufs[i].busy = 0;
    buf_manager->bufs[i].win = buf_manager->win;
  }
  wl_shm_pool_destroy(buf_manager->pool);
  close(buf_manager->fd);
  return 0;
}

struct wayland_buffer *
wayland_window_find_a_free_buffer(struct wayland_window *win) {
  struct wayland_buffer *tmp = NULL;
  struct wayland_buffer_manager *buf_manager = win->buf_manager;
  log("buf_manager->buffer_caps: %d\n", buf_manager->buffer_caps);
  for (int i = 0; i < buf_manager->buffer_caps; i++) {
    if (!buf_manager->bufs[i].busy) {
      tmp = &buf_manager->bufs[i];
      log("buf[%d] is available\n", i);
      return tmp;
    }
  }
  return tmp;
}

void wayland_window_attach_buffer(struct wayland_window *win, struct wayland_buffer *buf, int x, int y) {
  log("%s, begin\n", __func__);
  wl_surface_attach(win->surface, buf->buffer, x, y);
  wl_surface_damage(win->surface, x, y, UINT32_MAX, UINT32_MAX);
  log("%s, end\n", __func__);
}

void wayland_window_commit_buffer(struct wayland_window *win, struct wayland_buffer *buf) {
  log("%s, begin\n", __func__);
  wayland_window_attach_buffer(win, buf, 0, 0);
  buf->busy = 1;
  win->buf_manager->index = buf->offset;
  wl_surface_commit(win->surface);
  log("%s, end\n", __func__);
}

int main(void) {
  int ret = 0;
  struct wayland_context *ctx = wayland_ctx_make();
  if (!ctx)
    return 1;

  ret = wayland_ctx_setup(ctx);
  if (ret) {
    wayland_ctx_cleanup(ctx);
    wayland_ctx_free((void**)&ctx);
    return ret;
  }

  struct wayland_surface_manager *surf_manager =
    wayland_surface_manager_make(ctx);
  if (!surf_manager) {
    wayland_ctx_cleanup(ctx);
    wayland_ctx_free((void**)&ctx);
    return 1;
  }

  struct wayland_window *win = wayland_surface_manager_create_window(
      surf_manager, "wl-test", HEIGHT, WIDTH, WIDTH * 4,
								     WL_SHM_FORMAT_XRGB8888);
  if (!win) {
    wayland_surface_manager_free_window(&win);
    wayland_ctx_cleanup(ctx);
    wayland_ctx_free((void**)&ctx);
    return 1;
  }

  struct wayland_buffer *buf = wayland_window_find_a_free_buffer(win);
  if (!buf) {
    wayland_surface_manager_free_window(&win);
    wayland_ctx_cleanup(ctx);
    wayland_ctx_free((void**)&ctx);
    return 1;
  }
  wayland_window_commit_buffer(win, buf);
  while (wayland_ctx_poll_events(ctx) > 0) {
  }
  wayland_ctx_cleanup(ctx);
  wayland_ctx_free((void**)&ctx);
  log("%s, end\n", __func__);
  return 0;
}
