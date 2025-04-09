
#pragma once

#include <linux/migrate.h>
#include <linux/module.h>

struct migration_work {
    struct work_struct work;
    struct folio *src;
    struct folio *dst;
    enum migrate_reason reason;
    atomic_t done;
};

#define MAX_NUMA_NODES 8

int page_migration_init(void);
void page_migration_exit(void);

ssize_t dump_page_migration_info(char *buf, ssize_t len);
int async_migrate_folio(struct address_space *mapping, struct folio *dst, struct folio *src,
                        enum migrate_reason reason);

extern bool vtism_migration_enable;