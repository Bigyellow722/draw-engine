#ifndef _DISPLAY_H_
#define _DISPLAY_H_

struct win_ctx_ops {
  void* (*priv_make)(const char* name, int height, int width, int stride);
  void (*priv_free)(void **priv);
  int (*priv_setup)(void *priv);
  void (*priv_cleanup)(void *priv);
  void (*fill_buffer)(void *priv, void *bitmap,
                                 int len);
  void (*commit_buffer)(void *priv);
  void (*sync)(void *priv);
};

struct window_context {
  void *priv;
  int width;
  int height;
  int stride;
};

void win_ctx_ops_register(struct win_ctx_ops* ops);

struct window_context *win_ctx_make(const char* name, int width,
                                    int height);
void win_ctx_free(struct window_context **pwin_ctx);
int win_context_setup(struct window_context *ctx);
void win_context_cleanup(struct window_context *ctx);

  
#endif
