
#include <asm-generic/errno-base.h>
#include <linux/hrtimer.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/ktime.h>
#include <linux/lockdep.h>
#include <linux/memory-tiers.h>
#include <linux/memory.h>
#include <linux/migrate.h>
#include <linux/mmzone.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

#include "common.h"
#include "kvm.h"
#include "page_classify.h"
#include "page_migration.h"
#include "pcm.h"

static struct kobject *vtism_kobj;

bool vtism_enable = false;
struct kobj_attribute vtism_enable_attr;

bool vtism_migration_enable = false;
struct kobj_attribute vtism_migration_enable_attr;

struct demotion_nodes {
    nodemask_t target_demotion_nodes;
};
extern struct demotion_nodes *node_demotion;
ssize_t dump_demotion_pretarget(char *buf) {
    ssize_t len = 0;
    int node, nid;
    len += sysfs_emit_at(buf, len, "[demotion pretarget]\n");
    for_each_node_state(node, N_MEMORY) {
        nodemask_t target_demotion_nodes = node_demotion[node].target_demotion_nodes;
        if (nodes_empty(target_demotion_nodes)) {
            len += sysfs_emit_at(buf, len, "node %d has no demotion target(cxl node)\n", node);
        } else {
            len += sysfs_emit_at(buf, len, "node %d demotion target: ", node);
            for_each_node_mask(nid, target_demotion_nodes) {
                len += sysfs_emit_at(buf, len, "%d ", nid);
            }
            len += sysfs_emit_at(buf, len, "\n");
        }
    }
    len += sysfs_emit_at(buf, len, "\n");
    return len;
}

static ssize_t dump_node_mem_info(char *buf, ssize_t len) {
    int nid;
    len += sysfs_emit_at(buf, len, "[node mem info]\n");
    // 遍历所有 NUMA 节点
    for_each_online_node(nid) {
        struct pglist_data *pgdat = NODE_DATA(nid);
        unsigned long total_pages = 0;
        unsigned long free_pages = 0;

        // 遍历该节点的所有 zones
        for (int zid = 0; zid < MAX_NR_ZONES; zid++) {
            struct zone *zone = pgdat->node_zones + zid;

            // 跳过无效或未初始化的 zone
            if (!populated_zone(zone))
                continue;

            // 获取 total 和 free 页面数
            total_pages += atomic_long_read(&zone->managed_pages);
            free_pages += zone_page_state(zone, NR_FREE_PAGES);
        }

        // 打印节点信息
        len += sysfs_emit_at(buf,
                             len,
                             "node %d: total = %lu MB(%lu GB), free = %lu MB(%lu GB)\n",
                             nid,
                             (total_pages * PAGE_SIZE) >> 20,
                             (total_pages * PAGE_SIZE) >> 30,
                             (free_pages * PAGE_SIZE) >> 20,
                             (free_pages * PAGE_SIZE) >> 30);
    }
    len += sysfs_emit_at(buf, len, "\n");
    return len;
}

static ssize_t dump_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    ssize_t len = dump_demotion_pretarget(buf);
    len = dump_vm_info(buf, len);
    len = dump_node_mem_info(buf, len);
    len = dump_node_bw_lat_info(buf, len);
    len = dump_page_classify_info(buf, len);
    len = dump_page_migration_info(buf, len);
    return len;
}

static struct kobj_attribute vtism_dump_attr = __ATTR_RO(dump);

static ssize_t enable_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    return sysfs_emit(buf, "%s\n", vtism_enable ? "true" : "false");
}

static ssize_t enable_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf,
                            size_t count) {
    bool new_val;
    if (kstrtobool(buf, &new_val) == -EINVAL) {
        return -EINVAL;
    }

    if (vtism_enable == false && new_val == true) {
        vtism_enable = new_val;
    } else if (vtism_enable == true && new_val == false) {
        vtism_enable = new_val;
    } else {
        pr_err("vtism_enable is already %s\n", vtism_enable ? "true" : "false");
    }
    return count;
}

static ssize_t migration_enable_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    return sysfs_emit(buf, "%s\n", vtism_migration_enable ? "true" : "false");
}

static ssize_t migration_enable_store(struct kobject *kobj, struct kobj_attribute *attr,
                                      const char *buf, size_t count) {
    bool new_val;
    if (kstrtobool(buf, &new_val) == -EINVAL) {
        return -EINVAL;
    }
    if (vtism_migration_enable == false && new_val == true) {
        if (page_migration_init() < 0) {
            ERR("enable vtism migration failed\n");
            return count;
        }
        vtism_migration_enable = new_val;
        INFO("enable vtism page migration\n");
    } else if (vtism_migration_enable == true && new_val == false) {
        page_migration_exit();
        vtism_migration_enable = new_val;
        INFO("disable vtism page migration\n");
    } else {
        pr_err("vtism_migration_enable is already %s\n", vtism_migration_enable ? "true" : "false");
    }
    return count;
}

