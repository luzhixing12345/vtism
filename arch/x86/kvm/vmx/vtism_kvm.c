

#ifdef CONFIG_VTISM

#include "vtism_kvm.h"

#define KB 1024
#define MB (1024 * KB)
#define GB (1024 * MB)

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

static inline gfn_t gfn_to_index(gfn_t gfn, gfn_t base_gfn, int level)
{
	/* KVM_HPAGE_GFN_SHIFT(PT_PAGE_TABLE_LEVEL) must be 0. */
	return (gfn >> KVM_HPAGE_GFN_SHIFT(level)) -
		(base_gfn >> KVM_HPAGE_GFN_SHIFT(level));
}


inline int flush_ept_dirty_bit_by_gfn(struct qemu_struct *qemu, uint64_t gfn)
{
	uint64_t idx;
	uint64_t *sptep;
	int level;
	struct kvm *kvm;
	struct kvm_rmap_head *rmap_head;
	struct kvm_memory_slot *slot;

	kvm = qemu->kvm;
	
	slot = gfn_to_memslot(kvm, gfn);

	if (slot == NULL) {
		printk("slot is null\n");
		return -1;
	}
    level = PT_PAGE_TABLE_LEVEL;
	idx = gfn_to_index(gfn, slot->base_gfn, level);
	rmap_head = &slot->arch.rmap[level - 1][idx];
	sptep = (uint64_t *)(rmap_head->val);

	if (sptep) {
		if (((*sptep) & (1ull << 9)) != 0) {
			(*sptep) = (*sptep) & (~(1ull << 9));
		}
		return 1;
	}

	return 0;
}

#endif