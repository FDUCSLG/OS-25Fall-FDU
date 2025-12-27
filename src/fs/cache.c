#include <common/bitmap.h>
#include <common/string.h>
#include <fs/cache.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <kernel/proc.h>

/**
    @brief the private reference to the super block.

    @note we need these two variables because we allow the caller to
            specify the block device and super block to use.
            Correspondingly, you should NEVER use global instance of
            them, e.g. `get_super_block`, `block_device`

    @see init_bcache
 */
static const SuperBlock *sblock;

/**
    @brief the reference to the underlying block device.
 */
static const BlockDevice *device; 

/**
    @brief global lock for block cache.

    Use it to protect anything you need.

    e.g. the list of allocated blocks, etc.
 */
static SpinLock lock;

/**
    @brief the list of all allocated in-memory block.

    We use a linked list to manage all allocated cached blocks.

    You can implement your own data structure if you like better performance.

    @see Block
 */
static ListNode head;

static LogHeader header; // in-memory copy of log header block.

/**
    @brief a struct to maintain other logging states.
    
    You may wonder where we store some states, e.g.
    
    * how many atomic operations are running?
    * are we checkpointing?
    * how to notify `end_op` that a checkpoint is done?

    Put them here!

    @see cache_begin_op, cache_end_op, cache_sync
 */
struct {
    int waiting_ops;      // number of operations waiting to begin
    bool committing;      // is the log being committed?
    Semaphore logsem; // semaphore to notify the completion of checkpointing
} log;

// read the content from disk.
static INLINE void device_read(Block *block) {
    device->read(block->block_no, block->data);
}

// write the content back to disk.
static INLINE void device_write(Block *block) {
    device->write(block->block_no, block->data);
}

// read log header from disk.
static INLINE void read_header() {
    device->read(sblock->log_start, (u8 *)&header);
}

// write log header back to disk.
static INLINE void write_header() {
    device->write(sblock->log_start, (u8 *)&header);
}

// initialize a block struct.
static void init_block(Block *block) {
    block->block_no = 0;
    init_list_node(&block->node);
    block->acquired = false;
    block->refcnt = 0;
    block->pinned = false;

    init_sleeplock(&block->lock);
    block->valid = false;
    memset(block->data, 0, sizeof(block->data));
}

static usize cache_nums_blocks;

// see `cache.h`.
static usize get_num_cached_blocks() {
    return cache_nums_blocks;
}

// see `cache.h`.
static Block *cache_acquire(usize block_no) {
    //printk("[BCACHE] Acquiring block no %llu\n", block_no);
    acquire_spinlock(&lock);
    Block *block_to_acquire = NULL;
    Block *block_current = NULL;
    _for_in_list(nd, &head) {
        if (nd == &head) continue;
        block_current = container_of(nd, Block, node);
        if (block_current->block_no == block_no) {
            block_to_acquire = block_current;
            break;
        }

    }
    if (block_to_acquire != NULL) {
        //printk("[BCACHE] Block no %llu found in cache\n", block_no);
        // found in cache
        block_to_acquire->refcnt++;
        block_to_acquire->acquired = true;
        release_spinlock(&lock);
        if (!wait_sem(&block_to_acquire->lock)) {
            //printk("[BCACHE] Error: cannot acquire block no %llu\n", block_no);
            PANIC();
        }
        acquire_spinlock(&lock);
        _detach_from_list(&block_to_acquire->node);
        _insert_into_list(&head, &block_to_acquire->node);
        release_spinlock(&lock);
        //printk("[BCACHE] Acquired block no %llu\n", block_no);
        return block_to_acquire;
    }
    //printk("[BCACHE] Block no %llu not found in cache\n", block_no);
    // not found in cache
    // need to allocate a new block, evict if necessary
    ListNode *p = head.prev;
    while(cache_nums_blocks >= EVICTION_THRESHOLD){
        ListNode* next_p = p->prev;
        if(p == &head){
            break;
        }
        Block* block_current = container_of(p, Block, node);
        if(!block_current->acquired && !block_current->pinned){
            _detach_from_list(p);
            cache_nums_blocks--;
            kfree(block_current);
        }
        p = next_p;
    }
    block_to_acquire = kalloc(sizeof(Block));
    init_block(block_to_acquire);
    if (!wait_sem(&block_to_acquire->lock)) {
        //printk("[BCACHE] Error: cannot acquire newly allocated block\n");
        PANIC();
    }
    block_to_acquire->block_no = block_no;
    block_to_acquire->refcnt = 1;
    block_to_acquire->acquired = true;
    block_to_acquire->valid = true;
    cache_nums_blocks++;
    release_spinlock(&lock);
    device_read(block_to_acquire);
    acquire_spinlock(&lock);
    _insert_into_list(&head, &block_to_acquire->node);
    release_spinlock(&lock);
    //printk("[BCACHE] Acquired block no %llu\n", block_no);
    return block_to_acquire;
}

// see `cache.h`.
static void cache_release(Block *block) {
    acquire_spinlock(&lock);
    if (block->refcnt <= 0) {
        //printk("[BCACHE] Error: releasing a block that is not acquired\n");
        PANIC();
    }
    block->refcnt--;
    if (block->refcnt == 0) {
        block->acquired = false;
    }
    post_sem(&block->lock);
    release_spinlock(&lock);
}

