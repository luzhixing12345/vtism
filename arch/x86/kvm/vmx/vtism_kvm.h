
#ifdef CONFIG_VTISM

#include <linux/stddef.h>
#include <linux/types.h>
#include <kvm/vtism_vm.h>
#include "vmx.h"

#define PT_PAGE_TABLE_LEVEL 1 // 4KB
#define PT_DIRECTORY_LEVEL 2 // 2MB
#define PT_PDPT_LEVEL 3 // 1GB

// void get_pml_log(struct qemu_struct *qemu, uint64_t gpa);
struct qemu_struct *get_vm(struct vcpu_vmx *vmx);
extern bool vtism_enable;
#endif