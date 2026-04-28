# Mini-Lockdep: Design & Logic Explanation

## Executive Summary

Mini-Lockdep detects potential deadlocks by:
1. **Tracking** which locks each thread holds
2. **Building** a dependency graph from lock acquisition order
3. **Detecting** cycles in the graph (cycles = deadlocks)
4. **Reporting** findings via kernel logs

The core algorithm is graph cycle detection using DFS (Depth-First Search).

---

## Part 1: Thread-Aware Lock Tracking

### Data Structure

```c
struct thread_locks {
    pid_t pid;                    // Process ID - unique identifier
    char name[TASK_COMM_LEN];    // Human-readable name (e.g., "kworker/1")
    int held[MAX_HELD_LOCKS];    // Ring of lock IDs currently held
    int held_count;              // Number of locks currently held (0-10)
    int active;                  // Is this entry in use?
};
```

### Why This Design?

- **Fixed array (MAX_THREADS=10)**: Avoids dynamic allocation in kernel
- **held array**: Maintains **order** of acquisition (critical for dependency ordering)
- **held_count**: Tracks how many locks are currently held
- **active flag**: Distinguishes empty vs. inactive entries

### Example State

```
Thread 1234 (Thread A):
  pid: 1234
  name: "lockdep_test_a"
  held: [1, 2, ?, ?, ...]    ← Acquired Lock1 first, then Lock2
  held_count: 2
  active: 1

Thread 5678 (Thread B):
  pid: 5678
  name: "lockdep_test_b"
  held: [3, ?, ?, ?, ...]    ← Acquired Lock3 only
  held_count: 1
  active: 1
```

### Function: `get_thread_data(pid_t pid)`

```
Input: Thread's PID
Process:
  1. Search thread table for PID
     - If found: return pointer
     - If not found: proceed to step 2
  
  2. Find empty slot (active == 0)
     - If found: initialize and return
     - If not found: table is full, return NULL

Output: Pointer to thread's lock data (or NULL if full)
```

**Key insight**: First access creates entry, subsequent accesses retrieve it.

### Synchronization

```c
spin_lock(&state_lock);
tdata = get_thread_data(pid);  // Protected read/write
spin_unlock(&state_lock);
```

All thread table modifications are protected by spinlock.

---

## Part 2: Dependency Graph Construction

### The Core Problem

**Goal**: Determine which locks must be acquired before which other locks.

**Key observation**: If a thread holds Lock A, then acquires Lock B:
- Lock A was acquired **before** Lock B
- Lock A and B have a **dependency relationship**: A → B

### Data Structure

```c
int graph[MAX_LOCKS][MAX_LOCKS];
```

**Interpretation**:
- `graph[i][j] = 1` means: Lock `i` must be acquired before Lock `j`
- `graph[i][j] = 0` means: No known dependency (yet)

### Visual Example

If thread acquires locks: 1 → 2 → 3

```
After acquiring Lock 1:
  held = [1]
  No dependencies added (first lock acquired)

After acquiring Lock 2 (while holding Lock 1):
  held = [1, 2]
  Add edge: 1 → 2 (Lock 1 before Lock 2)
  
  graph[1][2] = 1
  
  Visual:
  ┌─────┐
  │ 1 ──┼──→ 2
  └─────┘

After acquiring Lock 3 (while holding Locks 1 and 2):
  held = [1, 2, 3]
  Add edge: 1 → 3 (Lock 1 before Lock 3)
  Add edge: 2 → 3 (Lock 2 before Lock 3)
  
  graph[1][3] = 1
  graph[2][3] = 1
  
  Visual:
      ┌────→ 3
      │    ↗
      1 ──→ 2
```

### Function: `add_dependency(int from, int to)`

```
Input: from (source lock), to (destination lock)
Process:
  1. Validate lock IDs (0 ≤ from, to < MAX_LOCKS)
  2. If graph[from][to] == 0:
     - Set graph[from][to] = 1
     - Log: "Added dependency: Lock{from} -> Lock{to}"
     - (Avoid duplicate edges)

Output: None (modifies graph in-place)
```

### Multiple Threads Building Same Graph

**Scenario**: Thread A and Thread B both hold Lock 1, then acquire different locks