// see `cache.h`.
static void cache_begin_op(OpContext *ctx) {
    //printk("[BCACHE] Beginning atomic operation\n");
    acquire_spinlock(&lock);
    ctx->rm = OP_MAX_NUM_BLOCKS;
    while (log.committing || LOG_MAX_SIZE <= header.num_blocks + (log.waiting_ops + 1)* OP_MAX_NUM_BLOCKS) {
        release_spinlock(&lock);
        if (!wait_sem(&log.logsem)) {
            //printk("[BCACHE] Error: cannot begin atomic operation due to log full\n");
            PANIC();
        }
        acquire_spinlock(&lock);
    }
    log.waiting_ops++;
    release_spinlock(&lock);
}

// see `cache.h`.
static void cache_sync(OpContext *ctx, Block *block) {
    //printk("[BCACHE] Syncing block no %llu\n", block->block_no);
    if(ctx == NULL) {
        device_write(block);
        return;
    }
    acquire_spinlock(&lock);
    block->pinned = true;
    bool already_in_log = false;
    for(usize i = 0; i < header.num_blocks; i++) {
        if(header.block_no[i] == block->block_no) {
            already_in_log = true;
            break;
        }
    }
    if(!already_in_log) {
        header.block_no[header.num_blocks] = block->block_no;
        header.num_blocks++;
        if(ctx->rm <= 0 || header.num_blocks > LOG_MAX_SIZE) {
            //printk("[BCACHE] Error: too many blocks in atomic operation\n");
            PANIC();
        }
        ctx->rm--;
    }
    release_spinlock(&lock);
}

// see `cache.h`.
static void cache_end_op(OpContext *ctx) {
    //printk("[BCACHE] Ending atomic operation\n");
    ctx = ctx;
    acquire_spinlock(&lock);
    log.waiting_ops--;
    if(log.waiting_ops > 0) {
        post_sem(&log.logsem);
        release_spinlock(&lock);
        return;
    }

    log.committing = true;
    release_spinlock(&lock);

    for(usize i = 0; i < header.num_blocks; i++) {
        usize from_block_no = header.block_no[i];
        usize to_block_no = sblock->log_start + 1 + i;
        Block *from_block = cache_acquire(from_block_no);
        Block *to_block = cache_acquire(to_block_no);
        for(int j = 0; j < BLOCK_SIZE; j++) {
            to_block->data[j] = from_block->data[j];
        }
        device_write(to_block);
        cache_release(from_block);
        cache_release(to_block);
    }
    write_header();
    for(usize i = 0; i < header.num_blocks; i++) {
        Block *block = cache_acquire(header.block_no[i]);
        device_write(block);
        acquire_spinlock(&lock);
        block->pinned = false;
        release_spinlock(&lock);
        cache_release(block);
    }
    
    acquire_spinlock(&lock);
    header.num_blocks = 0;
    release_spinlock(&lock);
    
    write_header();
    
    acquire_spinlock(&lock);
    log.committing = false;
    post_all_sem(&log.logsem);
    release_spinlock(&lock);
}

// see `cache.h`.
static usize cache_alloc(OpContext *ctx) {
    //printk("[BCACHE] Allocating block\n");
    Block *bitmap_block = cache_acquire(sblock->bitmap_start);
    for(usize i = 0; i < sblock->num_blocks; i++) {
        if(!bitmap_get((BitmapCell*)bitmap_block->data, i)) {
            bitmap_set((BitmapCell*)bitmap_block->data, i);

            cache_sync(ctx, bitmap_block);
            cache_release(bitmap_block);
            
            Block *new_block = cache_acquire(i);
            memset(new_block->data, 0, BLOCK_SIZE);
            cache_sync(ctx, new_block);
            cache_release(new_block);
            return i;
        }
    }
    cache_release(bitmap_block);
    //printk("[BCACHE] Error: no free block available for allocation\n");
    PANIC();
}

// see `cache.h`.
static void cache_free(OpContext *ctx, usize block_no) {
    //printk("[BCACHE] Freeing block no %llu\n", block_no);
    Block *bitmap_block = cache_acquire(sblock->bitmap_start);
    
    bitmap_clear((BitmapCell*)bitmap_block->data, block_no);

    cache_sync(ctx, bitmap_block);
    cache_release(bitmap_block);
}

// see `cache.h`.
void init_bcache(const SuperBlock *_sblock, const BlockDevice *_device) {
    sblock = _sblock;
    device = _device;
    cache_nums_blocks = 0;
    init_spinlock(&lock);
    
    init_sem(&log.logsem, 0);
    log.waiting_ops = 0;
    log.committing = false;

    init_list_node(&head);
    read_header();
    for (usize i = 0; i < header.num_blocks; i++) {
        usize real_block_no = header.block_no[i];
        usize log_block_no = sblock->log_start + 1 + i;
        Block *real_block = cache_acquire(real_block_no);
        Block *log_block = cache_acquire(log_block_no);
        
        memcpy(real_block->data, log_block->data, BLOCK_SIZE);
        
        device_write(real_block);
        cache_release(real_block);
        cache_release(log_block);
    }
    header.num_blocks = 0;
    memset(header.block_no, 0, LOG_MAX_SIZE);
    write_header();
    // TODO
}

BlockCache bcache = {
    .get_num_cached_blocks = get_num_cached_blocks,
    .acquire = cache_acquire,
    .release = cache_release,
    .begin_op = cache_begin_op,
    .sync = cache_sync,
    .end_op = cache_end_op,
    .alloc = cache_alloc,
    .free = cache_free,
};