#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/delay.h>

static DEFINE_MUTEX(lock_a);
static DEFINE_MUTEX(lock_b);

static struct task_struct *thread1;
static struct task_struct *thread2;

// Thread 1: Grabs A, then tries to grab B
static int thread_func1(void *data) {
    pr_info("Thread 1 starting. Attempting to lock A...\n");
    mutex_lock(&lock_a);
    pr_info("Thread 1 holds lock A. Sleeping...\n");
    
    msleep(2000); // Give Thread 2 time to grab lock B

    pr_info("Thread 1 now attempting to lock B ...\n");
    mutex_lock(&lock_b); 

    mutex_unlock(&lock_b);
    mutex_unlock(&lock_a);
    return 0;
}

// Thread 2: Grabs B, then tries to grab A
static int thread_func2(void *data) {
    pr_info("Thread 2 starting. Attempting to lock B...\n");
    mutex_lock(&lock_b);
    pr_info("Thread 2 holds lock B. Sleeping...\n");

    msleep(2000); // Give Thread 1 time to grab lock A

    pr_info("Thread 2 now attempting to lock A...\n");
    mutex_lock(&lock_a);

    mutex_unlock(&lock_a);
    mutex_unlock(&lock_b);
    return 0;
}

static int __init deadlock_init(void) {
    pr_info("Module loading...\n");
    thread1 = kthread_run(thread_func1, NULL, "deadlock_thread_1");
    thread2 = kthread_run(thread_func2, NULL, "deadlock_thread_2");

    return 0;
}

static void __exit deadlock_exit(void) {
    pr_info("Module exiting...\n");
}

module_init(deadlock_init);
module_exit(deadlock_exit);
MODULE_LICENSE("GPL");            

