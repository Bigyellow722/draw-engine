#include <stdlib.h>
#include <string.h>

#include "../utils/utils.h"
#include "app.h"

struct app* app_make(struct app_config* cfg, struct app_ops* ops)
{
  struct app* new = NULL;

  if (!ops) {
    err_log("%s: Invalid ops\n", __func__);
    return NULL;
  }
  new = malloc(sizeof(struct app));
  if (!new) {
    err_log("%s: no enough memory\n", __func__);
    return NULL;
  }

  size_t name_len = strlen(cfg->name);
  new->name = malloc(name_len + 1);
  if (!new->name) {
    err_log("%s: no enough memory\n", __func__);
    return NULL;
  }
  strncpy(new->name, cfg->name, name_len + 1);
  new->ops = ops;
  return new;
}

void app_free(struct app** papp)
{
  struct app* app = *papp;
  if (app) {
    if (app->name) {
      free(app->name);
      app->name = NULL;
    }
    free(app);
    app = NULL;
  }
}

int app_run(struct app* app)
{
  app->ops->init_display();
  app->ops->init_render();
  app->ops->run_main_loop();
  app->ops->cleanup();
  return 0;
}
