/*
 *Copyright (c) 2024 All rights reserved
 *@description: page demotion when system free dram is not enough
 *@author: Zhixing Lu
 *@date: 2024-12-16
 *@email: luzhixing12345@163.com
 *@Github: luzhixing12345
 */
#include <linux/hrtimer.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/migrate.h>
#include <linux/mmzone.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sysfs.h>

#include "common.h"
#include "linux/atomic/atomic-instrumented.h"
#include "linux/cpumask.h"

#define SCAN_INTERVAL_MS 1000  // 定时器间隔
#define LOW_WATERMARK    100   // 冷页面迁移阈值

static struct hrtimer demotion_timer;
static bool pgdat_free_space_enough(struct pglist_data *pgdat) {
    int z;
    unsigned long enough_wmark;

    enough_wmark = max(1UL * 1024 * 1024 * 1024 >> PAGE_SHIFT, pgdat->node_present_pages >> 4);
    for (z = pgdat->nr_zones - 1; z >= 0; z--) {
        struct zone *zone = pgdat->node_zones + z;

        if (!populated_zone(zone))
            continue;

        if (zone_watermark_ok(zone, 0, wmark_pages(zone, WMARK_PROMO) + enough_wmark, ZONE_MOVABLE, 0))
            return true;
    }
    return false;
}

static enum hrtimer_restart demotion_scan(struct hrtimer *timer) {
    int nid;

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
        INFO("Node %d: total = %lu KB, free = %lu KB\n",
             nid, total_pages << (PAGE_SHIFT - 10), free_pages << (PAGE_SHIFT - 10));
        // unsigned long watermark = free_pages * 1000 / total_pages;
        // if (watermark < LOW_WATERMARK) {
        //     // 进行冷页面迁移
        // }
    }

    // 重新启动定时器
    hrtimer_forward_now(timer, ms_to_ktime(SCAN_INTERVAL_MS));
    return HRTIMER_RESTART;
}

int page_demotion_init(void) {
    ktime_t interval = ms_to_ktime(SCAN_INTERVAL_MS);
    // 初始化定时器
    hrtimer_init(&demotion_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    demotion_timer.function = demotion_scan;
    hrtimer_start(&demotion_timer, interval, HRTIMER_MODE_REL);

    pr_info("page_demotion module loaded.\n");
    return 0;
}

void page_demotion_exit(void) {
    hrtimer_cancel(&demotion_timer);
    pr_info("page_demotion module unloaded.\n");
}

// 冷页面迁移逻辑 (示例)
static void migrate_cold_pages(struct zone *zone) {
    // 遍历 zone 内存页,并选择冷页面进行迁移
    // 实际实现需要结合页面引用计数等信息
    // 迁移接口:migrate_pages()
    pr_info("Migrating cold pages in zone: %s\n", zone->name);
}
