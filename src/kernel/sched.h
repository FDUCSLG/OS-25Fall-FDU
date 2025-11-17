#pragma once

#include <kernel/proc.h>

void init_sched();
void init_schinfo(struct schinfo *);

bool activate_proc(Proc *);

bool is_zombie(Proc *);
bool is_unused(Proc *);
void acquire_sched_lock();
void release_sched_lock();
void sched(enum procstate new_state);

// MUST call lock_for_sched() before sched() !!!
#define yield() (/*printk("[SCHED]yielding...\n"),*/  acquire_sched_lock(), sched(RUNNABLE))

Proc *thisproc();
