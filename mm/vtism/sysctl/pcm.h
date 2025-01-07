
#pragma once

#include <linux/kobject.h>
#include <linux/lockdep.h>
#include <linux/memory-tiers.h>
#include <linux/memory.h>
#include <linux/slab.h>
#include <linux/sysfs.h>



int register_pcm_sysctl(struct kobject *vtism_kobj);