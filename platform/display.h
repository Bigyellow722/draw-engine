#ifndef _DISPLAY_H_
#define _DISPLAY_H_

#include <stdint.h>
#include <stdbool.h>

struct win_ctx_ops {
  void* (*ctx_make)(void);
  void (*ctx_free)(void **ctx);
  int (*create_window)(void *ctx, const char *name, int height, int width,
                      int stride);
  void (*close_window)(void *ctx);
  bool (*window_should_close)(void *vctx);
  uint32_t* (*get_pixel_buffer_ptr)(void *ctx);
  void (*attach_buffer)(void *ctx, int x, int y);
  void (*commit_buffer)(void *ctx);
  int (*poll_events)(void *ctx);
};

struct win_ctx {
  struct win_ctx_ops *ops;
  void *ctx;
};


void win_ctx_ops_register(struct win_ctx_ops* ops);

int win_ctx_init(void);

int win_context_setup(struct win_ctx *ctx);
void win_context_cleanup(struct win_ctx *ctx);

  
#endif
