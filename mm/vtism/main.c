#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/err.h>

#include "common.h"
#include "workqueue.h"
#include "page_classify.h"
#include "page_demotion.h"

// 初始化模块
static int __init vtism_init(void)
{
	// int ret;

    // create /dev/vtism_migrate interface for async page migration
	// ret = init_interface();
	// if (ret < 0)
	// 	return ret;

    // ret = page_classify_init();
    // if (ret < 0)
    //     return ret;

    // ret = page_demotion_init();
    // if (ret < 0)
    //     return ret;

    // wq = alloc_workqueue("async_promote", WQ_UNBOUND, 1);

	INFO("vtism module loaded successfully\n");
	return 0;
}

// 清理模块
static void __exit vtism_exit(void)
{
	// destroy_interface();
    // page_classify_exit();
    // page_demotion_exit();
	INFO("vtism module removed\n");
}

module_init(vtism_init);
module_exit(vtism_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("kamilu");
MODULE_DESCRIPTION("Character device for asynchronous page migration");
