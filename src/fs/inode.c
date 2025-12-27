#include <common/string.h>
#include <fs/inode.h>
#include <kernel/mem.h>
#include <kernel/printk.h>

/**
    @brief the private reference to the super block.

    @note we need these two variables because we allow the caller to
            specify the block cache and super block to use.
            Correspondingly, you should NEVER use global instance of
            them.

    @see init_inodes
 */
static const SuperBlock* sblock;

/**
    @brief the reference to the underlying block cache.
 */
static const BlockCache* cache;

/**
    @brief global lock for inode layer.

    Use it to protect anything you need.

    e.g. the list of allocated blocks, ref counts, etc.
 */
static SpinLock lock;

/**
    @brief the list of all allocated in-memory inodes.

    We use a linked list to manage all allocated inodes.

    You can implement your own data structure if you want better performance.

    @see Inode
 */
static ListNode head;


// return which block `inode_no` lives on.
static INLINE usize to_block_no(usize inode_no) {
    return sblock->inode_start + (inode_no / (INODE_PER_BLOCK));
}

// return the pointer to on-disk inode.
static INLINE InodeEntry* get_entry(Block* block, usize inode_no) {
    return ((InodeEntry*)block->data) + (inode_no % INODE_PER_BLOCK);
}

// return address array in indirect block.
static INLINE u32* get_addrs(Block* block) {
    return ((IndirectBlock*)block->data)->addrs;
}

// initialize inode tree.
void init_inodes(const SuperBlock* _sblock, const BlockCache* _cache) {
    init_spinlock(&lock);
    init_list_node(&head);
    sblock = _sblock;
    cache = _cache;

    if (ROOT_INODE_NO < sblock->num_inodes)
        inodes.root = inodes.get(ROOT_INODE_NO);
    else
        printk("(warn) init_inodes: no root inode.\n");
}

// initialize in-memory inode.
static void init_inode(Inode* inode) {
    init_sleeplock(&inode->lock);
    init_rc(&inode->rc);
    init_list_node(&inode->node);
    inode->inode_no = 0;
    inode->valid = false;
}

// see `inode.h`.
static usize inode_alloc(OpContext* ctx, InodeType type) {
    //printk("[INODE] Allocating inode of type %d\n", type);
    ASSERT(type != INODE_INVALID);
    acquire_spinlock(&lock);
    for (usize i = 1; i < sblock->num_inodes; i++) {
        // search for a free inode
        Block* block = cache->acquire(to_block_no(i));
        InodeEntry* entry = get_entry(block, i);
        if (entry->type == INODE_INVALID) {
            // found a free inode
            memset(entry, 0, sizeof(InodeEntry));
            entry->type = type;
            cache->sync(ctx, block);
            cache->release(block);
            release_spinlock(&lock);
            return i;
        }
        cache->release(block);
    }
    release_spinlock(&lock);
    PANIC();
    return 0;
}

// see `inode.h`.
static void inode_lock(Inode* inode) {
    if(inode == NULL) {
        PANIC();
    }
    //printk("[INODE] Locking inode no %llu\n", inode->inode_no);
    //printk("[INODE] rc count: %d\n", inode->rc.count);
    ASSERT(inode->rc.count > 0);
    //printk("[INODE] Acquiring sleeplock for inode no %llu\n", inode->inode_no);
    unalertable_wait_sem(&inode->lock);
    //printk("[INODE] Sleeplock acquired for inode no %llu\n", inode->inode_no);
}

// see `inode.h`.
static void inode_unlock(Inode* inode) {
    if(inode == NULL) {
        PANIC();
    }
    //printk("[INODE] Unlocking inode no %llu\n", inode->inode_no);
    ASSERT(inode->rc.count > 0);
    // TODO
    post_sem(&inode->lock);
    //printk("[INODE] Sleeplock released for inode no %llu\n", inode->inode_no);
}

