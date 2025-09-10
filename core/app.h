#ifndef _APP_H_
#define _APP_H_


struct app_ops {
  void (*init_render)();
  void (*init_display)();
  void (*run_main_loop)();
  void (*cleanup)();
};


struct app_config {
  char *name;
};

struct app {
  char *name;
  struct app_ops* ops;
};

struct app* app_make(struct app_config* cfg, struct app_ops* ops);
void app_free(struct app** papp);
int app_run(struct app* app);

#endif