int demotion_min_free_ratio = CONFIG_VTISM_DEMOTION_MIN_FREE_RATIO;
int promotion_min_free_ratio = CONFIG_VTISM_PROMOTION_MIN_FREE_RATIO;
static struct kobj_attribute vtism_demotion_min_free_ratio_attr;
static struct kobj_attribute vtism_promotion_min_free_ratio_attr;

static ssize_t demotion_min_free_ratio_show(struct kobject *kobj, struct kobj_attribute *attr,
                                            char *buf) {
    return sysfs_emit(buf, "%d\n", demotion_min_free_ratio);
}

static ssize_t demotion_min_free_ratio_store(struct kobject *kobj, struct kobj_attribute *attr,
                                             const char *buf, size_t count) {
    int new_val;
    if (kstrtoint(buf, 10, &new_val) == -EINVAL) {
        return -EINVAL;
    }
    demotion_min_free_ratio = new_val;
    return count;
}

static ssize_t promotion_min_free_ratio_show(struct kobject *kobj, struct kobj_attribute *attr,
                                             char *buf) {
    return sysfs_emit(buf, "%d\n", promotion_min_free_ratio);
}

static ssize_t promotion_min_free_ratio_store(struct kobject *kobj, struct kobj_attribute *attr,
                                              const char *buf, size_t count) {
    int new_val;
    if (kstrtoint(buf, 10, &new_val) == -EINVAL) {
        return -EINVAL;
    }
    promotion_min_free_ratio = new_val;
    return count;
}

/*
kernel system interface for vtism (/sys/kernel/mm/vtism)
*/

int vtismctl_init(void) {
    int err;
    vtism_kobj = kobject_create_and_add("vtism", mm_kobj);
    if (!vtism_kobj) {
        pr_err("failed to create vtism kobject\n");
        return -ENOMEM;
    }

    err = sysfs_create_file(vtism_kobj, &vtism_dump_attr.attr);
    if (err) {
        pr_err("failed to create dump file\n");
        goto delete_obj;
    }

    sysfs_attr_init(&vtism_enable_attr.attr);
    vtism_enable_attr.attr.name = "enable";
    vtism_enable_attr.attr.mode = 0666;
    vtism_enable_attr.show = enable_show;
    vtism_enable_attr.store = enable_store;  // control vtism page classify and migrate
    err = sysfs_create_file(vtism_kobj, &vtism_enable_attr.attr);
    if (err) {
        pr_err("failed to create enable file\n");
        goto delete_obj;
    }

    sysfs_attr_init(&vtism_migration_enable_attr.attr);
    vtism_migration_enable_attr.attr.name = "migration_enable";
    vtism_migration_enable_attr.attr.mode = 0666;
    vtism_migration_enable_attr.show = migration_enable_show;
    vtism_migration_enable_attr.store = migration_enable_store;
    err = sysfs_create_file(vtism_kobj, &vtism_migration_enable_attr.attr);
    if (err) {
        pr_err("failed to create migration_enable file\n");
        goto delete_obj;
    }

    sysfs_attr_init(&vtism_demotion_min_free_ratio_attr.attr);
    vtism_demotion_min_free_ratio_attr.attr.name = "demotion_min_free_ratio";
    vtism_demotion_min_free_ratio_attr.attr.mode = 0666;
    vtism_demotion_min_free_ratio_attr.show = demotion_min_free_ratio_show;
    vtism_demotion_min_free_ratio_attr.store = demotion_min_free_ratio_store;
    err = sysfs_create_file(vtism_kobj, &vtism_demotion_min_free_ratio_attr.attr);
    if (err) {
        pr_err("failed to create demotion_min_free_ratio file\n");
        goto delete_obj;
    }

    sysfs_attr_init(&vtism_promotion_min_free_ratio_attr.attr);
    vtism_promotion_min_free_ratio_attr.attr.name = "promotion_min_free_ratio";
    vtism_promotion_min_free_ratio_attr.attr.mode = 0666;
    vtism_promotion_min_free_ratio_attr.show = promotion_min_free_ratio_show;
    vtism_promotion_min_free_ratio_attr.store = promotion_min_free_ratio_store;
    err = sysfs_create_file(vtism_kobj, &vtism_promotion_min_free_ratio_attr.attr);
    if (err) {
        pr_err("failed to create promotion_min_free_ratio file\n");
        goto delete_obj;
    }

    err = register_pcm_sysctl(vtism_kobj);
    if (err) {
        pr_err("failed to register pcm group\n");
        goto delete_obj;
    }

    return 0;
delete_obj:
    kobject_put(vtism_kobj);
    return err;
}

int vtismctl_exit(void) {
    kobject_del(vtism_kobj);
    unregister_pcm_sysctl();
    INFO("unload vtismctl module\n");
    return 0;
}