// see `inode.h`.
static void inode_sync(OpContext* ctx, Inode* inode, bool do_write) {
    // TODO
    //printk("[INODE] Syncing inode no %llu, do_write: %d\n", inode->inode_no, do_write);
    if(!inode->valid && do_write) {
        PANIC();
    } else if (!inode->valid && !do_write) {
        // Load inode from disk
        Block* block = cache->acquire(to_block_no(inode->inode_no));
        InodeEntry* entry = get_entry(block, inode->inode_no);
        memcpy(&inode->entry, entry, sizeof(InodeEntry));
        cache->release(block);
        inode->valid = true; // Mark as valid after loading
    } else if (inode->valid && do_write) {
        Block* block = cache->acquire(to_block_no(inode->inode_no));
        InodeEntry* entry = get_entry(block, inode->inode_no);
        memcpy(entry, &inode->entry, sizeof(InodeEntry));
        cache->sync(ctx, block);
        cache->release(block);
    } else if (inode->valid && !do_write) {
        Block* block = cache->acquire(to_block_no(inode->inode_no));
        InodeEntry* entry = get_entry(block, inode->inode_no);
        memcpy(&inode->entry, entry, sizeof(InodeEntry));
        cache->release(block);
    }
}

// see `inode.h`.
static Inode* inode_get(usize inode_no) {
    //printk("[INODE] Getting inode no %llu\n", inode_no);
    ASSERT(inode_no > 0);
    //printk("[INODE] Total inodes: %llu\n", sblock->num_inodes);
    ASSERT(inode_no < sblock->num_inodes);
    //printk("[INODE] Acquiring global inode lock\n");
    acquire_spinlock(&lock);
    _for_in_list(nd, &head) {
        Inode* inode = container_of(nd, Inode, node);
        if (inode->inode_no == inode_no) {
            // found in-memory inode
            increment_rc(&inode->rc);
            release_spinlock(&lock);
            return inode;
        }
    }
    release_spinlock(&lock);
    // not found, create a new one
    Inode* new_node = kalloc(sizeof(Inode));
    init_inode(new_node);
    new_node->inode_no = inode_no;
    increment_rc(&new_node->rc);
    //printk("[INODE] Loading inode no %llu from disk\n", inode_no);
    //printk("[INODE] rc count: %d\n", new_node->rc.count);
    inode_lock(new_node);
    inode_sync(NULL, new_node, false);
    inode_unlock(new_node);
    acquire_spinlock(&lock);
    //printk("[INODE] Inode no %llu loaded from disk\n", inode_no);
    _insert_into_list(&head, &new_node->node);
    //printk("[INODE] Inserting inode no %llu into global inode list\n", new_node->inode_no);
    release_spinlock(&lock);
    return new_node;
}
// see `inode.h`.
static void inode_clear(OpContext* ctx, Inode* inode) {
    //printk("[INODE] Clearing inode no %llu\n", inode->inode_no);
    //acquire_spinlock(&lock);
    // 清空 inode 的内容（使文件变成长度为 0 的空文件)
    // inode is not a leave
    if (inode->entry.indirect){
        Block* indirect_block = cache->acquire(inode->entry.indirect);
        u32* addrs = get_addrs(indirect_block);
        for (usize i = 0; i < INODE_NUM_INDIRECT; i++) {
            if (addrs[i]) {
                cache->free(ctx, addrs[i]);
            }
        }
        cache->sync(ctx, indirect_block);
        cache->release(indirect_block);
        cache->free(ctx, inode->entry.indirect);
        inode->entry.indirect = 0;
    }
    // direct blocks
    for (usize i = 0; i < INODE_NUM_DIRECT; i++) {
        if (inode->entry.addrs[i]) {
            cache->free(ctx, inode->entry.addrs[i]);
            inode->entry.addrs[i] = 0;
        }
    }
    inode->entry.num_bytes = 0;
    inode_sync(ctx, inode, true);
    //release_spinlock(&lock);
}

// see `inode.h`.
static Inode* inode_share(Inode* inode) {
    //printk("[INODE] Sharing inode no %llu\n", inode->inode_no);
    // TODO
    increment_rc(&inode->rc);
    return inode;
}

// see `inode.h`.
static void inode_put(OpContext* ctx, Inode* inode) {
    // TODO
    //printk("[INODE] Putting inode no %llu\n", inode->inode_no);
    acquire_spinlock(&lock);
    decrement_rc(&inode->rc);
    //printk("[INODE] rc count after decrement: %d\n", inode->rc.count);
    // if no one needs the inode any more
    if(inode->rc.count == 0) {
        // Remove from list immediately so no one else gets it
        _detach_from_list(&inode->node);
        release_spinlock(&lock); // Release before IO

        if (inode->entry.num_links == 0) {
            inode->entry.type = INODE_INVALID;
            inode_clear(ctx, inode);
        }
        kfree(inode);
    } else {
        release_spinlock(&lock);
    }
}

