
#include <linux/kobject.h>
#include <linux/lockdep.h>
#include <linux/memory-tiers.h>
#include <linux/memory.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

#include "../common.h"
#include "linux/types.h"
#include "pcm.h"

#define VTISM_VM_PIDS_MAX CONFIG_VTISM_VM_PIDS_MAX

unsigned int vtism_vm_pids[VTISM_VM_PIDS_MAX] = {0};
unsigned int vtism_vm_pids_count = 0;

unsigned int vtism_enable = 1;

static ssize_t enable_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    return sysfs_emit(buf, "%u\n", vtism_enable);
}
static ssize_t enable_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count) {
    int ret;
    ret = kstrtouint(buf, 10, &vtism_enable);
    if (ret < 0)
        return ret;
    return count;
}
static struct kobj_attribute vtism_enable_attr = __ATTR_RW(enable);

static ssize_t vm_pids_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    ssize_t len = 0;
    int i;

    for (i = 0; i < VTISM_VM_PIDS_MAX; i++) len += sysfs_emit_at(buf, len, "%d ", vtism_vm_pids[i]);

    len += sysfs_emit_at(buf, len, "\n");  // Add a newline at the end
    return len;
}

static ssize_t vm_pids_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count) {
    int ret;

    if (vtism_vm_pids_count >= VTISM_VM_PIDS_MAX)
        return -EINVAL;

    ret = kstrtouint(buf, 10, &vtism_vm_pids[vtism_vm_pids_count++]);
    if (ret < 0)
        return ret;

    return count;
}

static struct kobj_attribute vtism_vm_pids_attr = __ATTR_RW(vm_pids);

struct demotion_nodes {
	nodemask_t target_demotion_nodes;
};
extern struct demotion_nodes *node_demotion;
ssize_t dump_demotion_target(char *buf) {
    ssize_t len = 0;
    int node, nid;

    for_each_node_state(node, N_MEMORY) {
        nodemask_t target_demotion_nodes = node_demotion[node].target_demotion_nodes;
        if (nodes_empty(target_demotion_nodes)) {
            len += sysfs_emit_at(buf, len, "node %d has no demotion target\n", node);
        } else {
            len += sysfs_emit_at(buf, len, "node %d demotion target: ", node);
            for_each_node_mask(nid, target_demotion_nodes) {
                len += sysfs_emit_at(buf, len, "%d ", nid);
            }
            len += sysfs_emit_at(buf, len, "\n");
        }
    }

    return len;
}

static ssize_t dump_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    ssize_t len = dump_demotion_target(buf);
    return len;
}

static struct kobj_attribute vtism_dump_attr = __ATTR_RO(dump);

static struct attribute *vtism_attrs[] = {
    &vtism_enable_attr.attr,
    &vtism_vm_pids_attr.attr,
    &vtism_dump_attr.attr,
    NULL,
};
static const struct attribute_group vtism_attr_group = {
    .attrs = vtism_attrs,
};

/*

kernel system interface for vtism (/sys/kernel/mm/vtism)

*/

static int __init vtism_init(void) {
    int err;
    struct kobject *vtism_kobj;
    vtism_kobj = kobject_create_and_add("vtism", mm_kobj);
    if (!vtism_kobj) {
        pr_err("failed to create vtism kobject\n");
        return -ENOMEM;
    }
    err = sysfs_create_group(vtism_kobj, &vtism_attr_group);
    if (err) {
        pr_err("failed to register vtism group\n");
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
subsys_initcall(vtism_init);
