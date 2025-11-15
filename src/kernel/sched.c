#include <kernel/sched.h>
#include <kernel/proc.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <aarch64/intrinsic.h>
#include <kernel/cpu.h>
#include <common/rbtree.h>

extern bool panic_flag;
extern void swtch(KernelContext *new_ctx, KernelContext **old_ctx);

static SpinLock shecdlock; 
static ListNode rq;

void init_sched()
{
    // TODO: initialize the scheduler
    // 1. initialize the resources (e.g. locks, semaphores)
    init_spinlock(&shecdlock);
    init_list_node(&rq);
    // 2. initialize the scheduler info of each CPU
    for (int i = 0; i < NCPU; i++) {
        Proc* p = kalloc(sizeof(Proc));
        p->idle = true;
        p->state = RUNNING;
        p->pid = -1; 
        cpus[i].sched.thisproc = cpus[i].sched.idle = p;
    }
    printk("[SCHED] Scheduler initialized\n");
}

Proc *thisproc()
{
    return cpus[cpuid()].sched.thisproc;
}

void init_schinfo(struct schinfo *p)
{
    init_list_node(&p->rqnode);
    // TODO: initialize your customized schinfo for every newly-created process
}

void acquire_sched_lock()
{
    //printk("[SCHED] CPU%lld: acquiring sched lock\n", cpuid());
    acquire_spinlock(&shecdlock); 
    // TODO: acquire the sched_lock if need
}

void release_sched_lock()
{
    //printk("[SCHED] CPU%lld: releasing sched lock\n", cpuid());
    release_spinlock(&shecdlock); 
    // TODO: release the sched_lock if need
}

bool is_zombie(Proc *p)
{
    bool r;
    acquire_sched_lock();
    r = p->state == ZOMBIE;
    release_sched_lock();
    return r;
}


bool activate_proc(Proc *p) {
    // if the proc->state is RUNNING/RUNNABLE, do nothing and return false
    // if the proc->state is SLEEPING/UNUSED, set the process state to RUNNABLE, add it to the sched queue, and return true
    acquire_sched_lock();
    // if the proc->state is RUNNING/RUNNABLE, do nothing
    //printk("[SCHED] Activating proc pid=%d, current state=%d, idle=%d\n", p->pid, p->state, p->idle);
    if (p->state==RUNNING || p->state==RUNNABLE || p->state==ZOMBIE) {
        release_sched_lock();
        return false; 
    }
    // if the proc->state if SLEEPING/UNUSED, set the process state to RUNNABLE and add it to the sched queue
    if (p->state==SLEEPING || p->state==UNUSED) {
        p->state = RUNNABLE;
        _insert_into_list(&rq, &p->schinfo.rqnode);
    }
    release_sched_lock();
    return true;
}

static void update_this_state(enum procstate new_state)
{
    //printk("[SCHED][DEBUG] CPU%lld: updating proc pid=%d state from %d to %d\n", cpuid(), thisproc()->pid, thisproc()->state, new_state);
    // Update the state of current process and modify the sched queue
    auto origin_state = thisproc()->state;
    thisproc()->state = new_state;
    if (thisproc()->idle) return;  // idle process, skip queue management
    
    // Remove from queue if transitioning to non-runnable state (SLEEPING/ZOMBIE)
    if((origin_state==RUNNABLE || origin_state==RUNNING) && 
       (new_state!=RUNNING && new_state != RUNNABLE)) {
        _detach_from_list(&thisproc()->schinfo.rqnode);
    }
    
    // Add to queue if transitioning from non-runnable to runnable
    if ((origin_state!=RUNNABLE && origin_state!=RUNNING) && 
        (new_state==RUNNABLE || new_state==RUNNING)) {
        _insert_into_list(&rq, &thisproc()->schinfo.rqnode);
    }
    
    // CRITICAL FIX: When yielding (RUNNING->RUNNABLE), move to tail for round-robin
    if (origin_state == RUNNING && new_state == RUNNABLE) {
        _detach_from_list(&thisproc()->schinfo.rqnode);
        _insert_into_list(&rq, &thisproc()->schinfo.rqnode);
    }

}

static Proc *pick_next()
{
    // Round-robin: pick first RUNNABLE process from queue
    // Processes naturally rotate as they yield/block and get re-added to tail
    Proc *selected_proc = NULL;
    _for_in_list(p, &rq) {
        if(p == &rq) continue;
        auto proc = container_of(p, Proc, schinfo.rqnode);
        if(proc->state == RUNNABLE) {
            selected_proc = proc;
            break;  // Take first RUNNABLE, ensures fair round-robin
        }
    }
    if(selected_proc) {
        return selected_proc;
    }

    return cpus[cpuid()].sched.idle;
}

static void update_this_proc(Proc *p)

{
    // Scheduler lock is expected to be held by caller.
    // Update the CPU-local current process pointer so interrupt/trap handlers
    // and other code reading thisproc() will see the new process immediately.
    cpus[cpuid()].sched.thisproc = p;
    // Maintain idle flag: processes with pid == -1 are the per-cpu idle process
    if (p && p->pid == -1) p->idle = true;
    else if (p) p->idle = false;
}


// // A simple scheduler.
// // You are allowed to replace it with whatever you like.
// // call with sched_lock
void sched(enum procstate new_state)
{
    auto this = thisproc();
    //printk("[SCHED] CPU%lld: scheduling proc pid=%d (idle=%d) to state=%d, init_state=%d\n", cpuid(), this ? this->pid : -999, this ? this->idle : -1, new_state, this->state);
    ASSERT(this->state == RUNNING);
    //printk("[SCHED] rq: \n");
    update_this_state(new_state);
    auto next = pick_next();
    update_this_proc(next);
    //printk("[SCHED] CPU%lld: switching from pid=%d to pid=%d\n", cpuid(), this ? this->pid : -999, next ? next->pid : -999);
    //printk("[SCHED] idle=%d, this->state=%d, next->state=%d, cpuid=%lld\n", this->idle, this->state, next->state, cpuid());
    ASSERT(next->state == RUNNABLE);
    next->state = RUNNING;
    if (next != this) {
        swtch(next->kcontext, &this->kcontext);
    } else {
    }
    release_sched_lock();
}

u64 proc_entry(void (*entry)(u64), u64 arg)
{
    release_sched_lock();
    set_return_addr(entry);
    return arg;
}

