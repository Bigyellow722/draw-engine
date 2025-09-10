#ifndef _UTILS_H_
#define _UTILS_H_

#include <stdio.h>

#define err_log(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)
#define log(fmt, ...) fprintf(stdout, fmt, ##__VA_ARGS__)

#endif
