#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/uaccess.h>

#include "common.h"
#include "page_classify.h"
#include "page_migration.h"
#include "vtismctl.h"
#include "workqueue.h"

// 初始化模块
static int __init vtism_init(void) {
    int ret;

    ret = vtismctl_init();
    if (ret < 0)
        return ret;
    INFO("vtismctl module loaded successfully\n");

    // wq = alloc_workqueue("async_promote", WQ_UNBOUND, 1);

    INFO("vtism module loaded successfully\n");
    return 0;
}

// 清理模块
static void __exit vtism_exit(void) {
    vtismctl_exit();
    INFO("vtism module removed\n");
}

module_init(vtism_init);
module_exit(vtism_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("kamilu");
MODULE_DESCRIPTION("Character device for asynchronous page migration");
