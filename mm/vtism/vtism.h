
#pragma once

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/err.h>
#include "common.h"

int vtism_open(struct inode *inode, struct file *file);
int vtism_release(struct inode *inode, struct file *file);
long vtism_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

