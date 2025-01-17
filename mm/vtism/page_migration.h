
#pragma once

#include <linux/module.h>

int page_migration_init(void);
void page_migration_exit(void);

ssize_t dump_page_migration_info(char *buf, ssize_t len);