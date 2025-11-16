#include <kernel/cpu.h>
#include <kernel/printk.h>
#include <kernel/sched.h>
#include <kernel/proc.h>
#include <aarch64/mmu.h>
#include <driver/timer.h>
#include <driver/clock.h>

struct cpu cpus[NCPU];

static bool __timer_cmp(rb_node lnode, rb_node rnode)
{
    i64 d = container_of(lnode, struct timer, _node)->_key -
            container_of(rnode, struct timer, _node)->_key;
    if (d < 0)
        return true;
    if (d == 0)
        return lnode < rnode;
    return false;
}

static void __timer_set_clock()
{
    auto node = _rb_first(&cpus[cpuid()].timer);
    if (!node) {
        // printk("cpu %lld set clock 1000, no timer left\n", cpuid());
        reset_clock(1000);
        return;
    }
    auto t1 = container_of(node, struct timer, _node)->_key;
    auto t0 = get_timestamp_ms();
    if (t1 <= t0)
        reset_clock(0);
    else
        reset_clock(t1 - t0);
    // printk("cpu %lld set clock %lld\n", cpuid(), t1 - t0);
}

static void timer_clock_handler()
{
    reset_clock(1000);
    // printk("cpu %lld aha, timestamp ms: %lld\n", cpuid(), get_timestamp_ms());
    while (1) {
        auto node = _rb_first(&cpus[cpuid()].timer);
        if (!node)
            break;
        auto timer = container_of(node, struct timer, _node);
        if (get_timestamp_ms() < timer->_key)
            break;
        cancel_cpu_timer(timer);
        timer->triggered = true;
        timer->handler(timer);
    }
}

void init_clock_handler()
{
    set_clock_handler(&timer_clock_handler);
}

static struct timer hello_timer[4];

static void hello(struct timer *t)
{
    printk("[CPU] CPU %lld: living\n", cpuid());
    t->data++;
    set_cpu_timer(&hello_timer[cpuid()]);
}

void set_cpu_timer(struct timer *timer)
{
    timer->triggered = false;
    timer->_key = get_timestamp_ms() + timer->elapse;
    ASSERT(0 == _rb_insert(&timer->_node, &cpus[cpuid()].timer, __timer_cmp));
    __timer_set_clock();
}

void cancel_cpu_timer(struct timer *timer)
{
    ASSERT(!timer->triggered);
    _rb_erase(&timer->_node, &cpus[cpuid()].timer);
    __timer_set_clock();
}

void set_cpu_on()
{
    ASSERT(!_arch_disable_trap());
    // disable the lower-half address to prevent stupid errors
    extern PTEntries invalid_pt;
    arch_set_ttbr0(K2P(&invalid_pt));
    extern char exception_vector[];
    arch_set_vbar(exception_vector);
    //printk("[CPU]CPU %lld: VBAR set to %p\n", cpuid(), exception_vector);
    arch_reset_esr();
    init_clock();
    cpus[cpuid()].online = true;
    printk("[CPU] CPU %lld: hello\n", cpuid());
    hello_timer[cpuid()].elapse = 5000;
    hello_timer[cpuid()].handler = hello;
    set_cpu_timer(&hello_timer[cpuid()]);
}

void set_cpu_off()
{
    _arch_disable_trap();
    cpus[cpuid()].online = false;
    printk("[CPU] CPU %lld: stopped\n", cpuid());
}
