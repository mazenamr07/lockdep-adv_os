#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Student");
MODULE_DESCRIPTION("Mini-Lockdep: Simplified Linux Lockdep for Deadlock Detection");

/* Configuration limits */
#define MAX_LOCKS 10
#define MAX_THREADS 10
#define MAX_HELD_LOCKS 10

/* Data structure to track locks held by a thread */
struct thread_locks {
    pid_t pid;
    char name[TASK_COMM_LEN];
    int held[MAX_HELD_LOCKS];      /* IDs of currently held locks */
    int held_count;                /* Number of locks currently held */
    int active;                    /* Is this entry active? */
};

/* Global state */
static struct thread_locks threads[MAX_THREADS];
static int graph[MAX_LOCKS][MAX_LOCKS];  /* Adjacency matrix for dependency graph */
static spinlock_t state_lock;             /* Protects global state */

/* ============================================================
 * UTILITY FUNCTIONS
 * ============================================================ */

/**
 * get_thread_data - Find or create thread tracking entry
 * @pid: Process ID of the thread
 * Returns: Pointer to thread_locks entry, or NULL if full
 */
static struct thread_locks* get_thread_data(pid_t pid)
{
    int i;

    /* Search for existing entry */
    for (i = 0; i < MAX_THREADS; i++) {
        if (threads[i].active && threads[i].pid == pid) {
            return &threads[i];
        }
    }

    /* Create new entry if space available */
    for (i = 0; i < MAX_THREADS; i++) {
        if (!threads[i].active) {
            threads[i].pid = pid;
            threads[i].held_count = 0;
            threads[i].active = 1;
            strncpy(threads[i].name, current->comm, TASK_COMM_LEN - 1);
            threads[i].name[TASK_COMM_LEN - 1] = '\0';
            printk(KERN_INFO "[Mini-Lockdep] Created tracking for Thread %d (%s)\n",
                   pid, threads[i].name);
            return &threads[i];
        }
    }

    printk(KERN_WARNING "[Mini-Lockdep] Thread table full, cannot track PID %d\n", pid);
    return NULL;
}

/**
 * add_dependency - Add edge to dependency graph
 * @from: Lock ID acquired first
 * @to: Lock ID acquired second
 */
static void add_dependency(int from, int to)
{
    if (from >= MAX_LOCKS || to >= MAX_LOCKS || from < 0 || to < 0) {
        printk(KERN_WARNING "[Mini-Lockdep] Invalid lock IDs: %d -> %d\n", from, to);
        return;
    }

    if (!graph[from][to]) {
        graph[from][to] = 1;
        printk(KERN_INFO "[Mini-Lockdep] Added dependency: Lock%d -> Lock%d\n", from, to);
    }
}

/**
 * dfs - Depth-first search for cycle detection
 * @node: Current node in traversal
 * @visited: Array tracking visited nodes
 * @rec_stack: Recursion stack (detects back edges)
 * Returns: 1 if cycle detected, 0 otherwise
 */
static int dfs(int node, int visited[], int rec_stack[])
{
    int i;

    visited[node] = 1;
    rec_stack[node] = 1;

    /* Check all adjacent nodes */
    for (i = 0; i < MAX_LOCKS; i++) {
        if (graph[node][i]) {
            if (!visited[i]) {
                if (dfs(i, visited, rec_stack)) {
                    return 1;
                }
            } else if (rec_stack[i]) {
                /* Back edge found - cycle detected */
                return 1;
            }
        }
    }

    rec_stack[node] = 0;
    return 0;
}

/**
 * detect_cycle - Check if dependency graph has a cycle
 * Returns: 1 if cycle (deadlock) detected, 0 otherwise
 */
static int detect_cycle(void)
{
    int visited[MAX_LOCKS] = {0};
    int rec_stack[MAX_LOCKS] = {0};
    int i;

    for (i = 0; i < MAX_LOCKS; i++) {
        if (!visited[i]) {
            if (dfs(i, visited, rec_stack)) {
                return 1;
            }
        }
    }

    return 0;
}

/**
 * print_graph_state - Debug: print current dependency graph
 */
static void print_graph_state(void)
{
    int i, j;
    printk(KERN_DEBUG "[Mini-Lockdep] Current dependency graph:\n");
    for (i = 0; i < MAX_LOCKS; i++) {
        for (j = 0; j < MAX_LOCKS; j++) {
            if (graph[i][j]) {
                printk(KERN_DEBUG "  Lock%d -> Lock%d\n", i, j);
            }
        }
    }
}

