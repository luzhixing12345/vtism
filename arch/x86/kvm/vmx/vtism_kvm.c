

#ifdef CONFIG_VTISM

#include "vtism_kvm.h"

// define in mm/vtism/kvm.c
extern struct vm_context qemu_vm;

void get_pml_log(struct qemu_struct *qemu, uint64_t gpa)
{
    // tart_log = true when start migrate_pages
	if (!qemu->start_log)
		return;
	if (gpa == 0 || qemu == NULL)
		return;
	pr_info("get_pml_log: gpa = 0x%llx", gpa);
	uint64_t gfn = gpa >> PAGE_SHIFT;
	if (qemu->pml_buffer_idx < PML_BUFFER_LEN) {
		qemu->pml_buffer[qemu->pml_buffer_idx++] = gfn;
	} else {
		pr_err("get_pml_log: pml_buffer is full");
	}
}

// vmx = to_vmx(vcpu);
// pml_buf = page_address(vmx->pml_pg);

struct qemu_struct *get_vm(struct vcpu_vmx *vmx)
{
	struct kvm *kvm = vmx->vcpu.kvm;
	for (int i = 0; i < qemu_vm.qemu_num; i++) {
		if (kvm == qemu_vm.qemu[i].kvm) {
			return &qemu_vm.qemu[i];
		}
	}
	pr_err("get_vm: qemu not found\n");
	return NULL;
}

#endif