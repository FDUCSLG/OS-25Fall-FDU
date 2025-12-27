#include <kernel/proc.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <kernel/pt.h>
#include <aarch64/mmu.h>
#include <common/list.h>
#include <common/string.h>
#include <kernel/printk.h>

Proc root_proc;

void kernel_entry();
void proc_entry();

static SpinLock proclock;

// PID bitmap allocator
static unsigned int pid_bitmap[(MAX_PID + 31) / 32];

// Internal helper: free pid without taking proclock. Caller must hold proclock.
static void pid_free_unlocked(int pid)
{
    if (pid < 0 || pid >= MAX_PID) return;
    int w = pid / 32;
    int b = pid % 32;
    pid_bitmap[w] &= ~(1u << b);
}

void pid_init()
{
    for (int i = 0; i < (MAX_PID + 31) / 32; i++)
        pid_bitmap[i] = 0;
}

int pid_alloc()
{
    // use proclock to protect pid allocation as requested
    acquire_spinlock(&proclock);
    for (int w = 0; w < (MAX_PID + 31) / 32; w++) {
        unsigned int word = pid_bitmap[w];
        if (~word) { // has zero bit
            for (int b = 0; b < 32; b++) {
                int bit = w * 32 + b;
                if (bit >= MAX_PID) break;
                if (!(word & (1u << b))) {
                    pid_bitmap[w] |= (1u << b);
                    release_spinlock(&proclock);
                    return bit;
                }
            }
        }
    }
    release_spinlock(&proclock);
    return -1; // no pid available
}

void pid_free(int pid)
{
    if (pid < 0 || pid >= MAX_PID) return;
    acquire_spinlock(&proclock);
    pid_free_unlocked(pid);
    release_spinlock(&proclock);
}

// init_kproc initializes the kernel process
// NOTE: should call after kinit
void init_kproc()
{
    // TODO:
    // 1. init global resources (e.g. locks, semaphores)
    // 2. init the root_proc (finished)
    init_spinlock(&proclock);
    pid_init();
    init_proc(&root_proc);
    root_proc.parent = &root_proc;
    start_proc(&root_proc, kernel_entry, 123456);
    printk("[KERNEL] kernel_entry started on CPU%lld, kernel_entry is %p\n", cpuid(), &kernel_entry);
    printk("[KERNEL][KPROC] root_proc=%p, pid=%d, &root_proc.childexit=%p ,runnable = %d\n", &root_proc, root_proc.pid, &root_proc.childexit, root_proc.state == RUNNABLE);
}

void init_proc(Proc *p)
{
    // TODO:
    // setup the Proc with kstack and pid allocated
    // NOTE: be careful of concurrency
    // NOTE: be careful of concurrency
    
    memset(p, 0, sizeof(*p));
    // allocate pid
    int id = pid_alloc();
    if (id < 0) {
        PANIC();
    }
    p->pid = id;
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
    init_pgdir(&p->pgdir);  // lab3_new_added
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
    //printk("[WAIT] CPU%lld pid=%d waiting for child exit...\n", cpuid(), thisproc()->pid);
    // 2. wait for childexit
    release_spinlock(&proclock);
    if(!wait_sem(&thisproc()->childexit)) { return -1; }
    //printk("[WAIT] CPU%lld pid=%d woke up from child exit wait.\n", cpuid(), thisproc()->pid);
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
        //printk("[WAIT] CPU%lld pid=%d checking child pid=%d state=%d\n", cpuid(), thisproc()->pid, childproc->pid, childproc->state);
    }
    if(zombie) {
        *exitcode = zombie->exitcode;
        pid_zombie = zombie->pid; 
        release_sched_lock();
        acquire_sched_lock();
        _detach_from_list(&zombie->ptnode);
        _detach_from_list(&zombie->schinfo.rqnode);
        kfree_page(zombie->kstack);
        // proclock is already held here; avoid re-entrance deadlock
        pid_free_unlocked(zombie->pid);
        kfree(zombie);
        //printk("[WAIT] CPU%lld pid=%d cleaned up zombie pid=%d\n", cpuid(), thisproc()->pid, pid_zombie);
    }
    release_sched_lock();
    release_spinlock(&proclock);
    //printk("[WAIT] CPU%lld pid=%d returning from wait with pid=%d\n", cpuid(), thisproc()->pid, pid_zombie);
    return pid_zombie;
    // NOTE: be careful of concurrency
}

NO_RETURN void exit(int code)
{
    //printk("[EXIT] CPU%lld pid=%d exit(%d), parent=%p parent_pid=%d\n", 
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
        //printk("[EXIT] CPU%lld pid=%d has children, transferring to root_proc.\n", cpuid(), thisproc()->pid);
        _merge_list(&root_proc.children, thisproc()->children.next);
        _detach_from_list(&thisproc()->children);
    }

    // 4. sched(ZOMBIE)
    //printk("[EXIT] CPU%lld pid=%d exit(%d) completed.\n", cpuid(), thisproc()->pid, code);
    release_sched_lock();
    release_spinlock(&proclock);
    acquire_spinlock(&proclock);
    acquire_sched_lock();
    free_pgdir(&thisproc()->pgdir);
    release_sched_lock();
    post_sem(&thisproc()->parent->childexit);
    acquire_sched_lock();
    release_spinlock(&proclock);
    sched(ZOMBIE);
    // NOTE: be careful of concurrency
    PANIC(); // prevent the warning of 'no_return function returns'
}

Proc* find_proc(int pid, Proc* current_proc) {
    if (current_proc->pid == pid && !is_unused(current_proc)) { return current_proc; }
    _for_in_list(p, &current_proc->children){
        if (p == &current_proc->children) continue;
        Proc* child_proc = container_of(p, Proc, ptnode);
        Proc* res_proc = find_proc(pid, child_proc);
        if(res_proc) return res_proc;
    }
    return NULL;
}

int kill(int pid)
{
    acquire_spinlock(&proclock);
    Proc* kill_proc = find_proc(pid,&root_proc);
    release_spinlock(&proclock);
    // Set the killed flag of the proc to true and return 0.
    if (kill_proc) {
        acquire_spinlock(&proclock);
        kill_proc->killed = true;
        alert_proc(kill_proc);
        release_spinlock(&proclock);
        return 0;
    }
    // Return -1 if the pid is invalid (proc not found).
    return -1;
}