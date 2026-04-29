#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mazen");
MODULE_DESCRIPTION("Mini-Lockdep: Deadlock Detection Module");

#define MAX_LOCKS 10
#define MAX_THREADS 10
#define MAX_HELD_LOCKS 10

/* tracks which locks a thread is holding */
struct thread_locks {
    pid_t pid;
    char name[TASK_COMM_LEN];
    int held[MAX_HELD_LOCKS];
    int held_count;
    int active;
};

static struct thread_locks threads[MAX_THREADS];
static int graph[MAX_LOCKS][MAX_LOCKS];  /* dependency graph as adjacency matrix */
static spinlock_t state_lock;

/* find thread entry by pid, or create one if it doesn't exist */
static struct thread_locks* get_thread_data(pid_t pid)
{
    int i;

    for (i = 0; i < MAX_THREADS; i++) {
        if (threads[i].active && threads[i].pid == pid)
            return &threads[i];
    }

    for (i = 0; i < MAX_THREADS; i++) {
        if (!threads[i].active) {
            threads[i].pid = pid;
            threads[i].held_count = 0;
            threads[i].active = 1;
            strncpy(threads[i].name, current->comm, TASK_COMM_LEN - 1);
            threads[i].name[TASK_COMM_LEN - 1] = '\0';
            printk(KERN_INFO "[Mini-Lockdep] Tracking Thread %d (%s)\n", pid, threads[i].name);
            return &threads[i];
        }
    }

    printk(KERN_WARNING "[Mini-Lockdep] Thread table full, cannot track PID %d\n", pid);
    return NULL;
}

/* add a directed edge from->to in the dependency graph */
static void add_dependency(int from, int to)
{
    if (from >= MAX_LOCKS || to >= MAX_LOCKS || from < 0 || to < 0) {
        printk(KERN_WARNING "[Mini-Lockdep] Invalid lock IDs: %d -> %d\n", from, to);
        return;
    }

    if (!graph[from][to]) {
        graph[from][to] = 1;
        printk(KERN_INFO "[Mini-Lockdep] Dependency added: Lock%d -> Lock%d\n", from, to);
    }
}

/* DFS to check for cycles */
static int dfs(int node, int visited[], int rec_stack[])
{
    int i;

    visited[node] = 1;
    rec_stack[node] = 1;

    for (i = 0; i < MAX_LOCKS; i++) {
        if (graph[node][i]) {
            if (!visited[i]) {
                if (dfs(i, visited, rec_stack))
                    return 1;
            } else if (rec_stack[i]) {
                return 1;
            }
        }
    }

    rec_stack[node] = 0;
    return 0;
}

/* check the whole graph for cycles */
static int detect_cycle(void)
{
    int visited[MAX_LOCKS] = {0};
    int rec_stack[MAX_LOCKS] = {0};
    int i;

    for (i = 0; i < MAX_LOCKS; i++) {
        if (!visited[i]) {
            if (dfs(i, visited, rec_stack))
                return 1;
        }
    }

    return 0;
}

static void print_graph_state(void)
{
    int i, j;
    printk(KERN_INFO "[Mini-Lockdep] Current dependency graph:\n");
    for (i = 0; i < MAX_LOCKS; i++) {
        for (j = 0; j < MAX_LOCKS; j++) {
            if (graph[i][j])
                printk(KERN_INFO "  Lock%d -> Lock%d\n", i, j);
        }
    }
}

static void mini_lock_acquire(int lock_id)
{
    struct thread_locks *tdata;
    pid_t pid = current->pid;
    int i;

    if (lock_id >= MAX_LOCKS || lock_id < 0) {
        printk(KERN_WARNING "[Mini-Lockdep] Invalid lock ID: %d\n", lock_id);
        return;
    }

    spin_lock(&state_lock);

    tdata = get_thread_data(pid);
    if (!tdata) {
        spin_unlock(&state_lock);
        return;
    }

    printk(KERN_INFO "[Mini-Lockdep] Thread %d (%s) acquiring Lock%d\n", pid, tdata->name, lock_id);

    /* add edges from all held locks to the new lock */
    for (i = 0; i < tdata->held_count; i++)
        add_dependency(tdata->held[i], lock_id);

    if (detect_cycle()) {
        printk(KERN_WARNING "[Mini-Lockdep] *** POTENTIAL DEADLOCK DETECTED ***\n");
        print_graph_state();
    }

    if (tdata->held_count < MAX_HELD_LOCKS) {
        tdata->held[tdata->held_count] = lock_id;
        tdata->held_count++;
        printk(KERN_INFO "[Mini-Lockdep] Lock%d added to Thread %d held list (count: %d)\n",
               lock_id, pid, tdata->held_count);
    } else {
        printk(KERN_WARNING "[Mini-Lockdep] Thread %d hold list full\n", pid);
    }

    spin_unlock(&state_lock);
}

