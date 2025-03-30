
#include "kvm.h"

#include <asm-generic/errno-base.h>
#include <linux/sort.h>
#include <linux/string.h>

#include "common.h"

struct vm_context qemu_vm = {0};
extern bool vtism_enable;

static int get_kvm_by_vpid(pid_t nr, struct kvm **kvmp, struct file **shared_filep) {
    struct pid *pid;
    struct task_struct *task;
    struct files_struct *files;
    int fd, max_fds;
    struct kvm *kvm = NULL;
    struct file *shared_file = NULL;
    rcu_read_lock();
    if (!(pid = find_vpid(nr))) {
        rcu_read_unlock();
        ERR("no such process whose pid = %d", nr);
        return -ESRCH;
    }
    if (!(task = pid_task(pid, PIDTYPE_PID))) {
        rcu_read_unlock();
        ERR("no such process whose pid = %d", nr);
        return -ESRCH;
    }
    files = task->files;
    max_fds = files_fdtable(files)->max_fds;
    for (fd = 0; fd < max_fds; fd++) {
        struct file *file;
        char buffer[32];
        char *fname;
        if (!(file = files_lookup_fd_rcu(files, fd)))
            continue;
        fname = d_path(&(file->f_path), buffer, sizeof(buffer));
        if (fname < buffer || fname >= buffer + sizeof(buffer))
            continue;
        if (strcmp(fname, "anon_inode:kvm-vm") == 0) {
            kvm = file->private_data;
            kvm_get_kvm(kvm);
        }
        // if start with /dev/shm, it is a shared memory file
        if (strncmp(fname, "/dev/shm", 8) == 0) {
            // open the shared memory file
            // DON'T use shared_file = file because guest vm could exit at any time
            shared_file = filp_open(fname, O_RDONLY, 0);
        }
    }
    rcu_read_unlock();
    if (!kvm) {
        ERR("qemu process (pid = %d) has no kvm", nr);
        return -EINVAL;
    }
    if (!shared_file) {
        ERR("qemu process (pid = %d) has no shared memory file", nr);
        return -EINVAL;
    }
    (*kvmp) = kvm;
    (*shared_filep) = shared_file;
    return 0;
}

static int get_ept_root(struct qemu_struct *qemu) {
    uint64_t root = 0;
    struct kvm *kvm = qemu->kvm;
    int i, vcpu_count = atomic_read(&(kvm->online_vcpus));
    for (i = 0; i < vcpu_count; i++) {
        struct kvm_vcpu *vcpu = kvm_get_vcpu(kvm, i);
        uint64_t root_of_vcpu;
        if (!vcpu) {
            ERR("vcpu[%d] of process (pid = %d) is uncreated", i, kvm->userspace_pid);
            return -EINVAL;
        }
        if (!(root_of_vcpu = vcpu->arch.mmu->root.hpa)) {
            ERR("vcpu[%d] is uninitialized", i);
            return -EINVAL;
        }
        if (!root)
            root = root_of_vcpu;
        else if (root != root_of_vcpu) {
            ERR("ept root of vcpu[%d] is %llx, different from other vcpus'", i, root_of_vcpu);
            return -EINVAL;
        }
    }
    qemu->ept_root = root ? (uint64_t *)__va(root) : NULL;
    qemu->vcpu = kvm_get_vcpu(kvm, 0);
    return 0;
}

static int compare_memslots(const void *a, const void *b) {
    // 比较 gpa 值
    uint64_t gpa_a = ((struct kvm_ept_memslot *)a)->gpa;
    uint64_t gpa_b = ((struct kvm_ept_memslot *)b)->gpa;

    if (gpa_a < gpa_b) {
        return -1;  // a 在 b 前
    } else if (gpa_a > gpa_b) {
        return 1;  // a 在 b 后
    } else {
        return 0;  // 相等
    }
}

static int get_kvm_ept(struct qemu_struct *qemu) {
    struct kvm_memslots *slots;
    struct kvm_memory_slot *ms;
    int bkt;
    int count = 0;

    slots = kvm_memslots(qemu->kvm);
    if (!slots || kvm_memslots_empty(slots))
        return -EINVAL;
    // get the number of memslots
    kvm_for_each_memslot(ms, bkt, slots) {
        count++;
    }
    qemu->count = count;
    qemu->memslots = (struct kvm_ept_memslot *)vzalloc(sizeof(struct kvm_ept_memslot) * count);

    // get the memslots, gpa and hva and nr_pages
    count = 0;
    kvm_for_each_memslot(ms, bkt, slots) {
        struct kvm_ept_memslot *dst = qemu->memslots + count;
        dst->gpa = ms->base_gfn << PAGE_SHIFT;
        dst->hva = ms->userspace_addr;
        dst->page_count = ms->npages;
        count++;
    }

    sort(qemu->memslots, qemu->count, sizeof(struct kvm_ept_memslot), compare_memslots, NULL);
    return 0;
}

static void show_ept(struct qemu_struct *qemu) {
    uint64_t total_pages = 0;
    for (size_t i = 0; i < qemu->count; i++) {
        struct kvm_ept_memslot *memslots = qemu->memslots;
        INFO("memslot[%lu]: gpa: %016llx, hva: %016llx, count: %lu\n",
             i,
             memslots[i].gpa,
             memslots[i].hva,
             memslots[i].page_count);
        total_pages += memslots[i].page_count;
    }
    INFO("total pages: %llu [%lld GB]\n", total_pages, total_pages * 4 / 1024 / 1024);
}

