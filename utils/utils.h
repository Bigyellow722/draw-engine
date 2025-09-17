#ifndef _UTILS_H_
#define _UTILS_H_

#include <stdio.h>
#include <errno.h>
#include <string.h>

#define err_log(fmt, ...)                                                      \
  do {                                                                         \
    fprintf(stderr, "err msg: %s. ", strerror(errno));                         \
    fprintf(stderr, fmt, ##__VA_ARGS__);                                       \
  } while(0)
#define log(fmt, ...) fprintf(stdout, fmt, ##__VA_ARGS__)

#endif
