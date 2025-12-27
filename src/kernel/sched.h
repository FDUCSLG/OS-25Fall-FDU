#pragma once

#include <kernel/proc.h>

void init_sched();
void init_schinfo(struct schinfo *);

bool _activate_proc(Proc *, bool onalert);
#define activate_proc(proc) _activate_proc(proc, false)
#define alert_proc(proc) _activate_proc(proc, true)

WARN_RESULT bool is_zombie(Proc *);
WARN_RESULT bool is_unused(Proc *);
void acquire_sched_lock();
void release_sched_lock();
void sched(enum procstate new_state);

// MUST call lock_for_sched() before sched() !!!
#define yield() (/*printk("[SCHED]yielding...\n"),*/  acquire_sched_lock(), sched(RUNNABLE))

WARN_RESULT Proc *thisproc();
