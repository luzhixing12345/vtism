
#pragma once

#include <linux/module.h>

#define DEVICE_NAME "vtism"

#ifdef CONFIG_VTISM_DEBUG
#define INFO(...) pr_info(DEVICE_NAME ": " __VA_ARGS__)
#else
#define INFO(...) ;
#endif

#define ERR(...) pr_err(DEVICE_NAME ": " __VA_ARGS__)

#define KB 1024
#define MB (1024 * 1024)
#define GB (1024 * 1024 * 1024)