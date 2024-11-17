#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/err.h>

#include "common.h"
#include "workqueue.h"
#include "vtism.h"

static dev_t dev_number;
static struct cdev vtism_cdev;
static struct class *vtism_class;

// 文件操作结构体
static struct file_operations vtism_fops = {
	.owner = THIS_MODULE,
	.open = vtism_open,
	.release = vtism_release,
	.unlocked_ioctl = vtism_ioctl,
};

static int init_interface(void)
{
	int ret;

	// 分配设备号
	ret = alloc_chrdev_region(&dev_number, 0, 1, DEVICE_NAME);
	if (ret < 0) {
		ERR("Failed to allocate device number\n");
		return ret;
	}

	// 初始化字符设备
	cdev_init(&vtism_cdev, &vtism_fops);
	vtism_cdev.owner = THIS_MODULE;
	ret = cdev_add(&vtism_cdev, dev_number, 1);
	if (ret < 0) {
		unregister_chrdev_region(dev_number, 1);
		ERR("Failed to add character device\n");
		return ret;
	}

	// 创建类
	vtism_class = class_create(DEVICE_NAME);
	if (IS_ERR(vtism_class)) {
		ret = PTR_ERR(vtism_class);
		cdev_del(&vtism_cdev);
		unregister_chrdev_region(dev_number, 1);
		ERR("Failed to create device class\n");
		return ret;
	}

	// 创建设备节点
	if (!device_create(vtism_class, NULL, dev_number, NULL, DEVICE_NAME)) {
		class_destroy(vtism_class);
		cdev_del(&vtism_cdev);
		unregister_chrdev_region(dev_number, 1);
		ERR("Failed to create device\n");
		return -ENOMEM;
	}

	INFO("Device created successfully /dev/%s\n", DEVICE_NAME);
	return 0;
}

static void destroy_interface(void)
{
	if (vtism_class) {
		device_destroy(vtism_class, dev_number);
		class_destroy(vtism_class);
	}
	cdev_del(&vtism_cdev);
	unregister_chrdev_region(dev_number, 1);
}

// 初始化模块
static int __init vtism_init(void)
{
	int ret;

    // create /dev/vtism_migrate interface for async page migration
	ret = init_interface();
	if (ret < 0)
		return ret;

    wq = alloc_workqueue("async_promote", WQ_UNBOUND, 1);

	INFO("module loaded successfully\n");
	return 0;
}

// 清理模块
static void __exit vtism_exit(void)
{
	destroy_interface();
	INFO("module removed\n");
}

module_init(vtism_init);
module_exit(vtism_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("kamilu");
MODULE_DESCRIPTION("Character device for asynchronous page migration");
