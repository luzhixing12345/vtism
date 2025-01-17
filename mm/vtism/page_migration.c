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

#define MIGRATION_THREAD_NUM CONFIG_VTISM_MIGRATION_THREAD_NUM
static struct task_struct *migration_threads[MIGRATION_THREAD_NUM];

/* 定义一个队列节点 */
struct queue_node {
    int data;
    struct list_head list;
};

/* 队列头 */
static wait_queue_head_t wq;
static LIST_HEAD(migration_queue_head);
static DEFINE_MUTEX(queue_lock);

static int migration_thread(void *data) {
    int thread_id = *(int *)data;
    INFO("Migration Thread %d: started and entering sleep.\n", thread_id);
    while (!kthread_should_stop()) {
        wait_event_interruptible(wq, !list_empty(&migration_queue_head) || kthread_should_stop());

        if (kthread_should_stop())
            break;

        mutex_lock(&queue_lock);
        if (!list_empty(&migration_queue_head)) {
            /* 从队列中取出第一个元素 */
            struct queue_node *node = list_first_entry(&migration_queue_head, struct queue_node, list);
            int value = node->data;

            /* 删除队列节点并释放内存 */
            list_del(&node->list);
            kfree(node);

            INFO("Migration Thread %d: processing value %d\n", thread_id, value);
        }
        mutex_unlock(&queue_lock);
    }

    INFO("Migration Thread %d: exiting.\n", thread_id);
    return 0;
}

static int get_migration_queue_size(void) {
    int len = 0;
    struct list_head *entry;
    if (list_empty(&migration_queue_head)) {
        return 0;
    }
    mutex_lock(&queue_lock);
    list_for_each(entry, &migration_queue_head) {
        len++;
    }
    mutex_unlock(&queue_lock);
    return len;
}

ssize_t dump_page_migration_info(char *buf, ssize_t len) {
    len += sysfs_emit_at(buf, len, "[page migration info]\n");
    if (migration_threads[0] == NULL) {
        len += sysfs_emit_at(buf, len, "not start page migration thread\n");
        len += sysfs_emit_at(buf, len, "\n");
        return len;
    }
    len += sysfs_emit_at(buf, len, "%d migration threads are running\n", MIGRATION_THREAD_NUM);
    for (int i = 0; i < MIGRATION_THREAD_NUM; i++) {
        len += sysfs_emit_at(buf, len, "  [%d]: %s(%d)\n", i, migration_threads[i]->comm, migration_threads[i]->pid);
    }
    len += sysfs_emit_at(buf, len, "migration queue size: %d\n", get_migration_queue_size());
    len += sysfs_emit_at(buf, len, "\n");
    return len;
}

int page_migration_init(void) {
    init_waitqueue_head(&wq);

    for (int i = 0; i < MIGRATION_THREAD_NUM; i++) {
        migration_threads[i] = kthread_run(migration_thread, NULL, "migration_thread_%d", i);
        if (IS_ERR(migration_threads[i])) {
            ERR("Failed to create migration thread %d\n", i);
            return PTR_ERR(migration_threads[i]);
        }
    }
    INFO("Init page migration module.\n");
    return 0;
}

void page_migration_exit(void) {
    // wait for all queue nodes to be processed
    int wait_time = 0;
    while (!list_empty(&migration_queue_head)) {
        INFO("waiting for migration queue to be empty Time/queue_size: %d/%d\n", wait_time, get_migration_queue_size());
        msleep(1000);
        wait_time += 1;
    }

    for (int i = 0; i < MIGRATION_THREAD_NUM; i++) {
        kthread_stop(migration_threads[i]);
        migration_threads[i] = NULL;
    }

    INFO("Exit page migration module.\n");
}
