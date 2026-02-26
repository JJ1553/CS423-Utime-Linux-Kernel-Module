// SPDX-License-Identifier: GPL-2.0-only
/*
 * This module emits "Hello, world" on printk when loaded.
 *
 * It is designed to be used for basic evaluation of the module loading
 * subsystem (for example when validating module signing/verification). It
 * lacks any extra dependencies, and will not normally be loaded by the
 * system unless explicitly requested by name.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/list.h>
#include <linux/uaccess.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/string.h>
#include <linux/kstrtox.h> 

#include "mp1_given.h"

#define ALLOC_SIZE 32
#define TIMER_INTERVAL 5000


// !!!!!!!!!!!!! IMPORTANT !!!!!!!!!!!!!
// Please put your name and email here
MODULE_AUTHOR("Josh Jenks <JaJenks2@illinois.edu>");
MODULE_LICENSE("GPL");

static struct proc_dir_entry *mp1_dir;
static struct proc_dir_entry *mp1_status;
static struct timer_list mp1_timer;
struct work_struct mp1_work;

// Linked list entry structure to track each PID and its CPU usage
struct mp1_entry {
	struct list_head list;
	pid_t pid;
	unsigned long cpu_use;
};

static LIST_HEAD(mp1_list);

static DEFINE_MUTEX(mp1_lock);

/* Work function to update CPU usage for each tracked process */
static void mp1_work_fn(struct work_struct *work) {
	struct mp1_entry *entry, *tmp;
	int ret;
	//lock and iterate through the list to update CPU usage, removing any entries for processes that have died
	mutex_lock(&mp1_lock);
		list_for_each_entry_safe(entry, tmp, &mp1_list, list) {
			ret = get_cpu_use(entry->pid, &entry->cpu_use);
			if (ret < 0) {
				pr_debug( "PID %d is dead, removing from list\n", entry->pid);
				list_del(&entry->list);
				kfree(entry);
			}
		}
	mutex_unlock(&mp1_lock);
}

/* Timer callback function to schedule work for updating CPU usage and reschedule the timer */
static void mp1_timer_fn(struct timer_list *timer) {
	schedule_work(&mp1_work);
	mod_timer(&mp1_timer, jiffies + msecs_to_jiffies(TIMER_INTERVAL));
}


/* Show function for the /proc/mp1/status file */
static int mp1_status_show(struct seq_file *m, void *v) {
	struct mp1_entry *entry;

	mutex_lock(&mp1_lock);
		list_for_each_entry(entry, &mp1_list, list) {
			seq_printf(m, "%d: %lu\n", entry->pid, entry->cpu_use); //print PID and CPU usage into the seq_file buffer
		}
	mutex_unlock(&mp1_lock);

	return 0;
}

/* Open function for the /proc/mp1/status file */
static int mp1_status_open(struct inode *inode, struct file *file) {
	return single_open(file, mp1_status_show, NULL);
}

/* Write function for the /proc/mp1/status file */
static ssize_t mp1_status_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos) {
	char *buf = NULL;
	pid_t pid;
	int err;
	size_t len = 0;
	struct mp1_entry *tmp, *entry;
	unsigned long init_cpu = 0;
	int ret;
	
	if (count == 0)
		return 0;	
	else if (count > ALLOC_SIZE)
		count = ALLOC_SIZE;
	
	buf = memdup_user_nul(buffer, count); //copy the input from user space and ensure it is null terminated
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	len = strnlen(buf, count+1);		//replace the newline character with a null terminator if it exists
	if (len > 0 && buf[len-1] == '\n')
    	buf[len-1] = '\0';

	err = kstrtoint(buf, 10, &pid);	//convert the input string to an integer PID
	if(err < 0 || pid <= 0) {
		kfree(buf);
		return -EINVAL;
	}

	pr_debug( "Received PID: %d\n", pid);
	
	ret = get_cpu_use(pid, &init_cpu);
	if (ret < 0) {
		kfree(buf);
		return -EINVAL;
	}
	mutex_lock(&mp1_lock);
		list_for_each_entry(tmp, &mp1_list, list) {
			if(tmp->pid == pid) {
				pr_debug( "PID %d already exists in the list\n", pid);
				mutex_unlock(&mp1_lock);
				kfree(buf);
				return count;
			}
		}
		//if new PID, create a new entry and add it to the list
		entry = kmalloc(sizeof(*entry), GFP_KERNEL);

		if (!entry) {
			mutex_unlock(&mp1_lock);
			kfree(buf);
			return -ENOMEM;
		}

		entry->pid = pid;
		entry->cpu_use = init_cpu; 
		list_add_tail(&entry->list, &mp1_list); //add the new entry to the end of the list
	mutex_unlock(&mp1_lock);
	
	kfree(buf);
	return count;
}

static const struct proc_ops mp1_proc_ops = {
	.proc_open	= mp1_status_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
	.proc_write	= mp1_status_write,
};

static int __init test_module_init(void)
{
	mp1_dir = proc_mkdir("mp1", NULL); //create the /proc/mp1 directory
	if (!mp1_dir)
		return -ENOMEM;
	
	//create the /proc/mp1/status file with read/write permissions for all users and associate it with the defined proc_ops
	mp1_status = proc_create("status", 0666, mp1_dir, &mp1_proc_ops); 
	if (!mp1_status) {
		proc_remove(mp1_dir);
		return -ENOMEM;
	}
	//initialize the work struct and timer, then schedule the first timer event for 5 seconds in the future
	INIT_WORK(&mp1_work, mp1_work_fn);
	timer_setup(&mp1_timer, mp1_timer_fn, 0);
	mod_timer(&mp1_timer, jiffies + msecs_to_jiffies(TIMER_INTERVAL));

	return 0;
}

module_init(test_module_init);

/* Exit function for the module, delete thetimer, workqueue, /proc/mp1/status file and /proc/mp1 directory, and free all allocated LL memory */
static void __exit test_module_exit(void)
{
	struct mp1_entry *entry, *tmp;
	
	del_timer_sync(&mp1_timer);
	cancel_work_sync(&mp1_work);

	mutex_lock(&mp1_lock);
		list_for_each_entry_safe(entry, tmp, &mp1_list, list) {
			list_del(&entry->list);
			kfree(entry);
		}
	mutex_unlock(&mp1_lock);

	if(mp1_status) proc_remove(mp1_status);
	if(mp1_dir) proc_remove(mp1_dir);
}

module_exit(test_module_exit);