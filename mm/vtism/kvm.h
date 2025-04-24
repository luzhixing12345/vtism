

#pragma once

#include <linux/fdtable.h>
#include <linux/kvm_host.h>
#include <linux/timer.h>
#include <kvm/vtism_vm.h>

int init_vm(void);
int destory_vm(void);
uint64_t gpa2hva(struct qemu_struct *qemu, uint64_t gpa);
ssize_t dump_vm_info(char *buf, ssize_t len);

// bool is_vm_mm(struct mm_struct *mm);