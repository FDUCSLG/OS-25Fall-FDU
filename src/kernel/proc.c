#include <kernel/proc.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <aarch64/mmu.h>
#include <common/list.h>
#include <common/string.h>
#include <kernel/printk.h>

Proc root_proc;

void kernel_entry();
void proc_entry();

static SpinLock proclock; 
static int pid_idx;

// init_kproc initializes the kernel process
// NOTE: should call after kinit
void init_kproc()
{
    // TODO:
    // 1. init global resources (e.g. locks, semaphores)
    // 2. init the root_proc (finished)
    init_spinlock(&proclock);
    init_proc(&root_proc);
    root_proc.parent = &root_proc;
    start_proc(&root_proc, kernel_entry, 123456);
    printk("[KPROC] root_proc=%p, pid=%d, &root_proc.childexit=%p ,runnable = %d\n", &root_proc, root_proc.pid, &root_proc.childexit, root_proc.state == RUNNABLE);
}

void init_proc(Proc *p)
{
    // TODO:
    // setup the Proc with kstack and pid allocated
    // NOTE: be careful of concurrency
    // NOTE: be careful of concurrency
    
    memset(p, 0, sizeof(*p));
    acquire_spinlock(&proclock);
    p->pid = pid_idx++;
    release_spinlock(&proclock);
    p->killed = false;
    p->idle = false;
    p->exitcode = 0;
    p->state = UNUSED;
    p->parent = NULL;
    init_sem(&p->childexit, 0);
    init_list_node(&p->children);
    init_list_node(&p->ptnode);
    p->kstack = kalloc_page();
    memset(p->kstack, 0, PAGE_SIZE);
    init_schinfo(&p->schinfo);
    p->kcontext=(KernelContext*)((u64)p->kstack+PAGE_SIZE-16-sizeof(KernelContext)-sizeof(UserContext));
    p->ucontext=(UserContext*)((u64)p->kstack+PAGE_SIZE-16-sizeof(UserContext));
}

Proc *create_proc()
{
    Proc *p = kalloc(sizeof(Proc));
    init_proc(p);
    return p;
}

void set_parent_to_this(Proc *proc)
{
    // NOTE: maybe you need to lock the process tree
    acquire_spinlock(&proclock);
    // NOTE: it's ensured that the old proc->parent = NULL
    ASSERT(proc->parent == NULL);
    proc->parent = thisproc();
    _insert_into_list(&thisproc()->children, &proc->ptnode);
    release_spinlock(&proclock);
}

int start_proc(Proc *p, void (*entry)(u64), u64 arg)
{
    // TODO:
    acquire_spinlock(&proclock);
    // 1. set the parent to root_proc if NULL
    if(p->parent == NULL) {
        p->parent = &root_proc;
        _insert_into_list(&root_proc.children, &p->ptnode);
    }
    // 2. setup the kcontext to make the proc start with proc_entry(entry, arg)
    p->kcontext->lr = (u64)&proc_entry;
    p->kcontext->x0 = (u64)entry;
    p->kcontext->x1 = (u64)arg;
    // 3. activate the proc and return its pid
    activate_proc(p);
    release_spinlock(&proclock);
    return p->pid;
    // NOTE: be careful of concurrency
}

int wait(int *exitcode)
{
    acquire_spinlock(&proclock);
    if(thisproc()->children.next == &thisproc()->children) { release_spinlock(&proclock); return -1; }
    // 2. wait for childexit
    release_spinlock(&proclock);
    if(!wait_sem(&thisproc()->childexit)) { return -1; }
    // NOTE: be careful of concurrency
    acquire_spinlock(&proclock);
    acquire_sched_lock();
    // 3. if any child exits, clean it up and return its pid and exitcode
    Proc* zombie = NULL;
    int pid_zombie = -1;
    _for_in_list(p, &thisproc()->children) {
        if(p == &thisproc()->children) continue;
        auto childproc = container_of(p, Proc, ptnode);
        if(childproc->state == ZOMBIE) { zombie = childproc; break; }
    }
    if(zombie) {
        *exitcode = zombie->exitcode;
        pid_zombie = zombie->pid; 
        release_sched_lock();
        acquire_sched_lock();
        _detach_from_list(&zombie->ptnode);
        _detach_from_list(&zombie->schinfo.rqnode);
        kfree_page(zombie->kstack);
        kfree(zombie);
    }
    release_sched_lock();
    release_spinlock(&proclock);
    return pid_zombie;
    // NOTE: be careful of concurrency
}

NO_RETURN void exit(int code)
{
    //     printk("[EXIT] CPU%lld pid=%d exit(%d), parent=%p parent_pid=%d\n", 
    //            cpuid(), thisproc()->pid, code, thisproc()->parent, thisproc()->parent ? thisproc()->parent->pid : -999);
    ASSERT(thisproc()!=&root_proc && thisproc()->pid!=-1);
    // NOTE: be careful of concurrency
    acquire_spinlock(&proclock);
    acquire_sched_lock();
    // 1. set the exitcode
    thisproc()->exitcode = code;
    // 2. clean up the resources
    // 3. transfer children to the root_proc, and notify the root_proc if there is zombie
    _for_in_list(p, &thisproc()->children) {
        if(p == &thisproc()->children) continue;
        auto childproc = container_of(p, Proc, ptnode);
        childproc->parent = &root_proc;
        if(childproc->state == ZOMBIE) {
            release_sched_lock();
            post_sem(&root_proc.childexit);
            acquire_sched_lock();
        }
    }
    if(!_empty_list(&thisproc()->children)) {
        _merge_list(&root_proc.children, thisproc()->children.next);
        _detach_from_list(&thisproc()->children);
    }

    // 4. sched(ZOMBIE)
    release_sched_lock();
    release_spinlock(&proclock);
    acquire_spinlock(&proclock);
    acquire_sched_lock();
    release_sched_lock();
    post_sem(&thisproc()->parent->childexit);
    acquire_sched_lock();
    release_spinlock(&proclock);
    sched(ZOMBIE);
    // NOTE: be careful of concurrency

    PANIC(); // prevent the warning of 'no_return function returns'
}
