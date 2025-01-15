

#pragma once

#include <linux/fdtable.h>
#include <linux/kvm_host.h>
#include <linux/timer.h>

struct kvm_ept_memslot {
    uint64_t gpa;       // the base GPA
    uint64_t hva;       // the base HVA
    size_t page_count;  // count of pages in the slot
};

#define KVM_EPT_MEMSLOTS_CAPACITY 64

struct qemu_struct {
    struct kvm *kvm;
    struct kvm_vcpu *vcpu;
    uint64_t *ept_root;
    struct kvm_ept_memslot *memslots;  // the array of slots
    size_t count;                      // the actual count of the array
    pid_t pid;
    struct file *shared_file;
    uint64_t pte_num;
};

#define MAX_QEMU_VM 32
struct vm_context {
    struct qemu_struct qemu[MAX_QEMU_VM];
    int qemu_num;
};

int init_vm(void);
int destory_vm(void);
uint64_t gpa2hva(struct qemu_struct *qemu, uint64_t gpa);
ssize_t dump_vm_info(char *buf, ssize_t len);