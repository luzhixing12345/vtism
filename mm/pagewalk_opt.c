// SPDX-License-Identifier: GPL-2.0
#include "asm/pgtable.h"
#include <linux/export.h>
#include <linux/pagewalk.h>
#include <linux/highmem.h>
#include <linux/sched.h>
#include <linux/hugetlb.h>

/*
 * We want to know the real level where a entry is located ignoring any
 * folding of levels which may be happening. For example if p4d is folded then
 * a missing entry found at level 1 (p4d) is actually at level 0 (pgd).
 */
static int real_depth(int depth)
{
	if (depth == 3 && PTRS_PER_PMD == 1)
		depth = 2;
	if (depth == 2 && PTRS_PER_PUD == 1)
		depth = 1;
	if (depth == 1 && PTRS_PER_P4D == 1)
		depth = 0;
	return depth;
}

static int walk_pte_range_inner(pte_t *pte, unsigned long addr,
				unsigned long end, struct mm_walk *walk)
{
	const struct mm_walk_ops *ops = walk->ops;
	int err = 0;

	for (;;) {
		err = ops->pte_entry(pte, addr, addr + PAGE_SIZE, walk);
		if (err)
			break;
		if (addr >= end - PAGE_SIZE)
			break;
		addr += PAGE_SIZE;
		pte++;
	}
	return err;
}

static int walk_pte_range(pmd_t *pmd, unsigned long addr, unsigned long end,
			  struct mm_walk *walk)
{
	pte_t *pte;
	int err = 0;
	spinlock_t *ptl;

	if (walk->no_vma) {
		/*
		 * pte_offset_map() might apply user-specific validation.
		 * Indeed, on x86_64 the pmd entries set up by init_espfix_ap()
		 * fit its pmd_bad() check (_PAGE_NX set and _PAGE_RW clear),
		 * and CONFIG_EFI_PGT_DUMP efi_mm goes so far as to walk them.
		 */
		if (walk->mm == &init_mm || addr >= TASK_SIZE)
			pte = pte_offset_kernel(pmd, addr);
		else
			pte = pte_offset_map(pmd, addr);
		if (pte) {
			err = walk_pte_range_inner(pte, addr, end, walk);
			if (walk->mm != &init_mm && addr < TASK_SIZE)
				pte_unmap(pte);
		}
	} else {
		pte = pte_offset_map_lock(walk->mm, pmd, addr, &ptl);
		if (pte) {
			err = walk_pte_range_inner(pte, addr, end, walk);
			pte_unmap_unlock(pte, ptl);
		}
	}
	if (!pte)
		walk->action = ACTION_AGAIN;
	return err;
}

#ifdef CONFIG_ARCH_HAS_HUGEPD
static int walk_hugepd_range(hugepd_t *phpd, unsigned long addr,
			     unsigned long end, struct mm_walk *walk,
			     int pdshift)
{
	int err = 0;
	const struct mm_walk_ops *ops = walk->ops;
	int shift = hugepd_shift(*phpd);
	int page_size = 1 << shift;

	if (!ops->pte_entry)
		return 0;

	if (addr & (page_size - 1))
		return 0;

	for (;;) {
		pte_t *pte;

		spin_lock(&walk->mm->page_table_lock);
		pte = hugepte_offset(*phpd, addr, pdshift);
		err = ops->pte_entry(pte, addr, addr + page_size, walk);
		spin_unlock(&walk->mm->page_table_lock);

		if (err)
			break;
		if (addr >= end - page_size)
			break;
		addr += page_size;
	}
	return err;
}
#else
static int walk_hugepd_range(hugepd_t *phpd, unsigned long addr,
			     unsigned long end, struct mm_walk *walk,
			     int pdshift)
{
	return 0;
}
#endif

static int walk_pmd_range(pud_t *pud, unsigned long addr, unsigned long end,
			  struct mm_walk *walk)
{
	pmd_t *pmd;
	unsigned long next;
	const struct mm_walk_ops *ops = walk->ops;
	int err = 0;
	int depth = real_depth(3);