/**
    @brief get which block is the offset of the inode in.

    e.g. `inode_map(ctx, my_inode, 1234, &modified)` will return the block_no
    of the block that contains the 1234th byte of the file
    represented by `my_inode`.

    If a block has not been allocated for that byte, `inode_map` will
    allocate a new block and update `my_inode`, at which time, `modified`
    will be set to true.

    HOWEVER, if `ctx == NULL`, `inode_map` will NOT try to allocate any new block,
    and when it finds that the block has not been allocated, it will return 0.
    
    @param[out] modified true if some new block is allocated and `inode`
    has been changed.

    @return usize the block number of that block, or 0 if `ctx == NULL` and
    the required block has not been allocated.

    @note the caller must hold the lock of `inode`.
 */
static usize inode_map(OpContext* ctx,
                       Inode* inode,
                       usize offset,
                       bool* modified) {
    //direct blocks
    //printk("[INODE] Mapping offset %llu for inode no %llu\n", offset, inode->inode_no);
    *modified = false;
    if (offset < INODE_NUM_DIRECT){
        if (inode->entry.addrs[offset] == 0){
            if (ctx == NULL){
                return 0;
            }
            inode->entry.addrs[offset] = cache->alloc(ctx);
            *modified = true;
        }
        return inode->entry.addrs[offset];
    }
    //indirect blocks
    offset -= INODE_NUM_DIRECT;
    if (inode->entry.indirect == 0){
        if (ctx == NULL) return 0;
        inode->entry.indirect = cache->alloc(ctx);
        *modified = true;
        // Initialize new indirect block with zeros
        Block* new_ind = cache->acquire(inode->entry.indirect);
        memset(new_ind->data, 0, BLOCK_SIZE);
        cache->sync(ctx, new_ind);
        cache->release(new_ind);
    }
    
    Block* indirect_block = cache->acquire(inode->entry.indirect);
    u32* addrs = get_addrs(indirect_block);
    
    if (addrs[offset] == 0){
        if (ctx == NULL){
            cache->release(indirect_block);
            return 0;
        }
        addrs[offset] = cache->alloc(ctx);
        // Sync the indirect block because we updated an entry in it
        cache->sync(ctx, indirect_block);
    }
    usize ret = addrs[offset];
    cache->release(indirect_block);
    return ret;
}

// see `inode.h`.
static usize inode_read(Inode* inode, u8* dest, usize offset, usize count) {
    //printk("[INODE] Reading %llu bytes from offset %llu of inode no %llu\n", count, offset, inode->inode_no);
    InodeEntry* entry = &inode->entry;
    if (count + offset > entry->num_bytes)
        count = entry->num_bytes - offset;
    usize end = offset + count;
    //printk("[INODE] [ASSERT] offset: %llu, end: %llu, entry->num_bytes: %llu\n", offset, end, entry->num_bytes);
    ASSERT(offset <= entry->num_bytes);
    //printk("[INODE] [ASSERT PASSED] offset: %llu, end: %llu, entry->num_bytes: %llu\n", offset, end, entry->num_bytes);
    ASSERT(end <= entry->num_bytes);
    //printk("[INODE] [ASSERT PASSED] offset: %llu, end: %llu, entry->num_bytes: %llu\n", offset, end, entry->num_bytes);
    ASSERT(offset <= end);
    bool modified = false;
    for (usize readed_block = 0, size = 0; readed_block < count; readed_block += size) {
        Block* block = NULL;
        usize block_no = inode_map(NULL, inode, offset / BLOCK_SIZE, &modified);
        block = cache->acquire(block_no);
        if (count - readed_block < BLOCK_SIZE - (offset % BLOCK_SIZE))
            size = count - readed_block;
        else
            size = BLOCK_SIZE - (offset % BLOCK_SIZE);
        memmove(dest, block->data + (offset % BLOCK_SIZE), size);
        cache->release(block);
        dest += size;
        offset += size;
    }
    return count;
}

