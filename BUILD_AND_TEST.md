# Mini-Lockdep Build & Testing Quick Guide

## Quick Start (3 Steps)

### 1. Build the Module
```bash
cd /path/to/mini_lockdep
make
```

Expected output:
```
make -C /lib/modules/5.X.X-XX-generic/build M=/path/to/mini_lockdep modules
[Module compilation details...]
  CC [M]  /path/to/mini_lockdep/mini_lockdep.o
  MODPOST /path/to/mini_lockdep/Module.symvers
  LD [M]  /path/to/mini_lockdep/mini_lockdep.ko
```

### 2. Load the Module
```bash
sudo insmod mini_lockdep.ko
```

Expected output (in dmesg):
```
[...] Mini-Lockdep] Module loading...
[...] Mini-Lockdep] Data structures initialized
[...] Mini-Lockdep] Running test scenarios...
```

### 3. View Output
```bash
dmesg | grep "Mini-Lockdep"
```

or for all output with timestamps:
```bash
dmesg | tail -150
```

## Detailed Testing Procedures

### Test Execution Flow

When you run `sudo insmod mini_lockdep.ko`, the module automatically executes:

#### Phase 1: Initialization
- Initializes thread tracking table (10 slots)
- Initializes dependency graph (10×10 adjacency matrix)
- Creates spinlock for synchronization

#### Phase 2: Safe Scenario Test
Creates two kernel threads running simultaneously:
```
Thread 1 (lockdep_test_t1):         Thread 2 (lockdep_test_t2):
  Acquire Lock 1                      Acquire Lock 1
  Sleep 50ms                          Sleep 50ms
  Acquire Lock 2                      Acquire Lock 2
  Sleep 100ms (work)                  Sleep 100ms (work)
  Release Lock 2                      Release Lock 2
  Release Lock 1                      Release Lock 1
```

Expected: No deadlock warning (both acquire in same order)

#### Phase 3: Deadlock Scenario Test
Creates two kernel threads with opposite lock order:
```
Thread A (lockdep_deadlock_a):      Thread B (lockdep_deadlock_b):
  Acquire Lock 1                      (wait 100ms)
  Sleep 200ms                         Acquire Lock 2
  Acquire Lock 2 ← ADDS DEP 1→2      Sleep 200ms
                                      Acquire Lock 1 ← ADDS DEP 2→1
                                                       (cycle detected!)
```

Expected: "*** POTENTIAL DEADLOCK DETECTED ***" warning

### Interpreting Dmesg Output

**Key sections to look for:**

1. **Module Loading**
```
========================================
[Mini-Lockdep] Module loading...
========================================
[Mini-Lockdep] Data structures initialized
```

2. **Test 1 Header**
```
=== TEST 1: SAFE SCENARIO ===
Two threads acquiring locks in same order (1 -> 2)
Expected: No deadlock warning
```

3. **Thread Creation & Lock Operations**
```
[Mini-Lockdep] Created tracking for Thread 1234 (lockdep_test)
[Mini-Lockdep] Thread 1234 acquiring Lock1
[Mini-Lockdep] Added dependency: Lock1 -> Lock2
[Mini-Lockdep] Lock1 added to Thread 1234's held list (count: 1)
```

4. **Critical: Deadlock Detection** (only appears if cycle found)
```
[Mini-Lockdep] *** POTENTIAL DEADLOCK DETECTED ***
[Mini-Lockdep] Current dependency graph:
  Lock1 -> Lock2
  Lock2 -> Lock1
```

5. **Module Unload**
```
========================================
[Mini-Lockdep] Module unloading...
========================================
```

## Common Tasks

### See Full Test Output
```bash
sudo insmod mini_lockdep.ko && dmesg | tail -200 | grep "Mini-Lockdep"
```

### Check if Module is Loaded
```bash
lsmod | grep mini_lockdep
```

Should show:
```
mini_lockdep            XXXXX  0
```

### Unload and Reload (for repeated testing)
```bash
sudo rmmod mini_lockdep && sudo insmod mini_lockdep.ko && dmesg | tail -100
```

### Save Output to File
```bash
dmesg | grep "Mini-Lockdep" > lockdep_output.txt
cat lockdep_output.txt
```

### Monitor in Real-Time
```bash
sudo tail -f /var/log/kern.log | grep "Mini-Lockdep"
```

## Verification Checklist

After loading module, verify:

- [ ] Safe scenario completes without deadlock warnings
- [ ] Deadlock scenario produces "POTENTIAL DEADLOCK DETECTED" message
- [ ] Thread tracking shows correct PIDs and thread names
- [ ] Dependency graph shows Lock1 → Lock2 edge
- [ ] Cycle detection correctly identifies 1→2→1 cycle
- [ ] Module unloads cleanly with rmmod
- [ ] Threads created have names starting with "lockdep_"

## Troubleshooting

### Issue: "insmod: ERROR: could not load module"

**Solution**:
```bash
dmesg | tail -20  # Check error message
sudo modinfo ./mini_lockdep.ko  # Verify module
```

Most common: kernel headers mismatch
```bash
apt-get install linux-headers-$(uname -r)
make clean && make
```

### Issue: No output in dmesg

**Solution**:
1. Verify module was actually loaded:
```bash
lsmod | grep mini_lockdep
```

2. If not shown, try to load with verbose output:
```bash
sudo insmod mini_lockdep.ko
dmesg | tail -20
```

3. Check for permission issues:
```bash
ls -la mini_lockdep.ko
# Should be readable by your user
```

