/*
 *Copyright (c) 2024 All rights reserved
 *@description: page demotion when system free dram is not enough
 *@author: Zhixing Lu
 *@date: 2024-12-16
 *@email: luzhixing12345@163.com
 *@Github: luzhixing12345
 */
#include "page_migration.h"

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/migrate.h>
#include <linux/mm_inline.h>
#include <linux/mmzone.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/page_owner.h>
#include <linux/sched.h>
#include <linux/sysfs.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#include "common.h"
#include "linux/topology.h"

// MAX_NUMNODES

struct workqueue_struct *promote_wq[MAX_NUMA_NODES];
static struct workqueue_struct *demote_wq[MAX_NUMA_NODES];

static int folio_to_nid(struct folio *folio) {
    return page_to_nid(&folio->page);
}

/**
 * @brief use memcpy instead of copy_highpage to migrate folio(folio_migrate_copy)
 *
 * @param dst
 * @param src
 */
void folio_migrate_memcpy(struct folio *dst, struct folio *src) {
    void *src_addr = folio_address(src);
    void *dst_addr = folio_address(dst);
    size_t size = folio_size(src);

    if (likely(src_addr && dst_addr)) {
        memcpy(dst_addr, src_addr, size);
    } else {
        // 回退到逐页复制
        ERR("still use copy_highpage to migrate folio\n");
        long i = 0;
        long nr = folio_nr_pages(src);
        for (;;) {
            copy_highpage(folio_page(dst, i), folio_page(src, i));
            if (++i == nr)
                break;
            cond_resched();
        }
    }
}

void migration_work_func(struct work_struct *work) {
    struct migration_work *mwork = container_of(work, struct migration_work, work);
    struct folio *dst = mwork->dst;
    struct folio *src = mwork->src;
    enum migrate_reason reason = mwork->reason;
    void *dst_addr = folio_address(dst);

    // INFO("[%s] Migrating folio %px -> %px on CPU %d (NUMA %d -> %d)\n",
    //      reason == MR_DEMOTION ? "demote" : "promote",
    //      src,
    //      dst,
    //      smp_processor_id(),
    //      folio_to_nid(src),
    //      folio_to_nid(dst));

    // copy pages
    folio_migrate_memcpy(dst, src);
    folio_migrate_flags(dst, src);

    // Promote 任务：迁移完成后预取页面
    if (reason == MR_NUMA_MISPLACED && dst_addr) {
        // INFO("Prefetching new page %px on CPU %d\n", dst_addr, smp_processor_id());
        prefetch(dst_addr);
    }
}

/**
 * @brief parallel migration with multi kthread copy instead of cond_sched(migrate_folio)
 * this function will be called from move_to_new_folio(mm/migrate.c) when vtism_migration_enable is
 * true
 */

int async_migrate_folio(struct address_space *mapping, struct folio *dst, struct folio *src,
                        enum migrate_reason reason) {
    int rc;
    struct migration_work *mwork;

    BUG_ON(folio_test_writeback(src)); /* Writeback must be complete */

    rc = folio_migrate_mapping(mapping, dst, src, 0);

    if (rc != MIGRATEPAGE_SUCCESS)
        return rc;

    mwork = kmalloc(sizeof(struct migration_work), GFP_KERNEL);
    if (!mwork)
        return -ENOMEM;

    INIT_WORK(&mwork->work, migration_work_func);
    mwork->dst = dst;
    mwork->src = src;
    mwork->reason = reason;
    // init_completion(&mwork->done);

    int src_node = folio_to_nid(src);
    int dst_node = folio_to_nid(dst);

    // When the page is promoted, the task is submitted to promote_wq of the NUMA node where dst is
    // located
    //
    // When a page is demoted, the task is submitted to demote_wq on the NUMA node where src
    // is located.

    if (reason == MR_NUMA_MISPLACED) {
        // promote
        if (dst_node < MAX_NUMA_NODES && promote_wq[dst_node])
            queue_work(promote_wq[dst_node], &mwork->work);
        else
            queue_work(promote_wq[0], &mwork->work);  // 兜底
    } else if (reason == MR_DEMOTION) {
        // demote
        if (src_node < MAX_NUMA_NODES && demote_wq[src_node])
            queue_work(demote_wq[src_node], &mwork->work);
        else
            queue_work(demote_wq[0], &mwork->work);  // 兜底
    } else {
        pr_err("Unknown migration reason: %d\n", reason);
        BUG();
    }

    // wait_for_completion(&mwork->done);
    // flush_work(&mwork->work);
    kfree(mwork);
    return MIGRATEPAGE_SUCCESS;
}

