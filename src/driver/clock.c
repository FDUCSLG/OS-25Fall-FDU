#include <aarch64/intrinsic.h>
#include <kernel/sched.h>
#include <driver/base.h>
#include <driver/clock.h>
#include <driver/interrupt.h>
#include <kernel/printk.h>
#include <driver/timer.h>

static struct {
    ClockHandler handler;
} clock;

void init_clock()
{
    // reserve one second for the first time.
    enable_timer();
    reset_clock(10);
}

void reset_clock(u64 interval_ms)
{
    u64 interval_clk = interval_ms * get_clock_frequency() / 1000;
    ASSERT(interval_clk <= 0x7fffffff);
    set_cntv_tval_el0(interval_clk);
}

void set_clock_handler(ClockHandler handler)
{
    clock.handler = handler;
    set_interrupt_handler(TIMER_IRQ, invoke_clock_handler);
}

void invoke_clock_handler()
{
    if (!clock.handler)
        PANIC();
    clock.handler();
}

u64 get_timestamp_ms()
{
    return get_timestamp() * 1000 / get_clock_frequency();
}