	pmd = pmd_offset(pud, addr);
	do {
again:
		next = pmd_addr_end(addr, end);
		if (pmd_none(*pmd)) {
			if (ops->pte_hole)
				err = ops->pte_hole(addr, next, depth, walk);
			if (err)
				break;
			continue;
		}
        if (!pmd_young(*pmd) || !pmd_present(*pmd))
            continue;

		walk->action = ACTION_SUBTREE;

		/*
		 * This implies that each ->pmd_entry() handler
		 * needs to know about pmd_trans_huge() pmds
		 */
		if (ops->pmd_entry)
			err = ops->pmd_entry(pmd, addr, next, walk);
		if (err)
			break;

		if (walk->action == ACTION_AGAIN)
			goto again;

		/*
		 * Check this here so we only break down trans_huge
		 * pages when we _need_ to
		 */
		if ((!walk->vma && (pmd_leaf(*pmd) || !pmd_present(*pmd))) ||
		    walk->action == ACTION_CONTINUE || !(ops->pte_entry))
			continue;

		if (walk->vma)
			split_huge_pmd(walk->vma, pmd, addr);

		if (is_hugepd(__hugepd(pmd_val(*pmd))))
			err = walk_hugepd_range((hugepd_t *)pmd, addr, next,
						walk, PMD_SHIFT);
		else
			err = walk_pte_range(pmd, addr, next, walk);
		if (err)
			break;

		if (walk->action == ACTION_AGAIN)
			goto again;

	} while (pmd++, addr = next, addr != end);

	return err;
}

static int walk_pud_range(p4d_t *p4d, unsigned long addr, unsigned long end,
			  struct mm_walk *walk)
{
	pud_t *pud;
	unsigned long next;
	const struct mm_walk_ops *ops = walk->ops;
	int err = 0;
	int depth = real_depth(2);

	pud = pud_offset(p4d, addr);
	do {
again:
		next = pud_addr_end(addr, end);
		if (pud_none(*pud)) {
			if (ops->pte_hole)
				err = ops->pte_hole(addr, next, depth, walk);
			if (err)
				break;
			continue;
		}
        if (!pud_young(*pud) || !pud_present(*pud))
            continue;

		walk->action = ACTION_SUBTREE;

		if (ops->pud_entry)
			err = ops->pud_entry(pud, addr, next, walk);
		if (err)
			break;

		if (walk->action == ACTION_AGAIN)
			goto again;

		if ((!walk->vma && (pud_leaf(*pud) || !pud_present(*pud))) ||
		    walk->action == ACTION_CONTINUE ||
		    !(ops->pmd_entry || ops->pte_entry))
			continue;

		if (walk->vma)
			split_huge_pud(walk->vma, pud, addr);
		if (pud_none(*pud))
			goto again;

		if (is_hugepd(__hugepd(pud_val(*pud))))
			err = walk_hugepd_range((hugepd_t *)pud, addr, next,
						walk, PUD_SHIFT);
		else
			err = walk_pmd_range(pud, addr, next, walk);
		if (err)
			break;
	} while (pud++, addr = next, addr != end);

	return err;
}

static int walk_p4d_range(pgd_t *pgd, unsigned long addr, unsigned long end,
			  struct mm_walk *walk)
{
	p4d_t *p4d;
	unsigned long next;
	const struct mm_walk_ops *ops = walk->ops;
	int err = 0;
	int depth = real_depth(1);

	p4d = p4d_offset(pgd, addr);
	do {
		next = p4d_addr_end(addr, end);
		if (p4d_none_or_clear_bad(p4d)) {
			if (ops->pte_hole)
				err = ops->pte_hole(addr, next, depth, walk);
			if (err)
				break;
			continue;
		}
        if (!p4d_young(*p4d) || !p4d_present(*p4d))
            continue;
		if (ops->p4d_entry) {
			err = ops->p4d_entry(p4d, addr, next, walk);
			if (err)
				break;
		}
		if (is_hugepd(__hugepd(p4d_val(*p4d))))
			err = walk_hugepd_range((hugepd_t *)p4d, addr, next,
						walk, P4D_SHIFT);
		else if (ops->pud_entry || ops->pmd_entry || ops->pte_entry)
			err = walk_pud_range(p4d, addr, next, walk);
		if (err)
			break;
	} while (p4d++, addr = next, addr != end);

	return err;
}

static int walk_pgd_range(unsigned long addr, unsigned long end,
			  struct mm_walk *walk)
{
	pgd_t *pgd;
	unsigned long next;
	const struct mm_walk_ops *ops = walk->ops;
	int err = 0;

	if (walk->pgd)
		pgd = walk->pgd + pgd_index(addr);
	else
		pgd = pgd_offset(walk->mm, addr);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_none_or_clear_bad(pgd)) {
			if (ops->pte_hole)
				err = ops->pte_hole(addr, next, 0, walk);
			if (err)
				break;
			continue;
		}
		if (!pgd_present(*pgd) || !pgd_young(*pgd))
			continue;
		if (ops->pgd_entry) {
			err = ops->pgd_entry(pgd, addr, next, walk);
			if (err)
				break;
		}
		if (is_hugepd(__hugepd(pgd_val(*pgd))))
			err = walk_hugepd_range((hugepd_t *)pgd, addr, next,
						walk, PGDIR_SHIFT);
		else if (ops->p4d_entry || ops->pud_entry || ops->pmd_entry ||
			 ops->pte_entry)
			err = walk_p4d_range(pgd, addr, next, walk);
		if (err)
			break;
	} while (pgd++, addr = next, addr != end);

	return err;
}

