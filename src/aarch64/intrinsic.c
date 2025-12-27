#include <aarch64/intrinsic.h>

void delay_us(u64 n)
{
    u64 freq = get_clock_frequency();
    u64 end = get_timestamp(), now;
    end += freq / 1000000 * n;

    do {
        now = get_timestamp();
    } while (now <= end);
}

void smp_init()
{
    psci_cpu_on(1, SECONDARY_CORE_ENTRY);
    psci_cpu_on(2, SECONDARY_CORE_ENTRY);
    psci_cpu_on(3, SECONDARY_CORE_ENTRY);
}