```
Thread A:                          Thread B:
  Acquire Lock 1                    Acquire Lock 1
  held = [1]                        held = [1]
  
  Acquire Lock 2                    Acquire Lock 3
  held = [1, 2]                     held = [1, 3]
  → Add dependency 1 → 2            → Add dependency 1 → 3

Result: Merged graph
  1 → 2
  1 → 3
```

The graph represents the **combined constraints** from all threads.

### Why Adjacency Matrix?

- **Advantage**: O(1) lookup/update (`graph[i][j]`)
- **Limitation**: Fixed size (but acceptable for educational module with MAX_LOCKS=10)
- **Alternative**: Adjacency list would use less memory for sparse graphs

---

## Part 3: Deadlock Detection via Cycle Detection

### The Deadlock = Cycle Theorem

**Deadlock occurs when**: Lock acquisition forms a cycle

```
SAFE scenario (no cycle):
  1 → 2  (Thread: acquire 1, then 2)
  1 → 3  (Thread: acquire 1, then 3)
  Result: ✓ No cycles - no deadlock possible

UNSAFE scenario (has cycle):
  1 → 2  (Thread A: acquire 1, then 2)
  2 → 1  (Thread B: acquire 2, then 1)
  Result: ✗ Cycle 1→2→1 - deadlock likely!
```

**Why?** With a cycle, threads wait in a circle forever:
- Thread A waits for Thread B to release Lock 2
- Thread B waits for Thread A to release Lock 1
- Circular wait = deadlock

### Algorithm: Depth-First Search (DFS)

DFS discovers cycles by detecting **back edges** (edges to ancestors in the DFS tree).

#### Visualization

```
Graph with cycle: 1 → 2 → 3 → 1

Step 1: Start at Node 1
  Visited: [1]
  RecStack: [1]
  
Step 2: Follow edge to Node 2
  Visited: [1, 2]
  RecStack: [1, 2]
  
Step 3: Follow edge to Node 3
  Visited: [1, 2, 3]
  RecStack: [1, 2, 3]
  
Step 4: Follow edge to Node 1
  Node 1 already in RecStack!
  → BACK EDGE FOUND → CYCLE DETECTED ✗
```

#### Code Structure

```c
int dfs(int node, int visited[], int rec_stack[])
{
    // 1. Mark as visited and add to recursion stack
    visited[node] = 1;
    rec_stack[node] = 1;
    
    // 2. Explore all neighbors
    for (each neighbor of node) {
        if (graph[node][neighbor]) {
            if (!visited[neighbor]) {
                // Unvisited: recursively explore
                if (dfs(neighbor, visited, rec_stack)) {
                    return 1;  // Cycle found in subtree
                }
            } else if (rec_stack[neighbor]) {
                // Visited AND in recursion stack: BACK EDGE!
                return 1;  // Cycle found
            }
        }
    }
    
    // 3. Backtrack: remove from recursion stack
    rec_stack[node] = 0;
    return 0;  // No cycle from this node
}
```

### Why Recursion Stack?

**Problem**: A node might be visited from different paths.
- `visited[i]` tells us "we've seen this before"
- But it doesn't tell us if we're visiting an **ancestor**

**Solution**: `rec_stack[i]` tracks only ancestors in current DFS path.
- Back edge = revisiting ancestor in same path = cycle

```
Graph: 1 → 2    1 → 3
          ↓        ↓
          3        2

DFS Path 1: 1 → 2 → 3
  visited[3] = 1 (from first path)
  rec_stack[3] = 0 (not in current path)
  → Not a back edge, no cycle

DFS Path 2: 1 → 3 → 2
  visited[2] = 1 (from first path)
  rec_stack[2] = 0 (not in current path)
  → Not a back edge, no cycle
```

### Function: `detect_cycle(void)`

```c
int detect_cycle(void)
{
    int visited[MAX_LOCKS] = {0};
    int rec_stack[MAX_LOCKS] = {0};
    
    // Try DFS from each unvisited node
    for (i = 0; i < MAX_LOCKS; i++) {
        if (!visited[i]) {
            if (dfs(i, visited, rec_stack)) {
                return 1;  // Cycle found!
            }
        }
    }
    
    return 0;  // No cycles found
}
```

### Time Complexity

- **DFS**: O(V + E) where V = nodes, E = edges
- **detect_cycle**: Calls DFS once per unvisited node, but each edge/node visited once total
- **Overall**: O(MAX_LOCKS²) in worst case (fully connected graph)
- **Practical**: Very fast for 10 locks

---

## Part 4: Lock Acquisition & Release

