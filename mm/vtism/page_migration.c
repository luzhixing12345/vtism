/*
 *Copyright (c) 2024 All rights reserved
 *@description: page demotion when system free dram is not enough
 *@author: Zhixing Lu
 *@date: 2024-12-16
 *@email: luzhixing12345@163.com
 *@Github: luzhixing12345
 */
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/migrate.h>
#include <linux/mmzone.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/sysfs.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#include "common.h"

int migration_thread_num = 4;  // 默认线程数量

static struct task_struct **migration_threads;  // 动态分配的线程指针数组
static int *thread_ids;

/* 队列头 */
static wait_queue_head_t wq;
static LIST_HEAD(submission_queue);
static LIST_HEAD(completion_queue);
static DEFINE_MUTEX(submission_lock);
static DEFINE_MUTEX(completion_lock);
static DECLARE_COMPLETION(copy_completion);

struct migration_task {
    struct list_head list;
    struct folio *src;
    struct folio *dst;
};

// 修改后的队列操作函数
static int add_migration_task(struct folio *src, struct folio *dst) {
    struct migration_task *task = kmalloc(sizeof(struct migration_task), GFP_KERNEL);
    if (!task)
        return -ENOMEM;

    task->src = src;
    task->dst = dst;

    mutex_lock(&submission_lock);
    list_add_tail(&task->list, &submission_queue);
    mutex_unlock(&submission_lock);

    wake_up_interruptible(&wq);
    return 0;
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

// 修改后的迁移线程
static int migration_thread(void *data) {
    int thread_id = *(int *)data;
    INFO("Migration Thread %d: started\n", thread_id);

    while (!kthread_should_stop()) {
        struct migration_task *task = NULL;

        wait_event_interruptible(wq, !list_empty(&submission_queue) || kthread_should_stop());

        if (kthread_should_stop())
            break;

        // 从请求队列获取任务
        mutex_lock(&submission_lock);
        if (!list_empty(&submission_queue)) {
            task = list_first_entry(&submission_queue, struct migration_task, list);
            list_del(&task->list);
        }
        mutex_unlock(&submission_lock);

        if (task) {
            // 执行实际的迁移拷贝
            INFO("Thread %d: Migrating %px -> %px\n", thread_id, task->src, task->dst);

            // 执行核心拷贝操作
            folio_migrate_memcpy(task->dst, task->src);
            folio_migrate_flags(task->dst, task->src);

            // 加入完成队列
            mutex_lock(&completion_lock);
            list_add_tail(&task->list, &completion_queue);
            mutex_unlock(&completion_lock);

            complete(&copy_completion);  // 通知完成
        }

        cond_resched();
    }
    return 0;
}

static int get_migration_queue_size(void) {
    int len = 0;
    struct list_head *entry;
    if (list_empty(&submission_queue)) {
        return 0;
    }
    mutex_lock(&submission_lock);
    list_for_each(entry, &submission_queue) {
        len++;
    }
    mutex_unlock(&submission_lock);
    return len;
}

int async_folio_migrate_copy(struct folio *newfolio, struct folio *folio) {
    // 将迁移任务加入队列
    int ret;
    ret = add_migration_task(folio, newfolio);
    if (ret < 0) {
        ERR("Failed to add migration task\n");
    }
    return ret;
}

void wait_async_copy_complete(struct folio *dst) {
    while (1) {
        struct migration_task *task, *tmp;
        bool found = false;

        mutex_lock(&completion_lock);
        list_for_each_entry_safe(task, tmp, &completion_queue, list) {
            // 找到已完成的任务, 并从队列中移除
            if (task->dst == dst) {
                found = true;
                list_del(&task->list);
                kfree(task);
                break;
            }
        }
        mutex_unlock(&completion_lock);

        if (found)
            break;
        wait_for_completion_interruptible(&copy_completion);
        // wait_for_completion_interruptible_timeout(&copy_completion, msecs_to_jiffies(1000));
    }
}

/**
 * @brief real async migration with multi kthread copy instead of cond_sched(migrate_folio)
 *
 * @param mapping
 * @param dst
 * @param src
 * @param mode
 * @return int
 */
int async_migrate_folio(struct address_space *mapping, struct folio *dst, struct folio *src,
                        enum migrate_mode mode) {
    int rc;

    BUG_ON(folio_test_writeback(src)); /* Writeback must be complete */

    rc = folio_migrate_mapping(mapping, dst, src, 0);

    if (rc != MIGRATEPAGE_SUCCESS)
        return rc;

    rc = async_folio_migrate_copy(dst, src);
    if (rc != MIGRATEPAGE_SUCCESS)
        return rc;

    wait_async_copy_complete(dst);
    return MIGRATEPAGE_SUCCESS;
}

ssize_t dump_page_migration_info(char *buf, ssize_t len) {
    len += sysfs_emit_at(buf, len, "[page migration info]\n");
    if (migration_threads == NULL) {
        len += sysfs_emit_at(buf, len, "not start page migration thread\n");
        len += sysfs_emit_at(buf, len, "\n");
        return len;
    }
    len += sysfs_emit_at(buf, len, "%d migration threads are running\n", migration_thread_num);
    for (int i = 0; i < migration_thread_num; i++) {
        len += sysfs_emit_at(
            buf, len, "  [%d]: %s(%d)\n", i, migration_threads[i]->comm, migration_threads[i]->pid);
    }
    len += sysfs_emit_at(buf, len, "migration queue size: %d\n", get_migration_queue_size());
    len += sysfs_emit_at(buf, len, "\n");
    return len;
}

int page_migration_init(void) {
    int i;
    

    // 校验线程数量
    if (migration_thread_num <= 0) {
        ERR("Invalid migration_thread_num: %d\n", migration_thread_num);
        return -EINVAL;
    }

    // 动态分配线程指针数组
    migration_threads = kmalloc_array(migration_thread_num, sizeof(struct task_struct *), GFP_KERNEL);
    if (!migration_threads) {
        ERR("Failed to allocate memory for migration_threads\n");
        return -ENOMEM;
    }

    // 动态分配线程 ID 数组
    thread_ids = kmalloc_array(migration_thread_num, sizeof(int), GFP_KERNEL);
    if (!thread_ids) {
        ERR("Failed to allocate memory for thread_ids\n");
        kfree(migration_threads);
        return -ENOMEM;
    }

    init_waitqueue_head(&wq);

    // 创建线程
    for (i = 0; i < migration_thread_num; i++) {
        thread_ids[i] = i;
        migration_threads[i] = kthread_run(migration_thread, &thread_ids[i], "kmigration%d", i);
        if (IS_ERR(migration_threads[i])) {
            ERR("Failed to create migration thread %d\n", i);
            // 清理已创建的线程
            while (i-- > 0) {
                kthread_stop(migration_threads[i]);
            }
            kfree(thread_ids);
            kfree(migration_threads);
            return PTR_ERR(migration_threads[i]);
        }
    }

    INFO("Init page migration module with %d threads.\n", migration_thread_num);
    return 0;
}

void page_migration_exit(void) {
    int i;

    // 等待所有任务完成
    int wait_time = 0;
    while (!list_empty(&submission_queue)) {
        INFO("waiting for migration queue to be empty Time/queue_size: %d/%d\n",
             wait_time,
             get_migration_queue_size());
        msleep(1000);
        wait_time += 1;
    }

    // 停止所有线程
    if (migration_threads) {
        for (i = 0; i < migration_thread_num; i++) {
            if (migration_threads[i]) {
                kthread_stop(migration_threads[i]);
            }
        }
        kfree(migration_threads);  // 释放线程指针数组
    }
    if (thread_ids) {
        kfree(thread_ids);
    }

    INFO("Exit page migration module.\n");
}
