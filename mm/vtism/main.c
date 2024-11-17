#include <linux/init.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/pid.h>
#include <linux/delay.h> // for ssleep
#include <asm/pgtable.h>

static int pid = 0;
module_param_named(pid, pid, int, 0644);

static struct task_struct *scan_thread;

// 内核线程函数
static int scan_page_tables(void *data)
{
	struct task_struct *task;
	struct mm_struct *mm;
	pgd_t *pgd;

	printk(KERN_INFO "Kernel thread started\n");

	// 主循环，直到线程被停止
	while (!kthread_should_stop()) {
		// 获取目标进程的 task_struct
		rcu_read_lock();
		task = pid_task(find_vpid(pid), PIDTYPE_PID);
		if (!task) {
			printk(KERN_ERR "Failed to find task with PID %d\n",
			       pid);
			rcu_read_unlock();
			break;
		}
		mm = task->mm;
		if (!mm) {
			printk(KERN_ERR "Task has no mm_struct\n");
			rcu_read_unlock();
			break;
		}
		rcu_read_unlock();

		// 遍历页表
		down_read(&mm->mmap_lock); // 锁定内存映射
		pgd = mm->pgd;
		pr_info("pgd: %p\n", pgd);
		// 这里需要实现对 PUD, PMD, PTE 的递归遍历
		// 并检查 A/D 位
		up_read(&mm->mmap_lock);

		// 每次扫描后休眠 5 秒
		ssleep(5);
	}

	printk(KERN_INFO "Kernel thread stopping\n");
	return 0;
}

static int __init my_module_init(void)
{
	printk(KERN_INFO "Initializing page table scanner module\n");
	scan_thread = kthread_run(scan_page_tables, NULL, "page_table_scanner");
	if (IS_ERR(scan_thread)) {
		printk(KERN_ERR "Failed to create kernel thread\n");
		return PTR_ERR(scan_thread);
	}
	return 0;
}

static void __exit my_module_exit(void)
{
	if (scan_thread) {
		kthread_stop(scan_thread);
		printk(KERN_INFO "Kernel thread stopped\n");
	}
	printk(KERN_INFO "Page table scanner module exited\n");
}

module_init(my_module_init);
module_exit(my_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("A module to scan page tables for Accessed/Dirty bits");