/* ============================================================
 * CORE LOCKDEP FUNCTIONS
 * ============================================================ */

/**
 * mini_lock_acquire - Simulate lock acquisition
 * @lock_id: ID of lock being acquired
 */
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

    printk(KERN_INFO "[Mini-Lockdep] Thread %d (%s) acquiring Lock%d\n",
           pid, tdata->name, lock_id);

    /* Add dependencies from all currently held locks to this new lock */
    for (i = 0; i < tdata->held_count; i++) {
        add_dependency(tdata->held[i], lock_id);
    }

    /* Check for cycles after adding dependencies */
    if (detect_cycle()) {
        printk(KERN_WARNING "[Mini-Lockdep] *** POTENTIAL DEADLOCK DETECTED ***\n");
        print_graph_state();
    }

    /* Add lock to held list */
    if (tdata->held_count < MAX_HELD_LOCKS) {
        tdata->held[tdata->held_count] = lock_id;
        tdata->held_count++;
        printk(KERN_INFO "[Mini-Lockdep] Lock%d added to Thread %d's held list (count: %d)\n",
               lock_id, pid, tdata->held_count);
    } else {
        printk(KERN_WARNING "[Mini-Lockdep] Thread %d hold list full\n", pid);
    }

    spin_unlock(&state_lock);
}

/**
 * mini_lock_release - Simulate lock release
 * @lock_id: ID of lock being released
 */
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

    /* Find and remove lock from held list */
    for (i = 0; i < tdata->held_count; i++) {
        if (tdata->held[i] == lock_id) {
            /* Shift remaining locks down */
            for (int j = i; j < tdata->held_count - 1; j++) {
                tdata->held[j] = tdata->held[j + 1];
            }
            tdata->held_count--;
            printk(KERN_INFO "[Mini-Lockdep] Thread %d released Lock%d (remaining: %d)\n",
                   pid, lock_id, tdata->held_count);
            spin_unlock(&state_lock);
            return;
        }
    }

    printk(KERN_WARNING "[Mini-Lockdep] Thread %d tried to release Lock%d (not held)\n",
           pid, lock_id);
    spin_unlock(&state_lock);
}

/* ============================================================
 * TEST SCENARIOS
 * ============================================================ */

static struct task_struct *thread1, *thread2;

/**
 * test_safe_scenario - Demonstrate safe lock ordering
 * Both threads acquire locks in same order (1 -> 2)
 */
static int test_safe_scenario(void *arg)
{
    int thread_id = (int)(long)arg;
    printk(KERN_INFO "[Mini-Lockdep TEST] Safe scenario - Thread %d starting\n", thread_id);

    mini_lock_acquire(1);
    msleep(50);
    mini_lock_acquire(2);

    printk(KERN_INFO "[Mini-Lockdep TEST] Safe scenario - Thread %d doing work\n", thread_id);
    msleep(100);

    mini_lock_release(2);
    mini_lock_release(1);

    printk(KERN_INFO "[Mini-Lockdep TEST] Safe scenario - Thread %d done\n", thread_id);
    return 0;
}

/**
 * test_deadlock_scenario_thread_a - Thread for deadlock demo (acquires 1 then 2)
 */
static int test_deadlock_scenario_a(void *arg)
{
    printk(KERN_INFO "[Mini-Lockdep TEST] Deadlock scenario - Thread A starting\n");

    mini_lock_acquire(1);
    msleep(200);
    mini_lock_acquire(2);

    printk(KERN_INFO "[Mini-Lockdep TEST] Deadlock scenario - Thread A doing work\n");
    msleep(100);

    mini_lock_release(2);
    mini_lock_release(1);

    printk(KERN_INFO "[Mini-Lockdep TEST] Deadlock scenario - Thread A done\n");
    return 0;
}

/**
 * test_deadlock_scenario_thread_b - Thread for deadlock demo (acquires 2 then 1)
 */
static int test_deadlock_scenario_b(void *arg)
{
    msleep(100);  /* Let thread A acquire lock 1 first */

    printk(KERN_INFO "[Mini-Lockdep TEST] Deadlock scenario - Thread B starting\n");

    mini_lock_acquire(2);
    msleep(200);
    mini_lock_acquire(1);

    printk(KERN_INFO "[Mini-Lockdep TEST] Deadlock scenario - Thread B doing work\n");
    msleep(100);

    mini_lock_release(1);
    mini_lock_release(2);

    printk(KERN_INFO "[Mini-Lockdep TEST] Deadlock scenario - Thread B done\n");
    return 0;
}