static void mini_lock_release(int lock_id)
{
    struct thread_locks *tdata;
    pid_t pid = current->pid;
    int i;

    if (lock_id >= MAX_LOCKS || lock_id < 0) {
        printk(KERN_WARNING "[Mini-Lockdep] Invalid lock ID: %d\n", lock_id);
        return;
    }

    spin_lock(&state_lock);

    tdata = get_thread_data(pid);
    if (!tdata) {
        spin_unlock(&state_lock);
        return;
    }

    for (i = 0; i < tdata->held_count; i++) {
        if (tdata->held[i] == lock_id) {
            for (int j = i; j < tdata->held_count - 1; j++)
                tdata->held[j] = tdata->held[j + 1];
            tdata->held_count--;
            printk(KERN_INFO "[Mini-Lockdep] Thread %d released Lock%d (remaining: %d)\n",
                   pid, lock_id, tdata->held_count);
            spin_unlock(&state_lock);
            return;
        }
    }

    printk(KERN_WARNING "[Mini-Lockdep] Thread %d tried to release Lock%d but it's not held\n",
           pid, lock_id);
    spin_unlock(&state_lock);
}

static struct task_struct *thread1, *thread2;

/* both threads acquire locks in the same order, no deadlock */
static int test_safe_scenario(void *arg)
{
    int thread_id = (int)(long)arg;
    printk(KERN_INFO "[Mini-Lockdep TEST] Safe - Thread %d starting\n", thread_id);

    mini_lock_acquire(1);
    msleep(50);
    mini_lock_acquire(2);

    msleep(100);

    mini_lock_release(2);
    mini_lock_release(1);

    printk(KERN_INFO "[Mini-Lockdep TEST] Safe - Thread %d done\n", thread_id);
    return 0;
}

/* Thread A: acquires lock 1 then lock 2 */
static int test_deadlock_scenario_a(void *arg)
{
    printk(KERN_INFO "[Mini-Lockdep TEST] Deadlock - Thread A starting\n");

    mini_lock_acquire(1);
    msleep(200);
    mini_lock_acquire(2);

    msleep(100);

    mini_lock_release(2);
    mini_lock_release(1);

    printk(KERN_INFO "[Mini-Lockdep TEST] Deadlock - Thread A done\n");
    return 0;
}

/* Thread B: acquires lock 2 then lock 1 (opposite order = deadlock) */
static int test_deadlock_scenario_b(void *arg)
{
    msleep(100);

    printk(KERN_INFO "[Mini-Lockdep TEST] Deadlock - Thread B starting\n");

    mini_lock_acquire(2);
    msleep(200);
    mini_lock_acquire(1);

    msleep(100);

    mini_lock_release(1);
    mini_lock_release(2);

    printk(KERN_INFO "[Mini-Lockdep TEST] Deadlock - Thread B done\n");
    return 0;
}

static int __init mini_lockdep_init(void)
{
    int i, j;

    printk(KERN_INFO "[Mini-Lockdep] Loading module...\n");

    spin_lock_init(&state_lock);

    for (i = 0; i < MAX_THREADS; i++) {
        threads[i].active = 0;
        threads[i].held_count = 0;
    }

    for (i = 0; i < MAX_LOCKS; i++)
        for (j = 0; j < MAX_LOCKS; j++)
            graph[i][j] = 0;

    /* Test 1: safe lock ordering */
    printk(KERN_INFO "=== TEST 1: SAFE SCENARIO ===\n");
    printk(KERN_INFO "Both threads acquire locks in same order (1 -> 2), no deadlock expected\n");

    thread1 = kthread_run(test_safe_scenario, (void *)1, "lockdep_t1");
    if (IS_ERR(thread1)) {
        printk(KERN_ERR "[Mini-Lockdep] Failed to create thread 1\n");
        return PTR_ERR(thread1);
    }

    thread2 = kthread_run(test_safe_scenario, (void *)2, "lockdep_t2");
    if (IS_ERR(thread2)) {
        printk(KERN_ERR "[Mini-Lockdep] Failed to create thread 2\n");
        kthread_stop(thread1);
        return PTR_ERR(thread2);
    }

    kthread_stop(thread1);
    kthread_stop(thread2);
    msleep(500);

    printk(KERN_INFO "=== TEST 1 DONE ===\n");

    /* reset state */
    for (i = 0; i < MAX_THREADS; i++) {
        threads[i].active = 0;
        threads[i].held_count = 0;
    }
    for (i = 0; i < MAX_LOCKS; i++)
        for (j = 0; j < MAX_LOCKS; j++)
            graph[i][j] = 0;

    /* Test 2: circular lock ordering -> deadlock */
    printk(KERN_INFO "=== TEST 2: DEADLOCK SCENARIO ===\n");
    printk(KERN_INFO "Thread A: 1->2, Thread B: 2->1, deadlock warning expected\n");

    thread1 = kthread_run(test_deadlock_scenario_a, NULL, "lockdep_a");
    if (IS_ERR(thread1)) {
        printk(KERN_ERR "[Mini-Lockdep] Failed to create thread A\n");
        return PTR_ERR(thread1);
    }

    thread2 = kthread_run(test_deadlock_scenario_b, NULL, "lockdep_b");
    if (IS_ERR(thread2)) {
        printk(KERN_ERR "[Mini-Lockdep] Failed to create thread B\n");
        kthread_stop(thread1);
        return PTR_ERR(thread2);
    }

    kthread_stop(thread1);
    kthread_stop(thread2);
    msleep(500);

    printk(KERN_INFO "=== TEST 2 DONE ===\n");
    printk(KERN_INFO "[Mini-Lockdep] Module loaded, check dmesg for output\n");

    return 0;
}

static void __exit mini_lockdep_exit(void)
{
    printk(KERN_INFO "[Mini-Lockdep] Module unloaded\n");
}

module_init(mini_lockdep_init);
module_exit(mini_lockdep_exit);