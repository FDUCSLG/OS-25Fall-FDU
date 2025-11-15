#include <aarch64/mmu.h>
#include <common/rc.h>
#include <common/spinlock.h>
#include <driver/memlayout.h>
#include <kernel/mem.h>
#include <common/list.h>
#include <common/string.h>
#include <kernel/printk.h>

struct page {
    RefCount ref;
    SpinLock ref_lock;
};

#define NULL 0
extern char end[];
#define PAGE_INDEX(page_base) ((KSPACE((u64)page_base) - P2K(EXTMEM)) / PAGE_SIZE)
#define phy_start PAGE_BASE((u64)&end)
#define phy_end P2K(PHYSTOP)
#define PAGES_REF_SIZE (sizeof(struct page))*(PHYSTOP/PAGE_SIZE)

RefCount kalloc_page_cnt;
RefCount _left_page_cnt;
void* zero_page_ptr;
SpinLock kernel_mem_lock;
QueueNode* nodes;
struct page* pages_ref;
static QueueNode* bin[256];


void init_page() {
    for(u64 p = PAGE_BASE(end); p < P2K(PHYSTOP)-PAGES_REF_SIZE; p = p + 4096) {
        add_to_queue(&nodes, (void*)p);
        increment_rc(&_left_page_cnt);
    }
}

void kinit() {
    init_rc(&kalloc_page_cnt);
    init_rc(&_left_page_cnt);
    init_spinlock(&kernel_mem_lock);
    zero_page_ptr = NULL;
    pages_ref = (struct page*)(P2K(PHYSTOP)-PAGES_REF_SIZE);
    init_page();
    zero_page_ptr = kalloc_page();
    memset(zero_page_ptr, 0, PAGE_SIZE);
    u64 page_num = ((u64)K2P(zero_page_ptr))/PAGE_SIZE;
    pages_ref[page_num].ref.count = 1;
    printk("[MEM] kernel memory initialized, page_cnt = %lld, to_alloct = %lld\n", kalloc_page_cnt.count, _left_page_cnt.count);
}

u64 left_page_cnt() {
    return _left_page_cnt.count;
}

void* kalloc_page() {
    increment_rc(&kalloc_page_cnt);
    decrement_rc(&_left_page_cnt);
    void* ret = fetch_from_queue(&nodes);
    u64 page_num = ((u64)K2P(ret))/PAGE_SIZE;
    pages_ref[page_num].ref.count = 0;
    init_spinlock(&(pages_ref[page_num].ref_lock));
    return ret;
}

void kfree_page(void* p) {
    if (!p) return;
    decrement_rc(&kalloc_page_cnt);
    u64 page_num = ((u64)K2P(p))/PAGE_SIZE;
    acquire_spinlock(&(pages_ref[page_num].ref_lock));
    decrement_rc(&(pages_ref[page_num].ref));
    if(pages_ref[page_num].ref.count <= 0){
        add_to_queue(&nodes, (QueueNode*)p);
        increment_rc(&_left_page_cnt);
    }
    release_spinlock(&(pages_ref[page_num].ref_lock));
    return;
}

void* kalloc(unsigned long long size) {
    acquire_spinlock(&kernel_mem_lock);
    size = size + 8;
    int number;
    if(size % 16 == 0) number = size / 16;
    else number = size / 16 + 1;
    if(number == 256) {
        void* node = kalloc_page();
        *(int*) node = 256;
        node = node + 8;
        release_spinlock(&kernel_mem_lock);
        return node;
    }
    for(int i = number - 1; i < 256 ; i++) {
        if(bin[i] != 0) {
            void* node = (void*)fetch_from_queue(&bin[i]);
            void* freeblock = node + number * 16;
            int freenumber = i + 1 - number;
            if(freenumber != 0) add_to_queue(&bin[freenumber - 1], (QueueNode*)freeblock);
            *(int*) node = number - 1;
            node = node + 8;
            release_spinlock(&kernel_mem_lock);
            return node;
        }
    }
    void* node = kalloc_page();
    void* freeblock = node + number * 16;
    int freenumber = 256 - number;
    add_to_queue(&bin[freenumber - 1], (QueueNode*)freeblock);
    *(int*) node = number - 1;
    node = node + 8;
    release_spinlock(&kernel_mem_lock);
    return node;
}

void kfree(void* ptr) {
    ptr = ptr - 8;
    int* address = ptr;
    add_to_queue(&bin[*address], ptr);
    return;
}