### Function: `mini_lock_acquire(int lock_id)`

This is the **heart** of the module. Here's the flow:

```
1. Validate lock ID (0 ≤ lock_id < MAX_LOCKS)
2. Get current thread's data
   - If no data → thread not yet tracked
   - Create new tracking entry

3. For each lock currently held by thread:
   - Call add_dependency(held[i], lock_id)
   - Example: thread holds Lock1, acquiring Lock2 → add 1→2

4. Check for cycles in updated graph
   - If cycle found: print WARNING
   - Print current graph state

5. Add lock_id to thread's held list
   - Append to held[] array
   - Increment held_count
```

#### Example Execution

```
Thread A acquiring locks in sequence:

Step 1: Acquire Lock 1
  Thread A held = []
  No dependencies to add (no locks held)
  Check cycle: No cycle
  Add Lock1 to held
  Thread A held = [1]

Step 2: Acquire Lock 2
  Thread A held = [1]
  Add dependency: 1 → 2
  Check cycle: No cycle (1→2 is path, not circle)
  Add Lock2 to held
  Thread A held = [1, 2]

Step 3: Acquire Lock 3
  Thread A held = [1, 2]
  Add dependency: 1 → 3
  Add dependency: 2 → 3
  Check cycle: No cycle (1→2→3 is path)
  Add Lock3 to held
  Thread A held = [1, 2, 3]
```

### Function: `mini_lock_release(int lock_id)`

```c
void mini_lock_release(int lock_id)
{
    // 1. Find lock_id in thread's held list
    for (i = 0; i < held_count; i++) {
        if (held[i] == lock_id) {
            // 2. Shift remaining locks down (maintain order)
            for (j = i; j < held_count - 1; j++) {
                held[j] = held[j + 1];
            }
            // 3. Decrement count
            held_count--;
            return;
        }
    }
    // If not found: log warning
}
```

**Key point**: Releasing doesn't remove edges from graph.
- Graph represents **all observed** orderings (historical)
- Removing edges would lose deadlock information
- In real Lockdep, edges might be removed with sophisticated heuristics

---

## Part 5: Test Scenarios

### Test 1: Safe Scenario (No Deadlock)

**Setup**:
```
Thread 1: Acquire 1, then 2
Thread 2: Acquire 1, then 2
```

**Expected Graph**:
```
1 → 2
```

**Cycle Detection**:
- DFS from 1: visits 2, no outgoing edges → no cycle
- DFS from 2: no outgoing edges → no cycle
- **Result**: No cycle ✓

**Output**: "No warning" (silence is success)

### Test 2: Deadlock Scenario (Cycle Detected)

**Setup**:
```
Thread A: Acquire 1, then 2
Thread B: Acquire 2, then 1 (with delay)
```

**Timeline**:
```
T=0ms:   Thread A acquires Lock 1
         Thread A held = [1]
         
T=0ms:   Thread B starts but sleeps 100ms
         
T=100ms: Thread B acquires Lock 2
         Thread B held = [2]
         Add dependency: 2 → 1
         (no cycle yet)
         
T=100ms: Thread A acquires Lock 2
         Thread A held = [1, 2]
         Add dependency: 1 → 2
         
T=100ms: Graph now has:
         1 → 2
         2 → 1  ← CYCLE!
         
         DFS finds: 1 → 2 → 1 (back edge)
         ** POTENTIAL DEADLOCK DETECTED **
```

**Output**: Prints deadlock warning + graph state

---

## Part 6: Synchronization & Safety

### Critical Section

```c
spin_lock(&state_lock);

/* Critical section */
get_thread_data(pid);      // Read/write to threads[]
add_dependency(...);       // Read/write to graph[]
detect_cycle();           // Read from graph[]

spin_unlock(&state_lock);
```

### Why Spinlock?

1. **Kernel context**: Module runs in kernel, spinlocks are the primitive
2. **Non-blocking**: Spinlocks busy-wait, appropriate for short sections
3. **Interrupt-safe**: Prevents interrupts while holding lock (if needed)
4. **Simplicity**: Easier to reason about than more complex primitives

### Race Condition Example (without spinlock)

```
Thread A:                        Thread B:
Read threads[].held_count = 2
                                 Read threads[].held_count = 2
Add Lock 3
                                 Add Lock 3
Write held_count = 3
                                 Write held_count = 3

Result: Lock 3 added twice! Data corruption.

With spinlock: operations serialize
```

