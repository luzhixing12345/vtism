
#pragma once

#include <linux/kobject.h>
#include <linux/lockdep.h>
#include <linux/memory-tiers.h>
#include <linux/memory.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

struct node_info {
    bool is_toptier;
    unsigned int read_bw;
    unsigned int write_bw;
    // latency and to_cxl_latency only work on cpu node
    unsigned int latency;
    unsigned int to_cxl_latency;
    unsigned int free_mem_size;
    unsigned int total_mem_size;
    struct kobj_attribute read_bw_attr;
    struct kobj_attribute write_bw_attr;
    struct kobj_attribute latency_attr;
    struct kobj_attribute to_cxl_latency_attr;
    struct kobject *node_kobj;
};

ssize_t dump_node_bw_lat_info(char *buf, ssize_t len);
int register_pcm_sysctl(struct kobject *vtism_kobj);
void unregister_pcm_sysctl(void);
int find_best_demotion_node(int node, const nodemask_t *maskp);
// bool should_migrate_to_target_node(int page_nid, int target_nid);