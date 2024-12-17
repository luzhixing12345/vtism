
#pragma once

#include <linux/module.h>

#define DEVICE_NAME "vtism"

#define INFO(...) printk(KERN_INFO DEVICE_NAME ": " __VA_ARGS__)
#define ERR(...) printk(KERN_ERR DEVICE_NAME ": " __VA_ARGS__)

#define KB 1024
#define MB (1024 * 1024)
#define GB (1024 * 1024 * 1024)