static int get_qemu_pid(void) {
    struct task_struct *task;
    int qemu_num = 0;
    for_each_process(task) {
        // task->comm contains the process name
        if (strncmp(task->comm, "qemu-system", 11) == 0) {
            if (qemu_num >= MAX_QEMU_VM) {
                return -1;
            }
            qemu_vm.qemu[qemu_num++].pid = task->pid;
        }
    }
    qemu_vm.qemu_num = qemu_num;
    return qemu_num;
}

uint64_t gpa2hva(struct qemu_struct *qemu, uint64_t gpa) {
    uint64_t hva;
    for (int i = 0; i < qemu->count; i++) {
        struct kvm_ept_memslot *memslot = qemu->memslots + i;
        if (gpa >= memslot->gpa && gpa < memslot->gpa + memslot->page_count * PAGE_SIZE) {
            hva = memslot->hva + (gpa - memslot->gpa);
            return hva;
        }
    }
    ERR("unexpected gpa: %llx\n", gpa);
    return 0;
}

ssize_t dump_vm_info(char *buf, ssize_t len) {
    len += sysfs_emit_at(buf, len, "[vm info]\n");
    if (!vtism_enable) {
        len += sysfs_emit_at(
            buf, len, "not enable vtism qemu info, please run qemu first and enable vtism\n");
        len += sysfs_emit_at(
            buf, len, "use \"echo 1 > /sys/kernel/mm/vtism/enable\" to get qemu info\n");
    } else {
        len += sysfs_emit_at(buf, len, "qemu_num: %d\n", qemu_vm.qemu_num);
        for (int i = 0; i < qemu_vm.qemu_num; i++) {
            len += sysfs_emit_at(buf, len, "  qemu[%d]: pid: %d\n", i, qemu_vm.qemu[i].pid);
            struct qemu_struct *qemu = &qemu_vm.qemu[i];
            uint64_t total_pages = 0;
            for (size_t i = 0; i < qemu->count; i++) {
                struct kvm_ept_memslot *memslots = qemu->memslots;
                len += sysfs_emit_at(buf,
                                     len,
                                     "  memslot[%lu]: gpa: %016llx, hva: %016llx, count: %lu\n",
                                     i,
                                     memslots[i].gpa,
                                     memslots[i].hva,
                                     memslots[i].page_count);
                total_pages += memslots[i].page_count;
            }
            len += sysfs_emit_at(buf,
                                 len,
                                 "  total pages: %llu [%lld GB]\n",
                                 total_pages,
                                 total_pages * 4 / 1024 / 1024);
        }
    }
    len += sysfs_emit_at(buf, len, "\n");
    return len;
}

int init_qemu(struct qemu_struct *qemu) {
    int ret;
    if ((ret = get_kvm_by_vpid(qemu->pid, &qemu->kvm, &qemu->shared_file) < 0)) {
        ERR("get_kvm_by_vpid failed\n");
        return ret;
    }

    if ((ret = get_kvm_ept(qemu)) < 0) {
        kvm_put_kvm(qemu->kvm);
        ERR("get_ept failed\n");
        return ret;
    }
    show_ept(qemu);

    if ((ret = get_ept_root(qemu)) < 0) {
        kvm_put_kvm(qemu->kvm);
        ERR("get_ept_root failed\n");
        return ret;
    }
    qemu->pml_buffer = vmalloc(PML_BUFFER_LEN * sizeof(uint64_t));
    qemu->pml_buffer_idx = 0;
    INFO("init pml buffer with %lu bytes\n", PML_BUFFER_LEN * sizeof(uint64_t));
    return ret;
}

int init_vm(void) {
    // uint64_t *ept_root = kvm_struct->ept_root;
    int ret;
    int qemu_num = get_qemu_pid();
    if (qemu_num == 0) {
        ERR("no qemu process found\n");
        return -ESRCH;
    } else if (qemu_num == -1) {
        ERR("too many qemu processes\n");
        return -EPERM;
    } else {
        INFO("find %d qemu process\n", qemu_num);
        for (int i = 0; i < qemu_num; i++) {
            INFO("  [%d] pid: %d\n", i, qemu_vm.qemu[i].pid);
        }
    }

    for (int i = 0; i < qemu_num; i++) {
        struct qemu_struct *qemu = &qemu_vm.qemu[i];
        if ((ret = init_qemu(qemu)) < 0) {
            ERR("init qemu[%d] failed\n", i);
            return ret;
        }
    }
    
    return ret;
}

void destory_qemu(struct qemu_struct *qemu) {
    kvm_put_kvm(qemu->kvm);
    filp_close(qemu->shared_file, NULL);
    vfree(qemu->pml_buffer);
    qemu->pml_buffer = NULL;
    qemu->pml_buffer_idx = 0;
}

int destory_vm(void) {
    for (int i = 0; i < qemu_vm.qemu_num; i++) {
        struct qemu_struct *qemu = &qemu_vm.qemu[i];
        destory_qemu(qemu);
    }
    qemu_vm.qemu_num = 0;
    return 0;
}
