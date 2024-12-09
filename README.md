
# vtism

vtism.config

kvm module built-in kernel

enable damon

remove kernel debug info
 
```bash
CONFIG_KVM_EPT_SAMPLE=y
```

autofs

EXPORT_SYMBOL(__mmu_notifier_clear_young);
EXPORT_SYMBOL(walk_page_vma_opt);
EXPORT_SYMBOL(ptep_test_and_clear_young);
EXPORT_SYMBOL(pmdp_test_and_clear_young);
EXPORT_SYMBOL(pudp_test_and_clear_young);
EXPORT_SYMBOL(p4dp_test_and_clear_young);
EXPORT_SYMBOL(pgdp_test_and_clear_young);