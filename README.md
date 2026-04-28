# Mini-Lockdep: Simplified Linux Lockdep Kernel Module

## Overview

Mini-Lockdep is an educational Linux kernel module that implements a simplified version of the Linux kernel's Lockdep (Lock Dependency Checker). It demonstrates the core concepts of:

- **Thread-aware lock tracking**: Monitors which locks each thread holds
- **Dependency graph construction**: Builds relationships between locks based on acquisition order
- **Deadlock cycle detection**: Uses depth-first search (DFS) to identify potential deadlock cycles
- **Kernel logging**: Reports all activities through kernel logs (`dmesg`)

## Architecture

### Data Structures

#### Thread Tracking
```c
struct thread_locks {
    pid_t pid;                          // Process ID
    char name[TASK_COMM_LEN];          // Thread name (current->comm)
    int held[MAX_HELD_LOCKS];          // Array of lock IDs currently held
    int held_count;                    // Number of locks held
    int active;                        // Is this entry active?
};
```

#### Dependency Graph
```c
int graph[MAX_LOCKS][MAX_LOCKS];  // Adjacency matrix
```
- `graph[a][b] = 1` means: Lock `a` must be acquired before Lock `b`
- Used to track potential lock ordering violations

### Key Limits
- `MAX_LOCKS`: 10 (total unique locks in system)
- `MAX_THREADS`: 10 (maximum concurrent threads tracked)
- `MAX_HELD_LOCKS`: 10 (maximum locks one thread can hold simultaneously)

## Core Functions

### Thread Management
- **`get_thread_data(pid_t pid)`**: Finds existing thread entry or creates new one
  - Searches thread table for matching PID
  - Creates new entry if found and space available
  - Returns pointer to thread's lock tracking data

### Lock Operations
- **`mini_lock_acquire(int lock_id)`**: Simulate lock acquisition
  - Identifies current thread
  - Adds dependencies from all currently held locks to new lock
  - Detects cycles after adding dependencies
  - Adds lock to thread's held list
  
- **`mini_lock_release(int lock_id)`**: Simulate lock release
  - Removes lock from thread's held list
  - Maintains chronological order

### Graph Operations
- **`add_dependency(int from, int to)`**: Add edge to dependency graph
  - Sets `graph[from][to] = 1`
  - Prevents duplicate edges
  
- **`detect_cycle()`**: Check for deadlock
  - Runs DFS from each unvisited node
  - Returns 1 if cycle found, 0 otherwise
  
- **`dfs(int node, visited[], rec_stack[])`**: Depth-first search helper
  - Marks current node as visited and adds to recursion stack
  - Recursively checks all adjacent nodes
  - Detects back edges (cycle indicators) using recursion stack

## Cycle Detection Algorithm

The module uses **DFS with recursion stack** to detect cycles:

```
For each unvisited node:
  dfs(node):
    1. Mark node as visited
    2. Add node to recursion stack
    3. For each adjacent node:
       a. If not visited: recursively dfs that node
       b. If in recursion stack: CYCLE FOUND
    4. Remove node from recursion stack
```

**Why recursion stack matters**: It detects back edges (edges pointing to ancestors in DFS tree), which indicate cycles.

## Test Scenarios

### Test 1: Safe Lock Ordering (No Deadlock)
**Description**: Two threads acquire locks in the same order

```
Thread 1: Acquire Lock1 -> Acquire Lock2 -> Release both
Thread 2: Acquire Lock1 -> Acquire Lock2 -> Release both

Dependency Graph:
  Lock1 -> Lock2

Result: NO CYCLE → No warning
```

### Test 2: Circular Lock Ordering (Deadlock Detected)
**Description**: Two threads acquire locks in opposite order, creating a cycle

```
Thread A: Acquire Lock1 -> Acquire Lock2
Thread B: Acquire Lock2 -> Acquire Lock1

Dependency Graph:
  Lock1 -> Lock2
  Lock2 -> Lock1  (creates cycle!)

Result: CYCLE DETECTED → "*** POTENTIAL DEADLOCK DETECTED ***"
```

## Build Instructions

### Prerequisites
- Linux kernel headers: `sudo apt-get install linux-headers-$(uname -r)`
- Build tools: `sudo apt-get install build-essential`
- Module building tools: `sudo apt-get install linux-headers-generic`

### Build
```bash
make
```
This generates:
- `mini_lockdep.ko` - Kernel module object
- `mini_lockdep.mod.c` - Generated module wrapper
- Build artifacts in `build/` directory

### Load Module
```bash
sudo insmod mini_lockdep.ko
```
- Module initializes automatically
- Runs both test scenarios
- Cleans up and ready to use

### View Output
```bash
dmesg | tail -100
```
or live monitoring:
```bash
sudo tail -f /var/log/kern.log
```

### Unload Module
```bash
sudo rmmod mini_lockdep
```

### Clean Build Artifacts
```bash
make clean
```

