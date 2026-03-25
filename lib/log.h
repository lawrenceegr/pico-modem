#ifndef AQUAGATE_LOG_H
#define AQUAGATE_LOG_H

#include <stdio.h>
#include "config.h"

#define LOG_ERR(fmt, ...)  printf("[ERR] " fmt "\n", ##__VA_ARGS__)
#define LOG_WRN(fmt, ...)  printf("[WRN] " fmt "\n", ##__VA_ARGS__)
#define LOG_INF(fmt, ...)  printf("[INF] " fmt "\n", ##__VA_ARGS__)

#if DEBUG
#define LOG_DBG(fmt, ...)  printf("[DBG] " fmt "\n", ##__VA_ARGS__)
#else
#define LOG_DBG(fmt, ...)  do {} while (0)
#endif

#endif