#ifdef CONFIG_HUGETLB_PAGE
static unsigned long hugetlb_entry_end(struct hstate *h, unsigned long addr,
				       unsigned long end)
{
	unsigned long boundary = (addr & huge_page_mask(h)) + huge_page_size(h);
	return boundary < end ? boundary : end;
}

static int walk_hugetlb_range(unsigned long addr, unsigned long end,
			      struct mm_walk *walk)
{
	struct vm_area_struct *vma = walk->vma;
	struct hstate *h = hstate_vma(vma);
	unsigned long next;
	unsigned long hmask = huge_page_mask(h);
	unsigned long sz = huge_page_size(h);
	pte_t *pte;
	const struct mm_walk_ops *ops = walk->ops;
	int err = 0;

	hugetlb_vma_lock_read(vma);
	do {
		next = hugetlb_entry_end(h, addr, end);
		pte = hugetlb_walk(vma, addr & hmask, sz);
		if (pte)
			err = ops->hugetlb_entry(pte, hmask, addr, next, walk);
		else if (ops->pte_hole)
			err = ops->pte_hole(addr, next, -1, walk);
		if (err)
			break;
	} while (addr = next, addr != end);
	hugetlb_vma_unlock_read(vma);

	return err;
}

#else /* CONFIG_HUGETLB_PAGE */
static int walk_hugetlb_range(unsigned long addr, unsigned long end,
			      struct mm_walk *walk)
{
	return 0;
}

#endif /* CONFIG_HUGETLB_PAGE */

/*
 * Decide whether we really walk over the current vma on [@start, @end)
 * or skip it via the returned value. Return 0 if we do walk over the
 * current vma, and return 1 if we skip the vma. Negative values means
 * error, where we abort the current walk.
 */
static int walk_page_test(unsigned long start, unsigned long end,
			  struct mm_walk *walk)
{
	struct vm_area_struct *vma = walk->vma;
	const struct mm_walk_ops *ops = walk->ops;

	if (ops->test_walk)
		return ops->test_walk(start, end, walk);

	/*
	 * vma(VM_PFNMAP) doesn't have any valid struct pages behind VM_PFNMAP
	 * range, so we don't walk over it as we do for normal vmas. However,
	 * Some callers are interested in handling hole range and they don't
	 * want to just ignore any single address range. Such users certainly
	 * define their ->pte_hole() callbacks, so let's delegate them to handle
	 * vma(VM_PFNMAP).
	 */
	if (vma->vm_flags & VM_PFNMAP) {
		int err = 1;
		if (ops->pte_hole)
			err = ops->pte_hole(start, end, -1, walk);
		return err ? err : 1;
	}
	return 0;
}

static int __walk_page_range(unsigned long start, unsigned long end,
			     struct mm_walk *walk)
{
	int err = 0;
	struct vm_area_struct *vma = walk->vma;
	const struct mm_walk_ops *ops = walk->ops;

	if (ops->pre_vma) {
		err = ops->pre_vma(start, end, walk);
		if (err)
			return err;
	}

	if (is_vm_hugetlb_page(vma)) {
		if (ops->hugetlb_entry)
			err = walk_hugetlb_range(start, end, walk);
	} else
		err = walk_pgd_range(start, end, walk);

	if (ops->post_vma)
		ops->post_vma(walk);

	return err;
}

static inline void process_mm_walk_lock(struct mm_struct *mm,
					enum page_walk_lock walk_lock)
{
	if (walk_lock == PGWALK_RDLOCK)
		mmap_assert_locked(mm);
	else
		mmap_assert_write_locked(mm);
}

static inline void process_vma_walk_lock(struct vm_area_struct *vma,
					 enum page_walk_lock walk_lock)
{
#ifdef CONFIG_PER_VMA_LOCK
	switch (walk_lock) {
	case PGWALK_WRLOCK:
		vma_start_write(vma);
		break;
	case PGWALK_WRLOCK_VERIFY:
		vma_assert_write_locked(vma);
		break;
	case PGWALK_RDLOCK:
		/* PGWALK_RDLOCK is handled by process_mm_walk_lock */
		break;
	}
#endif
}

int walk_page_vma_opt(struct vm_area_struct *vma, const struct mm_walk_ops *ops,
		      void *private)
{
	struct mm_walk walk = {
		.ops = ops,
		.mm = vma->vm_mm,
		.vma = vma,
		.private = private,
	};

	if (!walk.mm)
		return -EINVAL;

	process_mm_walk_lock(walk.mm, ops->walk_lock);
	process_vma_walk_lock(vma, ops->walk_lock);
	return __walk_page_range(vma->vm_start, vma->vm_end, &walk);
}

EXPORT_SYMBOL(walk_page_vma_opt);