/* ============================================================
 * MODULE INITIALIZATION AND CLEANUP
 * ============================================================ */

static int __init mini_lockdep_init(void)
{
    int i, j;

    printk(KERN_INFO "========================================\n");
    printk(KERN_INFO "[Mini-Lockdep] Module loading...\n");
    printk(KERN_INFO "========================================\n");

    spin_lock_init(&state_lock);

    /* Initialize thread table */
    for (i = 0; i < MAX_THREADS; i++) {
        threads[i].active = 0;
        threads[i].held_count = 0;
    }

    /* Initialize dependency graph (no dependencies initially) */
    for (i = 0; i < MAX_LOCKS; i++) {
        for (j = 0; j < MAX_LOCKS; j++) {
            graph[i][j] = 0;
        }
    }

    printk(KERN_INFO "[Mini-Lockdep] Data structures initialized\n");
    printk(KERN_INFO "[Mini-Lockdep] Running test scenarios...\n\n");

    /* TEST 1: Safe scenario - same lock order for both threads */
    printk(KERN_INFO "=== TEST 1: SAFE SCENARIO ===\n");
    printk(KERN_INFO "Two threads acquiring locks in same order (1 -> 2)\n");
    printk(KERN_INFO "Expected: No deadlock warning\n\n");

    thread1 = kthread_run(test_safe_scenario, (void *)1, "lockdep_test_t1");
    if (IS_ERR(thread1)) {
        printk(KERN_ERR "[Mini-Lockdep] Failed to create thread 1\n");
        return PTR_ERR(thread1);
    }

    thread2 = kthread_run(test_safe_scenario, (void *)2, "lockdep_test_t2");
    if (IS_ERR(thread2)) {
        printk(KERN_ERR "[Mini-Lockdep] Failed to create thread 2\n");
        kthread_stop(thread1);
        return PTR_ERR(thread2);
    }

    /* Wait for safe scenario to complete */
    kthread_stop(thread1);
    kthread_stop(thread2);
    msleep(500);

    printk(KERN_INFO "=== TEST 1 COMPLETE ===\n\n");

    /* Reset for next test */
    for (i = 0; i < MAX_THREADS; i++) {
        threads[i].active = 0;
        threads[i].held_count = 0;
    }
    for (i = 0; i < MAX_LOCKS; i++) {
        for (j = 0; j < MAX_LOCKS; j++) {
            graph[i][j] = 0;
        }
    }

    /* TEST 2: Deadlock scenario - circular lock ordering */
    printk(KERN_INFO "=== TEST 2: DEADLOCK SCENARIO ===\n");
    printk(KERN_INFO "Thread A acquires locks: 1 -> 2\n");
    printk(KERN_INFO "Thread B acquires locks: 2 -> 1\n");
    printk(KERN_INFO "Expected: Deadlock warning when cycle is detected\n\n");

    thread1 = kthread_run(test_deadlock_scenario_a, NULL, "lockdep_deadlock_a");
    if (IS_ERR(thread1)) {
        printk(KERN_ERR "[Mini-Lockdep] Failed to create deadlock test thread A\n");
        return PTR_ERR(thread1);
    }

    thread2 = kthread_run(test_deadlock_scenario_b, NULL, "lockdep_deadlock_b");
    if (IS_ERR(thread2)) {
        printk(KERN_ERR "[Mini-Lockdep] Failed to create deadlock test thread B\n");
        kthread_stop(thread1);
        return PTR_ERR(thread2);
    }

    /* Wait for deadlock scenario to complete */
    kthread_stop(thread1);
    kthread_stop(thread2);
    msleep(500);

    printk(KERN_INFO "=== TEST 2 COMPLETE ===\n\n");

    printk(KERN_INFO "========================================\n");
    printk(KERN_INFO "[Mini-Lockdep] Module loaded successfully\n");
    printk(KERN_INFO "[Mini-Lockdep] Check dmesg for output\n");
    printk(KERN_INFO "========================================\n");

    return 0;
}

static void __exit mini_lockdep_exit(void)
{
    printk(KERN_INFO "========================================\n");
    printk(KERN_INFO "[Mini-Lockdep] Module unloading...\n");
    printk(KERN_INFO "========================================\n");
    printk(KERN_INFO "[Mini-Lockdep] Cleanup complete\n");
}

module_init(mini_lockdep_init);
module_exit(mini_lockdep_exit);