ssize_t dump_page_migration_info(char *buf, ssize_t len) {
    len += sysfs_emit_at(buf, len, "[page migration info]\n");
    if (vtism_migration_enable) {
        len += sysfs_emit_at(buf, len, "promote and demote wq\n");
    } else {
        len += sysfs_emit_at(buf, len, "migration disable\n");
    }
    len += sysfs_emit_at(buf, len, "\n");
    return len;
}

int set_workqueue_numa_affinity(struct workqueue_struct *wq, int node) {
    int err;
    struct workqueue_attrs *attrs = alloc_workqueue_attrs();
    if (!attrs) {
        ERR("Failed to allocate workqueue attrs\n");
        return -ENOMEM;
    }
    // Set CPU mask to allow only CPUs on the specified NUMA node
    cpumask_clear(attrs->cpumask);  // Clear any existing CPUs
    const struct cpumask *cpus = cpumask_of_node(node);
    int i;
    for_each_cpu(i, cpus) {
        INFO("CPU %d on node %d\n", i, node);
    }
    cpumask_or(attrs->cpumask, attrs->cpumask, cpus);

    // Set affinity scope to NUMA
    attrs->affn_scope = WQ_AFFN_NUMA;

    // Optionally set affn_strict to true if you want strict affinity
    attrs->affn_strict = true;
    err = apply_workqueue_attrs(wq, attrs);
    if (err) {
        ERR("Failed to apply workqueue attrs for node %d\n", node);
        free_workqueue_attrs(attrs);
        return err;
    }
    free_workqueue_attrs(attrs);
    return err;
}

int page_migration_init(void) {
    // https://docs.kernel.org/core-api/workqueue.html
    int node;
    int err;
    // init workqueue for each node which has CPU
    for_each_node_with_cpus(node) {
        promote_wq[node] =
            alloc_workqueue("promote_wq_%d", WQ_HIGHPRI | WQ_UNBOUND | WQ_MEM_RECLAIM, 0, node);
        if (!promote_wq[node]) {
            ERR("Failed to create promote_wq for node %d\n", node);
            return -ENOMEM;
        }

        demote_wq[node] = alloc_workqueue("demote_wq_%d", WQ_UNBOUND | WQ_MEM_RECLAIM, 0, node);
        if (!demote_wq[node]) {
            ERR("Failed to create demote_wq for node %d\n", node);
            destroy_workqueue(promote_wq[node]);
            return -ENOMEM;
        }

        // set workqueue attributes, numa affinity
        err = set_workqueue_numa_affinity(promote_wq[node], node);
        if (err) {
            ERR("Failed to set workqueue cpu affinity for promote_wq on node %d\n", node);
            destroy_workqueue(promote_wq[node]);
            destroy_workqueue(demote_wq[node]);
            return err;
        }
        err = set_workqueue_numa_affinity(demote_wq[node], node);
        if (err) {
            ERR("Failed to set workqueue cpu affinity for demote_wq on node %d\n", node);
            destroy_workqueue(promote_wq[node]);
            destroy_workqueue(demote_wq[node]);
        }
        INFO("Created promote and demote workqueue for node %d\n", node);
    }

    return 0;
}

void page_migration_exit(void) {
    int node;
    for_each_online_node(node) {
        if (promote_wq[node])
            destroy_workqueue(promote_wq[node]);
        if (demote_wq[node])
            destroy_workqueue(demote_wq[node]);
    }
    INFO("Exit page migration module.\n");
}