## Expected Output

### Safe Scenario Section
```
=== TEST 1: SAFE SCENARIO ===
Two threads acquiring locks in same order (1 -> 2)
Expected: No deadlock warning

[Mini-Lockdep TEST] Safe scenario - Thread 1 starting
[Mini-Lockdep] Created tracking for Thread 123 (lockdep_test)
[Mini-Lockdep] Thread 123 acquiring Lock1
[Mini-Lockdep] Lock1 added to Thread 123's held list (count: 1)
[Mini-Lockdep] Thread 123 acquiring Lock2
[Mini-Lockdep] Added dependency: Lock1 -> Lock2
[Mini-Lockdep] Lock2 added to Thread 123's held list (count: 2)
...
```

### Deadlock Scenario Section
```
=== TEST 2: DEADLOCK SCENARIO ===
Thread A acquires locks: 1 -> 2
Thread B acquires locks: 2 -> 1
Expected: Deadlock warning when cycle is detected

...
[Mini-Lockdep] Added dependency: Lock2 -> Lock1
[Mini-Lockdep] *** POTENTIAL DEADLOCK DETECTED ***
[Mini-Lockdep] Current dependency graph:
  Lock1 -> Lock2
  Lock2 -> Lock1
```

## Synchronization & Safety

### Spinlock Protection
```c
spin_lock(&state_lock);
/* Critical section - access global thread table and graph */
spin_unlock(&state_lock);
```

- Protects all global state modifications
- Ensures thread-safe access in kernel context
- Prevents race conditions between concurrent threads

### Why Spinlock?
- Kernel module runs in atomic context
- Spinlocks are the appropriate synchronization primitive
- No sleeping allowed in interrupt contexts

## Comparison: Mini-Lockdep vs Real Lockdep

| Aspect | Mini-Lockdep | Real Lockdep |
|--------|-------------|------------|
| **Purpose** | Educational demonstration | Production kernel protection |
| **Scope** | Simplified simulation | Tracks ALL kernel locks (mutexes, spinlocks, semaphores, etc.) |
| **Graph Size** | 10 locks max | Thousands of locks |
| **Thread Tracking** | Manual simulation | Automatic via kernel infrastructure |
| **Cycle Detection** | Simple DFS | Advanced algorithms with heuristics |
| **Diagnostics** | Basic cycle detection | Extensive lock usage patterns, false positive filtering |
| **Performance Impact** | Negligible (demo only) | Measurable but acceptable |
| **Configuration** | Hardcoded limits | Tunable, scales dynamically |

### Learning Value
Mini-Lockdep teaches the **fundamental concept**: potential deadlocks arise from circular lock dependencies. Real Lockdep applies this principle to the entire kernel with much more sophistication.

## Code Quality & Design Decisions

### Readability
- Clear function names: `mini_lock_acquire`, `detect_cycle`
- Comprehensive comments explaining purpose and logic
- Consistent naming conventions (snake_case for functions)

### Modularity
- Separate concerns: tracking, graph operations, cycle detection, testing
- Each function handles one responsibility
- Easy to extend or modify

### Correctness Priority
- Focuses on correct deadlock detection
- Simple bounded arrays avoid dynamic allocation in kernel
- Conservative: reports potential issues (may have false positives, but no false negatives)

### Educational Value
- Clear to understand data flow
- DFS algorithm implementation is straightforward
- Test scenarios demonstrate both safe and unsafe patterns

## Troubleshooting

### Module won't compile
- Ensure kernel headers are installed: `apt-get install linux-headers-$(uname -r)`
- Check compiler version compatibility
- Run `make clean` and rebuild

### Module won't load
- Check kernel module signature requirements: `sudo insmod --help`
- Verify kernel version compatibility
- Use `dmesg` to see any error messages

### No output in dmesg
- Module must be loaded with `sudo insmod`
- Output is only generated during module init (when loaded)
- Use `dmesg` after loading, not before

### Permission denied errors
- All module operations require `sudo`
- `insmod`, `rmmod`, and `dmesg` all need elevation

## Extensions & Future Work

1. **Persistent logging**: Save detection events to file
2. **Multiple lock classes**: Track different lock types separately
3. **Lock acquisition timing**: Measure actual hold times
4. **Statistics**: Count acquisitions, releases, cycles detected
5. **Dynamic memory**: Use `kmalloc` for scalability beyond limits
6. **Integration with sysfs**: Expose data through kernel interface

## Files Included

- `mini_lockdep.c` - Main module implementation (~500 lines)
- `Makefile` - Build configuration
- `README.md` - This documentation

## Author Notes

This module demonstrates the core deadlock detection algorithm in a minimal, understandable way. The key insight is that **deadlocks arise from circular lock dependencies**, which this module detects using standard graph cycle detection techniques.

For production use, see the actual Linux Lockdep implementation in `kernel/locking/lockdep.c` in the kernel source tree.