### Issue: Dmesg output is cluttered

**Solution**: Filter to Mini-Lockdep messages only:
```bash
dmesg | grep "\[Mini-Lockdep\]"
```

### Issue: Module won't unload

**Solution**:
```bash
# Force unload (careful!)
sudo rmmod -f mini_lockdep

# Check what processes are using it
sudo lsof | grep mini_lockdep
```

## Test Output Example

Here's what you should see in `dmesg`:

```
[12345.123456] ========================================
[12345.123457] [Mini-Lockdep] Module loading...
[12345.123458] ========================================
[12345.123459] [Mini-Lockdep] Data structures initialized
[12345.123460] [Mini-Lockdep] Running test scenarios...
[12345.123461] 
[12345.123462] === TEST 1: SAFE SCENARIO ===
[12345.123463] Two threads acquiring locks in same order (1 -> 2)
[12345.123464] Expected: No deadlock warning
[12345.123465] 
[12345.123466] [Mini-Lockdep TEST] Safe scenario - Thread 1 starting
[12345.123467] [Mini-Lockdep] Created tracking for Thread 5678 (lockdep_test_t1)
[12345.123468] [Mini-Lockdep] Thread 5678 acquiring Lock1
[12345.123469] [Mini-Lockdep] Lock1 added to Thread 5678's held list (count: 1)
[12345.123470] [Mini-Lockdep TEST] Safe scenario - Thread 1 doing work
[12345.123520] [Mini-Lockdep] Thread 5678 released Lock1 (remaining: 0)
[12345.123521] === TEST 1 COMPLETE ===
[12345.123522] 
[12345.123523] === TEST 2: DEADLOCK SCENARIO ===
[12345.123524] Thread A acquires locks: 1 -> 2
[12345.123525] Thread B acquires locks: 2 -> 1
[12345.123526] Expected: Deadlock warning when cycle is detected
[12345.123527] 
[12345.123528] [Mini-Lockdep TEST] Deadlock scenario - Thread A starting
[12345.123529] [Mini-Lockdep] Created tracking for Thread 5679 (lockdep_deadlock_a)
[12345.123530] [Mini-Lockdep] Thread 5679 acquiring Lock1
[12345.123531] [Mini-Lockdep] Lock1 added to Thread 5679's held list (count: 1)
[12345.123650] [Mini-Lockdep TEST] Deadlock scenario - Thread B starting
[12345.123651] [Mini-Lockdep] Created tracking for Thread 5680 (lockdep_deadlock_b)
[12345.123652] [Mini-Lockdep] Thread 5680 acquiring Lock2
[12345.123653] [Mini-Lockdep] Lock2 added to Thread 5680's held list (count: 1)
[12345.123750] [Mini-Lockdep] Thread 5679 acquiring Lock2
[12345.123751] [Mini-Lockdep] Added dependency: Lock1 -> Lock2
[12345.123752] [Mini-Lockdep] Lock2 added to Thread 5679's held list (count: 2)
[12345.123850] [Mini-Lockdep] Thread 5680 acquiring Lock1
[12345.123851] [Mini-Lockdep] Added dependency: Lock2 -> Lock1
[12345.123852] [Mini-Lockdep] *** POTENTIAL DEADLOCK DETECTED ***
[12345.123853] [Mini-Lockdep] Current dependency graph:
[12345.123854]   Lock1 -> Lock2
[12345.123855]   Lock2 -> Lock1
[12345.123856] [Mini-Lockdep] Lock1 added to Thread 5680's held list (count: 2)
[12345.123950] [Mini-Lockdep TEST] Deadlock scenario - Thread A done
[12345.124050] [Mini-Lockdep TEST] Deadlock scenario - Thread B done
[12345.124051] === TEST 2 COMPLETE ===
[12345.124052] 
[12345.124053] ========================================
[12345.124054] [Mini-Lockdep] Module loaded successfully
[12345.124055] [Mini-Lockdep] Check dmesg for output
[12345.124056] ========================================
```

## Advanced Debugging

### Enable Debug Prints
The module uses `KERN_INFO` and `KERN_DEBUG` levels. To see debug output:
```bash
sudo dmesg -n 7  # Set console log level to DEBUG
sudo insmod mini_lockdep.ko
dmesg | grep "Mini-Lockdep"
```

### Trace System Calls
If you need deeper kernel debugging:
```bash
sudo trace-cmd record -e 'sched_*' -p function sudo insmod mini_lockdep.ko
sudo trace-cmd report
```

### Check Thread Information
```bash
ps aux | grep lockdep
# Should NOT show threads after module finishes (they're cleaned up)
```

## Performance Notes

- Module initialization takes ~500ms (mostly sleeping in tests)
- No measurable kernel overhead once module is loaded
- Spinlock contention negligible with 2 test threads
- Memory usage < 1MB

## Video Demonstration Script

For recording a demo, follow this sequence:

```bash
# 1. Open terminal
# 2. Show dmesg is empty
dmesg | tail -10

# 3. Clear dmesg
sudo dmesg -c

# 4. Build
make clean && make

# 5. Load module (this triggers tests)
sudo insmod mini_lockdep.ko

# 6. Show output
dmesg | grep "Mini-Lockdep" | head -50
# (Point out: Test 1 has no warnings)

dmesg | grep "Mini-Lockdep" | tail -20
# (Point out: Test 2 has DEADLOCK DETECTED warning)

# 7. Unload
sudo rmmod mini_lockdep
```

This demonstrates all core functionality in ~2 minutes.
