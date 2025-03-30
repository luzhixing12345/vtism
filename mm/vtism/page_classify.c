/*
 *Copyright (c) 2024 All rights reserved
 *@description: classify page from qemu vms
 *@author: Zhixing Lu
 *@date: 2024-12-16
 *@email: luzhixing12345@163.com
 *@Github: luzhixing12345
 */

#include "page_classify.h"

#include <linux/delay.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/hrtimer.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/kvm_host.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <uapi/linux/sched/types.h>

#include "common.h"
#include "kvm.h"
#include "linux/swap.h"
#include "linux/sysfs.h"

// each qemu vm(32GB) needs a 64MB shared memory buffer
#define BUFFER_SIZE (64 * MB)

void *pte_addr;
uint64_t *last_ids;
static struct task_struct *page_classify_thread = NULL;
static struct hrtimer page_classify_hrtimer;
static unsigned int thread_interval_ms = 5000;
struct ivshmem_head {
    uint64_t id;
    uint64_t pte_num;
};

extern struct vm_context qemu_vm;

// static inline int folio_lru_refs(struct folio *folio) {
//     unsigned long flags = READ_ONCE(folio->flags);
//     bool workingset = flags & BIT(PG_workingset);

//     /*
//      * Return the number of accesses beyond PG_referenced, i.e., N-1 if the
//      * total number of accesses is N>1, since N=0,1 both map to the first
//      * tier. lru_tier_from_refs() will account for this off-by-one. Also see
//      * the comment on MAX_NR_TIERS.
//      */
//     return ((flags & LRU_REFS_MASK) >> LRU_REFS_PGOFF) + workingset;
// }

// int get_page_type(struct folio *folio) {
//     // struct folio *folio = pfn_folio(pte_pfn(*pte));
//     enum page_type page_type;
//     if (unlikely(folio_test_ksm(folio))) {
//         page_type = UNKNOWN_PAGE;
//     } else if (folio_test_anon(folio)) {
//         page_type = ANON_PAGE;
//     } else {
//         page_type = FILE_PAGE;
//     }
//     return page_type;
// }

struct page *get_kvm_page(struct kvm *kvm, uint64_t hva) {
    struct vm_area_struct *vma;
    struct page *page = NULL;

    vma = find_vma(kvm->mm, hva);
    if (!vma) {
        ERR("not found vma: %016llx\n", hva);
        return NULL;
    }
    page = follow_page(vma, hva, FOLL_GET);
    return page;
}

ssize_t dump_page_classify_info(char *buf, ssize_t len) {
    len += sysfs_emit_at(buf, len, "[page classify info]\n");
    if (page_classify_thread == NULL) {
        len += sysfs_emit_at(buf, len, "not start page classify thread\n");
        len += sysfs_emit_at(buf, len, "\n");
        return len;
    }
    len += sysfs_emit_at(buf, len, "kclassify thread is running\n");
    for (int i = 0; i < qemu_vm.qemu_num; i++) {
        struct qemu_struct *qemu = &qemu_vm.qemu[i];
        if (qemu->pte_num == 0) {
            len += sysfs_emit_at(buf, len, "no data passed from qemu[%d]\n", i);
        } else {
            len += sysfs_emit_at(buf, len, "qemu[%d] pte_num: %lld\n", i, qemu->pte_num);
        }
    }
    len += sysfs_emit_at(buf, len, "\n");
    return len;
}

int get_kvm_pages(void) {
    for (int i = 0; i < qemu_vm.qemu_num; i++) {
        struct qemu_struct *qemu = &qemu_vm.qemu[i];
        uint64_t *qemu_pte_addr = pte_addr + i * BUFFER_SIZE;
        if (qemu->pte_num) {
            INFO("qemu[%d] pte_num: %llu\n", i, qemu->pte_num);
            for (int j = 0; j < qemu->pte_num; j++) {
                uint64_t hva = gfn_to_hva(qemu->kvm, gpa_to_gfn(qemu_pte_addr[j]));
                if (kvm_is_error_hva(hva)) {
                    ERR("gfn_to_hva: hva[%d]: %016llx\n", j, hva);
                    return -EINVAL;
                }
                struct page *page = get_kvm_page(qemu->kvm, hva);
                if (IS_ERR_OR_NULL(page)) {
                    ERR("page not found: %016llx\n", hva);
                    continue;
                }
                folio_mark_accessed(page_folio(page));
                put_page(page);
            }
        }
    }
    return 0;
}

