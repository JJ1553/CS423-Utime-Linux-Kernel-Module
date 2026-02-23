# UIUC CS 423 MP1

Your Name: Josh Jenks  
Your NetID: JaJenks2  

## Overview
This MP implements a Linux kernel module that tracks userspace CPU time (utime) for registered processes. User programs register a PID by writing to `/proc/mp1/status`, and can query the current tracked list by reading the same proc file.

## Kernel Module (`mp1.c`)

### Procfs Interface
- Creates `/proc/mp1/` and `/proc/mp1/status` in `module_init`.
- `/proc/mp1/status` is created with permission 0666 so any user can read/write.
- **Write**: user writes a decimal PID string (e.g., `echo "123" > /proc/mp1/status`) to register a process.
  - Input is copied from user space with `memdup_user_nul()` and parsed using `kstrtoint()`.
- **Read**: returns one line per registered PID in the format:
  ```
  <pid>: <cpu_use>
  ```
  Implemented using the `seq_file` interface (`single_open` + `seq_read`) to avoid manual offset (`ppos`) handling.

### Data Structure: Kernel Linked List
- Maintains an intrusive kernel linked list of:
  ```c
  struct mp1_entry { pid_t pid; unsigned long cpu_use; ... }
  ```
- Nodes are allocated and freed using the slab allocator (`kmalloc`/`kfree`).
- Duplicate PID writes are ignored (PID is not inserted twice).

### Periodic Updates: Timer + Workqueue (Two Halves)
- A kernel timer fires every **5 seconds** (single shot timer re-armed with `mod_timer()`).
- Timer callback schedules a workqueue job using `schedule_work()` (Top Half).
- Workqueue function (Bottom Half):
  - Locks the list with a mutex (`mp1_lock`)
  - Iterates once over all registered PIDs
  - Updates `cpu_use` using the provided helper `get_cpu_use(pid, &cpu_use)` from `mp1_given.h`
  - Removes dead/exited processes when `get_cpu_use` returns `-1`

### Concurrency / Locking
- Uses a kernel mutex to protect the process list across:
  - proc read (`/proc/mp1/status`)
  - proc write (PID registration)
  - periodic workqueue updates
  - module unload cleanup

### Cleanup
On module unload:
- Stops asynchronous activity first:
  - `del_timer_sync(&mp1_timer)`
  - `cancel_work_sync(&mp1_work)`
- Frees all linked list nodes
- Removes procfs entries (`/proc/mp1/status` then `/proc/mp1`)

## User Program (`userapp.c`)
- Uses `getpid()` to obtain its PID.
- Registers itself by writing its PID to `/proc/mp1/status` using `fopen()`/`fprintf()`.
- Performs CPU intensive computation for ~10–15 seconds.
- Reads `/proc/mp1/status` and prints its contents, then exits.

## How to Run (in the MP0 VM / QEMU)
Example commands:
```bash
make
gcc -O0 -Wall -Wextra -o userapp userapp.c

sudo insmod mp1.ko
./userapp
cat /proc/mp1/status
sudo rmmod mp1.ko
```
