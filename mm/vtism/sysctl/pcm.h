
#pragma once

#include <linux/kobject.h>
#include <linux/lockdep.h>
#include <linux/memory-tiers.h>
#include <linux/memory.h>
#include <linux/slab.h>
#include <linux/sysfs.h>


ssize_t dump_node_info(char *buf, ssize_t len);
int register_pcm_sysctl(struct kobject *vtism_kobj);