#include <stdio.h>

#include "utils/utils.h"
#include "core/app.h"

void test_app_init_render()
{
  log("%s: begin\n", __func__);
  log("%s: end\n", __func__);
}

void test_app_init_display()
{
  log("%s: begin\n", __func__);
  log("%s: end\n", __func__);
}

void test_app_run_main_loop()
{
  log("%s: begin\n", __func__);
  log("%s: end\n", __func__);
}

void test_app_cleanup()
{
  log("%s: begin\n", __func__);
  log("%s: end\n", __func__);
}


static struct app_ops test_app_ops = {
  .init_render = test_app_init_render,
  .init_display = test_app_init_display,
  .run_main_loop = test_app_run_main_loop,
  .cleanup = test_app_cleanup,
};


int main(int argc, char **argv) {
  struct app* test_app = NULL;
  int ret = 0;
  struct app_config test_cfg = {
    .name = "test app",
  };
  test_app = app_make(&test_cfg, &test_app_ops);
  ret = app_run(test_app);
  if (ret) {
    err_log("There is some thing wrong when running an app\n");
  }

  app_free(&test_app);
  return 0;
}
