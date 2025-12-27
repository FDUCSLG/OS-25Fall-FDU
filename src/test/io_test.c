#include <common/buf.h>
#include <kernel/printk.h>
#include <driver/virtio.h>
#include <common/string.h>
#include <aarch64/intrinsic.h>

void io_test()
{
    static Buf buffer[1 << 11];
    int num_blocks = sizeof(buffer) / sizeof(buffer[0]);
    int megabytes = (num_blocks * BSIZE) >> 20;
    if (megabytes == 0)
        PANIC();

    i64 frequency, timestamp;
    asm volatile("mrs %[freq], cntfrq_el0" : [freq] "=r"(frequency));

    printk("\e[0;32m[Test] Starting disk test. \e[0m\n");

    printk("\e[0;32m[Test] Checking data correctness...\e[0m\n");
    (void)timestamp;
    for (int i = 1; i < num_blocks; i++) {
        // Backup current block.
        buffer[0].flags = 0;
        buffer[0].block_no = (u32)i;
        virtio_blk_rw(&buffer[0]);
        
        // Write a pattern to block `i`.
        buffer[i].flags = B_DIRTY;
        buffer[i].block_no = (u32)i;
        for (int j = 0; j < BSIZE; j++) {
            buffer[i].data[j] = (u8)((i * j) & 0xFF);
        }
        virtio_blk_rw(&buffer[i]);

        // Clear data and verify pattern
        memset(buffer[i].data, 0, sizeof(buffer[i].data));
        buffer[i].flags = 0;
        virtio_blk_rw(&buffer[i]);

        for (int j = 0; j < BSIZE; j++) {
            if (buffer[i].data[j] != ((i * j) & 0xFF)) {
                PANIC();  // Trigger panic on data mismatch
            }
        }

        // Restore previous block data
        buffer[0].flags = B_DIRTY;
        virtio_blk_rw(&buffer[0]);
    }
    printk("\e[0;32m[Test] Data correctness verified. \e[0m\n");

    printk("\e[0;32m[Test] Measuring read speed... \e[0m\n");
    arch_dsb_sy();
    timestamp = (i64)get_timestamp();
    arch_dsb_sy();

    for (int i = 0; i < num_blocks; i++) {
        buffer[i].flags = 0;
        buffer[i].block_no = (u32)i;
        virtio_blk_rw(&buffer[i]);
    }

    arch_dsb_sy();
    timestamp = (i64)get_timestamp() - timestamp;
    arch_dsb_sy();

    printk("\e[0;32m[Test] Read %dB (%dMB), time: %lld cycles, speed: %lld.%lld MB/s\e[0m\n",
           num_blocks * BSIZE, megabytes, timestamp,
           megabytes * frequency / timestamp, (megabytes * frequency * 10 / timestamp) % 10);

    printk("\e[0;32m[Test] Measuring write speed... \e[0m\n");
    arch_dsb_sy();
    timestamp = (i64)get_timestamp();
    arch_dsb_sy();

    for (int i = 0; i < num_blocks; i++) {
        buffer[i].flags = B_DIRTY;
        buffer[i].block_no = (u32)i;
        virtio_blk_rw(&buffer[i]);
    }

    arch_dsb_sy();
    timestamp = (i64)get_timestamp() - timestamp;
    arch_dsb_sy();

    printk("\e[0;32m[Test] Write %dB (%dMB), time: %lld cycles, speed: %lld.%lld MB/s\e[0m\n",
           num_blocks * BSIZE, megabytes, timestamp,
           megabytes * frequency / timestamp, (megabytes * frequency * 10 / timestamp) % 10);

    printk("\e[0;32m[Test] io_test PASS\e[0m\n");
}
