
#pragma once

#include <linux/module.h>

#define DEVICE_NAME "vtism_migrate"

#define INFO(...) printk(KERN_INFO DEVICE_NAME __VA_ARGS__)
#define ERR(...) printk(KERN_ERR DEVICE_NAME __VA_ARGS__)