int shm_read_data(void *data) {
    pte_addr = vmalloc(BUFFER_SIZE * qemu_vm.qemu_num);
    if (!pte_addr) {
        ERR("Failed to allocate memory for buffer.\n");
        return -ENOMEM;
    }

    last_ids = vzalloc(qemu_vm.qemu_num * sizeof(uint64_t));
    if (!last_ids) {
        ERR("Failed to allocate memory for last ids.\n");
        return -ENOMEM;
    }
    while (!kthread_should_stop()) {
        ssize_t bytes_read;
        loff_t pos = 0;

        struct ivshmem_head head;
        for (int i = 0; i < qemu_vm.qemu_num; i++) {
            struct qemu_struct *qemu = &qemu_vm.qemu[i];
            struct file *shared_file = qemu->shared_file;
            pos = i * BUFFER_SIZE;
            bytes_read = kernel_read(shared_file, &head, sizeof(head), &pos);
            if (bytes_read < 0) {
                ERR("Failed to read head data from file.\n");
                return -1;
            }
            if (last_ids[i] == head.id) {
                qemu->pte_num = 0;
                continue;
            }
            last_ids[i] = head.id;
            qemu->pte_num = head.pte_num;
            INFO("qemu[%d] id: %lld, pte_num: %lld\n", i, head.id, head.pte_num);
            uint64_t pte_addr_size = head.pte_num * sizeof(pt_addr_t);
            if (pte_addr_size > BUFFER_SIZE) {
                ERR("Buffer size is not enough.\n");
                pte_addr_size = BUFFER_SIZE;
                head.pte_num = pte_addr_size / sizeof(pt_addr_t);
            }
            bytes_read = kernel_read(shared_file, pte_addr + i * BUFFER_SIZE, pte_addr_size, &pos);
            if (bytes_read < 0) {
                ERR("Failed to read data from file.\n");
                return -1;
            }
        }

        get_kvm_pages();
        set_current_state(TASK_INTERRUPTIBLE);
        schedule();
    }
    return 0;
}

static enum hrtimer_restart page_classify_hrtimer_fn(struct hrtimer *timer) {
    if (page_classify_thread)
        wake_up_process(page_classify_thread);

    hrtimer_forward_now(timer, ms_to_ktime(thread_interval_ms));
    return HRTIMER_RESTART;
}

int page_classify_init(void) {
    int ret = 0;
    ret = init_vm();
    if (ret < 0) {
        ERR("init_vm failed\n");
        return ret;
    }
    INFO("init vm success\n");
    page_classify_thread = kthread_run(shm_read_data, NULL, "kclassify");
    if (IS_ERR(page_classify_thread)) {
        ERR("Failed to create page walk thread.\n");
        return PTR_ERR(page_classify_thread);
    }
    INFO("start page classify thread\n");

    hrtimer_init(&page_classify_hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    page_classify_hrtimer.function = page_classify_hrtimer_fn;
    hrtimer_start(&page_classify_hrtimer, ms_to_ktime(thread_interval_ms), HRTIMER_MODE_REL);

    INFO("Init page classify module.\n");
    return 0;
}

void page_classify_exit(void) {
    // if thread is still running
    if (!IS_ERR(page_classify_thread)) {
        kthread_stop(page_classify_thread);
        page_classify_thread = NULL;
        INFO("page classify thread stopped.\n");
    }
    hrtimer_cancel(&page_classify_hrtimer);
    destory_vm();
    if (pte_addr)
        vfree(pte_addr);
    if (last_ids)
        vfree(last_ids);

    INFO("page classify module unloaded.\n");
}