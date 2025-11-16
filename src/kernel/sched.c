#include <kernel/sched.h>
#include <kernel/proc.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <aarch64/intrinsic.h>
#include <kernel/cpu.h>
#include <common/rbtree.h>
extern void swtch(KernelContext *new_ctx, KernelContext **old_ctx);

extern bool panic_flag;
static SpinLock shecdlock; 
static ListNode rq;

static struct timer sched_timer[NCPU]; 
void sched_timer_handler(struct timer* t) {
    t->elapse = 8;
    acquire_sched_lock();
    sched(RUNNABLE);
}

void init_sched()
{
    // TODO: initialize the scheduler
    // 1. initialize the resources (e.g. locks, semaphores)
    init_spinlock(&shecdlock);
    init_list_node(&rq);
    // 2. initialize the scheduler info of each CPU
    for (int i = 0; i < NCPU; i++) {
        // initialize per-CPU scheduler timer
        sched_timer[i].handler = sched_timer_handler;
        sched_timer[i].elapse = 8; // ms tick for scheduler
        // mark as triggered to avoid cancel before first insertion
        sched_timer[i].triggered = true;
    }
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

bool is_unused(Proc *p)
{
    bool r;
    acquire_sched_lock();
    r = p->state == UNUSED;
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

static void update_this_state(enum procstate new_state) {
    // TODO: if you use template sched function, you should implement this routinue
    // update the state of current process to new_state, and modify the sched queue if necessary
    auto origin_state = thisproc()->state;
    thisproc()->state = new_state;
    if (thisproc()->pid == -1) return;
    if((origin_state==RUNNABLE || origin_state==RUNNING) && new_state!=RUNNING && new_state != RUNNABLE) {
        _detach_from_list(&thisproc()->schinfo.rqnode);
    }
    if (origin_state!=RUNNABLE && origin_state!=RUNNING && (new_state==RUNNABLE || new_state==RUNNING)) {
        _insert_into_list(&rq, &thisproc()->schinfo.rqnode);
    }
}

static Proc *pick_next()
{
    // Round-robin: pick first RUNNABLE process from queue
    // choose the next process to run, and return idle if no runnable process
    Proc *selected_proc = NULL;
    int min_load = 1000000;
    _for_in_list(p, &rq) {
        if(p == &rq) continue;
        auto proc = container_of(p, Proc, schinfo.rqnode);
        if(proc->state == RUNNABLE && proc->timeload < min_load) {
            selected_proc = proc;
            min_load = proc->timeload;
        }
    }
    if(selected_proc) {
        selected_proc->timeload++;
        return selected_proc;
    }
    //printk("[SCHED] CPU%lld: no RUNNABLE process, returning idle\n", cpuid());
    return cpus[cpuid()].sched.idle;
}

static void update_this_proc(Proc *p) {
    // TODO: you should implement this routinue
    // update thisproc to the choosen process
    cpus[cpuid()].sched.thisproc = p;
    if (!sched_timer[cpuid()].triggered) {
        cancel_cpu_timer(&sched_timer[cpuid()]);
    }
    set_cpu_timer(&sched_timer[cpuid()]);
}

// // A simple scheduler.
// // You are allowed to replace it with whatever you like.
// // call with sched_lock
void sched(enum procstate new_state)
{
    auto this = thisproc();
    //printk("[SCHED] CPU%lld: scheduling proc pid=%d (idle=%d) to state=%d, init_state=%d\n", cpuid(), this ? this->pid : -999, this ? this->idle : -1, new_state, this->state);
    ASSERT(this->state == RUNNING);
    if (this->killed && new_state!=ZOMBIE) { release_sched_lock(); return; }
    //printk("[SCHED] rq: \n");
    update_this_state(new_state);
    auto next = pick_next();
    update_this_proc(next);
    //printk("[SCHED] CPU%lld: switching from pid=%d to pid=%d\n", cpuid(), this ? this->pid : -999, next ? next->pid : -999);
    //printk("[SCHED] idle=%d, this->state=%d, next->state=%d, cpuid=%lld\n", this->idle, this->state, next->state, cpuid());
    ASSERT(next->state == RUNNABLE);
    next->state = RUNNING;
    if (next != this) {
        //extern void trap_return(u64);
        // CPU%lld: switch to pid=%d; attach pgdir\n", cpuid(), next->pid);
        //printk("[SCHED] CPU%lld: next kctx lr=%p x0(entry)=%p trap_return=%p\n",
        //       cpuid(), (void*)next->kcontext->lr, (void*)next->kcontext->x0, (void*)trap_return);
        attach_pgdir(&next->pgdir);
        //if (next->ucontext) {
            //pt_check_user_exec_violation(&next->pgdir, next->ucontext->elr);
            // 解析 ELR 所在页的物理地址 (PTE -> PA)
            //PTEntriesPtr ptep = get_pte(&next->pgdir, next->ucontext->elr, false);
            //u64 phys_addr = 0;
            //if (ptep && (*ptep & PTE_VALID)) {
            //    phys_addr = PTE_ADDRESS(*ptep) | VA_OFFSET(next->ucontext->elr);
            //}
            //printk("[SCHED] CPU%lld: next process va=0x%llx pa=0x%llx pte=%p flags=0x%llx\n",
            //       cpuid(), next->ucontext->elr, phys_addr, ptep, ptep ? PTE_FLAGS(*ptep) : 0ULL);
        //}
        swtch(next->kcontext, &this->kcontext);
    }
    release_sched_lock();
}

u64 proc_entry(void (*entry)(u64), u64 arg) {
    release_sched_lock();
    set_return_addr(entry);
    return arg;
}