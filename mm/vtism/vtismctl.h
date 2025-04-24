
#pragma once

#include <linux/types.h>

int vtismctl_init(void);
int vtismctl_exit(void);
void update_numa_mem(void);
extern bool vtism_enable;