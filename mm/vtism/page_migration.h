
#pragma once

#include <linux/migrate.h>
#include <linux/module.h>

int page_migration_init(void);
void page_migration_exit(void);

ssize_t dump_page_migration_info(char *buf, ssize_t len);
int async_migrate_folio(struct address_space *mapping, struct folio *dst, struct folio *src, enum migrate_mode mode);