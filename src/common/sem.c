#include <common/sem.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <kernel/printk.h>
#include <common/list.h>

void init_sem(Semaphore *sem, int val)
{
    sem->val = val;
    init_spinlock(&sem->lock);
    init_list_node(&sem->sleeplist);
}

void _lock_sem(Semaphore *sem)
{
    acquire_spinlock(&sem->lock);
}

void _unlock_sem(Semaphore *sem)
{
    release_spinlock(&sem->lock);
}

bool _get_sem(Semaphore *sem)
{
    bool ret = false;
    if (sem->val > 0) {
        sem->val--;
        ret = true;
    }
    return ret;
}

int _query_sem(Semaphore *sem)
{
    return sem->val;
}

int get_all_sem(Semaphore *sem)
{
    int ret = 0;
    _lock_sem(sem);
    if (sem->val > 0) {
        ret = sem->val;
        sem->val = 0;
    }
    _unlock_sem(sem);
    return ret;
}

int post_all_sem(Semaphore *sem)
{
    int ret = -1;
    _lock_sem(sem);
    do
        _post_sem(sem), ret++;
    while (!_get_sem(sem));
    _unlock_sem(sem);
    return ret;
}

bool _wait_sem(Semaphore *sem, bool alertable)
{
    if (--sem->val >= 0) {
        release_spinlock(&sem->lock);
        return true;
    }
    WaitData *wait = kalloc(sizeof(WaitData));
    wait->proc = thisproc();
    wait->up = false;
    _insert_into_list(&sem->sleeplist, &wait->slnode);
    acquire_sched_lock();
    release_spinlock(&sem->lock);
    sched(alertable ? SLEEPING : DEEPSLEEPING);
    acquire_spinlock(&sem->lock); // also the lock for waitdata
    if (!wait->up) // wakeup by other sources
    {
        ASSERT(++sem->val <= 0);
        _detach_from_list(&wait->slnode);
    }
    release_spinlock(&sem->lock);
    bool ret = wait->up;
    kfree(wait);
    return ret;
}

void _post_sem(Semaphore *sem)
{
    if (++sem->val <= 0) {
        ASSERT(!_empty_list(&sem->sleeplist));
        auto wait = container_of(sem->sleeplist.prev, WaitData, slnode);
        wait->up = true;
        _detach_from_list(&wait->slnode);
        activate_proc(wait->proc);
    }
}