---

## Part 7: Limitations & Assumptions

### Simplifications vs. Real Lockdep

| Aspect | Mini-Lockdep | Real Lockdep |
|--------|-------------|------------|
| **Graph size** | 10 locks | Thousands |
| **Memory** | Stack-allocated | Dynamic |
| **Lock types** | Simulated | Real (mutex, spinlock, etc.) |
| **False positives** | Possible | Filtered by heuristics |
| **Scalability** | Not | Yes |

### Known Limitations

1. **Fixed size**: Can't track > 10 locks or 10 threads
2. **No forgetting**: Edges never removed (conservative but may accumulate)
3. **No lock re-entrant handling**: Same thread acquiring same lock multiple times
4. **Simulated locks**: Not integrating with actual kernel locks

### Why These Tradeoffs?

- **Educational goal**: Simplicity > features
- **Kernel constraints**: Avoid dynamic allocation
- **Demo scope**: 10 locks sufficient for examples
- **Correctness**: Conservative (no false negatives)

---

## Part 8: Key Design Decisions Explained

### Decision 1: Adjacency Matrix vs. List

```c
// CHOSEN: Adjacency matrix
int graph[MAX_LOCKS][MAX_LOCKS];

// ALTERNATIVE: Adjacency list
struct edge {
    int from, to;
} edges[MAX_EDGES];
```

**Why matrix?**
- O(1) edge lookup: `if (graph[a][b])`
- Simpler DFS implementation
- Space: 10×10 = 100 ints = 400 bytes (acceptable)
- List would save space for sparse graphs but add complexity

### Decision 2: Per-Thread Tracking vs. Per-Lock

```c
// CHOSEN: Track held locks per thread
struct thread_locks {
    pid_t pid;
    int held[MAX_HELD_LOCKS];
};

// ALTERNATIVE: Track holding threads per lock
struct lock_info {
    int lock_id;
    pid_t holding_threads[];
};
```

**Why per-thread?**
- Matches the problem domain (thread → locks mapping)
- Easier to compute dependencies (iterate thread's held list)
- Aligns with kernel terminology ("thread-aware")

### Decision 3: DFS Cycle Detection

```c
// CHOSEN: DFS with recursion stack
int dfs(int node, int visited[], int rec_stack[]) { ... }

// ALTERNATIVES:
// 1. Tarjan's algorithm (finds strongly connected components)
// 2. Topological sort (fails if cycle exists)
// 3. Floyd-Warshall (all-pairs shortest path)
```

**Why DFS?**
- Simple to implement and understand
- O(V+E) time complexity
- Standard algorithm in textbooks (educational value)
- Recursion stack clearly shows back edges = cycles

---

## Part 9: How to Extend Mini-Lockdep

### Extension 1: Lock Classes

```c
struct lock_info {
    int lock_id;
    char class[50];  // "spinlock", "mutex", "semaphore"
};

// Separate graph per class
int graph_spinlock[MAX_LOCKS][MAX_LOCKS];
int graph_mutex[MAX_LOCKS][MAX_LOCKS];
```

**Benefit**: Detect cross-class deadlocks only

### Extension 2: Lock Hold Time

```c
struct lock_entry {
    int lock_id;
    unsigned long acquired_ns;  // Nanosecond timestamp
};

// Compute hold duration
unsigned long hold_time = jiffies - acquired_ns;
if (hold_time > threshold) {
    printk("Lock %d held for %lu ns", lock_id, hold_time);
}
```

**Benefit**: Identify performance bottlenecks

### Extension 3: Dynamic Memory

```c
struct thread_entry *thread_table;
int num_threads;

// In init:
thread_table = kmalloc(sizeof(...) * INITIAL_THREADS, GFP_KERNEL);
```

**Benefit**: Scale to arbitrary numbers of threads/locks

### Extension 4: Sysfs Interface

```c
// Expose data to userspace
/sys/kernel/debug/lockdep/
  ├── threads
  ├── graph
  └── stats
```

**Benefit**: Query state without rebooting

---

## Summary: The Flow

```
Lock Acquired
  ↓
[Get thread data] ← Create if needed
  ↓
[For each held lock: add dependency]
  ↓
[Run cycle detection]
  ↓
[If cycle found: print warning]
  ↓
[Add lock to held list]
```

This simple flow implements the complete deadlock detection algorithm!