// see `inode.h`.
static usize inode_write(OpContext* ctx,
                         Inode* inode,
                         u8* src,
                         usize offset,
                         usize count) {
    //printk("[INODE] Writing %llu bytes to offset %llu of inode no %llu\n", count, offset, inode->inode_no);
    InodeEntry* entry = &inode->entry;
    usize end = offset + count;
    ASSERT(offset <= entry->num_bytes);
    ASSERT(end <= INODE_MAX_BYTES);
    ASSERT(offset <= end);
    if (end > entry->num_bytes){
        entry->num_bytes = end;
        inode_sync(ctx, inode, true);
    }
    usize written_byte = 0;
    while (offset < end) {
        usize byte_to_write = MIN(BLOCK_SIZE - (offset % BLOCK_SIZE), end - offset);
        bool modified = false;
        usize block_no = inode_map(ctx, inode, offset / BLOCK_SIZE, &modified);
        if (block_no == 0) {
            PANIC();
        }
        if (modified) {
            inode_sync(ctx, inode, true);
        }
        Block* block = cache->acquire(block_no);
        memcpy(block->data + (offset % BLOCK_SIZE), src, byte_to_write);
        cache->sync(ctx, block);
        cache->release(block);
        offset += byte_to_write;
        src += byte_to_write;
        written_byte += byte_to_write;
    }
    return written_byte;
}

// see `inode.h`.
static usize inode_lookup(Inode* inode, const char* name, usize* index) {
    //printk("[INODE] Looking up name %s in directory inode no %llu\n", name, inode->inode_no);
    InodeEntry* entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);
    DirEntry dir_entry;
    for (usize off = 0; off < entry->num_bytes; off += sizeof(DirEntry)) {
        inode_read(inode, (u8*)&dir_entry, off, sizeof(DirEntry));
        if ((strncmp(dir_entry.name, name, FILE_NAME_MAX_LENGTH) == 0) && dir_entry.inode_no != 0) {
            if (index)
                *index = off;
            return dir_entry.inode_no;
        }
    }
    return 0;
}

// see `inode.h`.
static usize inode_insert(OpContext* ctx,
                          Inode* inode,
                          const char* name,
                          usize inode_no) {
    InodeEntry* entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);
    //printk("[INODE] Inserting name %s with inode no %llu into directory inode no %llu\n", name, inode_no, inode->inode_no);
    if (inode_lookup(inode, name, NULL) != 0) {
        return -1;
    }
    DirEntry dir_entry;
    usize idx = inode->entry.num_bytes;
     // find a free entry
    for (usize i = 0; i < inode->entry.num_bytes; i += sizeof(DirEntry)) {
        inode_read(inode, (u8*)&dir_entry, i, sizeof(DirEntry));
        if (dir_entry.inode_no == 0) {
            idx = i;
            break;
        }
    }
    memset(&dir_entry, 0, sizeof(DirEntry));
    strncpy(dir_entry.name, name, FILE_NAME_MAX_LENGTH);
    dir_entry.inode_no = inode_no;
    inode_write(ctx, inode, (u8*)&dir_entry, idx, sizeof(DirEntry));
    return idx;
}

// see `inode.h`.
static void inode_remove(OpContext* ctx, Inode* inode, usize index) {
    //printk("[INODE] Removing entry at index %llu from directory inode no %llu\n", index, inode->inode_no);
    InodeEntry* entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);
    ASSERT(index < entry->num_bytes);
    DirEntry dir_entry;
    inode_read(inode, (u8*)&dir_entry, index, sizeof(DirEntry));
    if (dir_entry.inode_no == 0) {
        return;
    }
    usize last_index = entry->num_bytes - sizeof(DirEntry);
    inode_read(inode, (u8*)&dir_entry, last_index, sizeof(DirEntry));
    dir_entry.inode_no = 0;
    inode_write(ctx, inode, (u8*)&dir_entry, index, sizeof(DirEntry));   
}

InodeTree inodes = {
    .alloc = inode_alloc,
    .lock = inode_lock,
    .unlock = inode_unlock,
    .sync = inode_sync,
    .get = inode_get,
    .clear = inode_clear,
    .share = inode_share,
    .put = inode_put,
    .read = inode_read,
    .write = inode_write,
    .lookup = inode_lookup,
    .insert = inode_insert,
    .remove = inode_remove,
};