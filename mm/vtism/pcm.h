
#pragma once

#include <linux/kobject.h>
#include <linux/lockdep.h>
#include <linux/memory-tiers.h>
#include <linux/memory.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

ssize_t dump_node_bw_lat_info(char *buf, ssize_t len);
int register_pcm_sysctl(struct kobject *vtism_kobj);
void unregister_pcm_sysctl(void);