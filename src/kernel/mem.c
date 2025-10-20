#include <aarch64/mmu.h>
#include <common/rc.h>
#include <common/spinlock.h>
#include <driver/memlayout.h>
#include <kernel/mem.h>
#include <common/defines.h>
#include <common/list.h>
#include <kernel/printk.h>

RefCount kalloc_page_cnt;

/* 页级空闲页（无锁栈） */
static QueueNode *g_free_pages = NULL;
static u8 *g_page_next  = NULL;
static u8 *g_page_limit = NULL;
static u8 *g_page_start = NULL;  
static SpinLock g_page_lock;

/* 小块参数 */
#define SMALL_ALIGN       8ull
#define MAX_SMALL_ALLOC   (PAGE_SIZE / 2)
#define NUM_SMALL_BINS    ((int)(MAX_SMALL_ALLOC / SMALL_ALIGN))

/* 魔数 */
#define SLAB_PAGE_MAGIC 0x534C4142u
#define PAGE_FREED_MAGIC 0xFEE1DEADCAFEBABEULL
#define SMALL_MAGIC_FREE  0xFEEEFEEEu
#define SMALL_MAGIC_USED  0xA110CA7Eu

/* Slab 页头（驻留页首） */
typedef struct SlabPage {
    struct SlabPage *next;
    struct SlabPage *prev;
    u32 magic;               /* 新增: 页头魔数 */
    u16 bin_index;
    u16 block_bytes;
    u16 capacity;
    u16 inuse;
    u16 first_free_off;      /* 0xFFFF 表示满页 */
    u16 reserved;
} SlabPage;

/* 块头（紧跟用户数据前置） */
typedef struct SmallBlockHeader {
    u16 next_off;            /* 空闲链下一块 offset；已分配=0xFFFF */
    u16 _pad;
    u32 size;                /* 对齐后的 payload 大小 */
    u32 tag;                 /* 魔数 */
    u32 _pad2;               /* 填充，使总大小 16 字节 */
} SmallBlockHeader;

/* 每个 bin 维护部分空闲页的双向链表 */
typedef struct {
    SlabPage *partial;
    u16       block_bytes;
    SpinLock  lock;
} SmallBin;

static SmallBin g_small_bins[NUM_SMALL_BINS];

/* 工具函数 */
static inline u64 _align8(u64 x) { return (x + (SMALL_ALIGN - 1)) & ~(SMALL_ALIGN - 1); }
static inline int _small_bin_index(u64 sz) { return (int)(sz / SMALL_ALIGN) - 1; }
static inline SlabPage* page_meta_from_ptr(void *p) {
    return (SlabPage *)((u64)p & ~(PAGE_SIZE - 1ULL));
}
static inline SmallBlockHeader* block_at(SlabPage *pg, u16 off) {
    return (SmallBlockHeader *)((u8 *)pg + off);
}

/* 双向链操作 */
static inline void bin_insert_head(SmallBin *bin, SlabPage *pg) {
    pg->prev = NULL;
    pg->next = bin->partial;
    if (bin->partial) bin->partial->prev = pg;
    bin->partial = pg;
}
static inline void bin_remove(SmallBin *bin, SlabPage *pg) {
    SlabPage *n = pg->next;
    SlabPage *p = pg->prev;
    if (p) p->next = n; else {
        if (bin->partial == pg) bin->partial = n;
    }
    if (n) n->prev = p;
    pg->next = pg->prev = NULL;
}

/* 预切分整页 */
static int init_new_slab_page(SlabPage *pg, int bin_index, u16 block_bytes) {
    u16 header_region = (u16)_align8(sizeof(SlabPage));
    if (header_region >= PAGE_SIZE) return -1;
    u16 capacity = (u16)((PAGE_SIZE - header_region) / block_bytes);
    if (capacity == 0) return -1;

    pg->magic          = SLAB_PAGE_MAGIC;
    pg->bin_index      = (u16)bin_index;
    pg->block_bytes    = block_bytes;
    pg->capacity       = capacity;
    pg->inuse          = 0;
    pg->first_free_off = header_region;
    pg->next = pg->prev = NULL;

    for (u16 i = 0; i < capacity; i++) {
        u16 off = header_region + i * block_bytes;
        SmallBlockHeader *hdr = block_at(pg, off);
        hdr->size     = 0;
        hdr->tag      = SMALL_MAGIC_FREE;
        hdr->next_off = (i + 1 < capacity)
                        ? (u16)(header_region + (i + 1) * block_bytes)
                        : 0xFFFF;
    }
    return 0;
}

/* 初始化 */
void kinit() {
    init_rc(&kalloc_page_cnt);
    init_spinlock(&g_page_lock);

    extern char end[];
    u64 start = round_up((u64)end, PAGE_SIZE);
    u64 limit = (u64)P2K_WO(PHYSTOP);
    g_page_start = (u8 *)start;  
    g_page_next  = (u8 *)start;
    g_page_limit = (u8 *)limit;

    for (int i = 0; i < NUM_SMALL_BINS; ++i) {
        g_small_bins[i].partial     = NULL;
        g_small_bins[i].block_bytes = 0;
        init_spinlock(&g_small_bins[i].lock);
    }
}

/* 页级接口 */
void* kalloc_page() {
    QueueNode *node = fetch_from_queue(&g_free_pages);
    void *p;
    if (node) {
        p = (void *)node;
    } else {
        acquire_spinlock(&g_page_lock);
        if (g_page_next + PAGE_SIZE > g_page_limit) {
            release_spinlock(&g_page_lock);
            return NULL;
        }
        p = (void *)g_page_next;
        g_page_next += PAGE_SIZE;
        release_spinlock(&g_page_lock);
    }
    if (*(u64 *)p == PAGE_FREED_MAGIC) {
        *(u64 *)p = 0;
    }
    increment_rc(&kalloc_page_cnt);
    return p;
}

void kfree_page(void* p) {
    if (!p) return;
    u64 addr = (u64)p;
    /* 对齐检查 */
    if (addr & (PAGE_SIZE - 1)) {
        printk("kfree_page: unaligned addr=%p\n", p);
        return;
    }
    /* 范围检查 */
    if (addr < (u64)g_page_start || addr >= (u64)g_page_limit) {
        printk("kfree_page: out-of-range addr=%p\n", p);
        return;
    }
    /* 双重释放检测：页首 8 字节是否已为 MAGIC */
    u64 *marker = (u64 *)p;
    if (*marker == PAGE_FREED_MAGIC) {
        printk("kfree_page: double free addr=%p\n", p);
        return;
    }
    *marker = PAGE_FREED_MAGIC;

    add_to_queue(&g_free_pages, (QueueNode *)p);
    decrement_rc(&kalloc_page_cnt);
}

/* 小块分配 */
void* kalloc(unsigned long long size) {
    if (size == 0) return NULL;
    u64 payload = _align8(size);
    if (payload == 0 || payload > MAX_SMALL_ALLOC) return NULL;

    int idx = _small_bin_index(payload);
    if (idx < 0 || idx >= NUM_SMALL_BINS) return NULL;

    SmallBin *bin = &g_small_bins[idx];
    acquire_spinlock(&bin->lock);

    if (bin->block_bytes == 0) {
        u64 blk = _align8(sizeof(SmallBlockHeader) + payload);
        u64 header_region = _align8(sizeof(SlabPage));
        if (blk == 0 || blk > PAGE_SIZE - header_region) {
            release_spinlock(&bin->lock);
            return NULL;
        }
        bin->block_bytes = (u16)blk;
    }

    SlabPage *pg = bin->partial;
    if (!pg) {
        /* 申请新页 */
        release_spinlock(&bin->lock);
        void *new_page = kalloc_page();
        if (!new_page) return NULL;

        pg = (SlabPage *)new_page;
        if (init_new_slab_page(pg, idx, bin->block_bytes) != 0) {
            kfree_page(new_page);
            return NULL;
        }

        acquire_spinlock(&bin->lock);
        bin_insert_head(bin, pg);
    }

    if (pg->first_free_off == 0xFFFF) {
        /* 理论不应出现：链头页却满 */
        bin_remove(bin, pg);
        release_spinlock(&bin->lock);
        return NULL;
    }

    u16 off = pg->first_free_off;
    SmallBlockHeader *hdr = block_at(pg, off);
    pg->first_free_off = hdr->next_off;
    pg->inuse++;

    if (pg->first_free_off == 0xFFFF) { /* 页满 */
        bin_remove(bin, pg);
    }

    release_spinlock(&bin->lock);

    hdr->next_off = 0xFFFF;
    hdr->size     = (u32)payload;
    hdr->tag      = SMALL_MAGIC_USED;

    return (u8 *)hdr + sizeof(SmallBlockHeader);
}

/* 小块释放 + 自动整页回收 */
void kfree(void *ptr) {
    if (!ptr) return;
    SmallBlockHeader *hdr = (SmallBlockHeader *)((u8 *)ptr - sizeof(SmallBlockHeader));
    u32 payload = hdr->size;
    if (payload == 0 || payload > MAX_SMALL_ALLOC || (payload & (SMALL_ALIGN - 1))) {
        printk("kfree: bad payload=%u hdr=%p\n", payload, hdr);
        return;
    }
    int idx = _small_bin_index(payload);
    if (idx < 0 || idx >= NUM_SMALL_BINS) {
        printk("kfree: bad idx=%d hdr=%p\n", idx, hdr);
        return;
    }

    SlabPage *pg = page_meta_from_ptr(hdr);
    SmallBin *bin = &g_small_bins[idx];
    acquire_spinlock(&bin->lock);

    // 校验页头
    if (pg->magic != SLAB_PAGE_MAGIC ||
        pg->bin_index != idx ||
        pg->block_bytes == 0 ||
        pg->inuse == 0) {
        release_spinlock(&bin->lock);
        printk("kfree: meta corrupt hdr=%p pg=%p magic=%x bin=%u inuse=%u\n",
               hdr, pg, pg->magic, pg->bin_index, pg->inuse);
        return;
    }

    u16 hdr_off = (u16)((u8 *)hdr - (u8 *)pg);
    u16 header_region = (u16)_align8(sizeof(SlabPage));
    if (hdr_off < header_region ||
        (hdr_off - header_region) % pg->block_bytes != 0) {
        release_spinlock(&bin->lock);
        printk("kfree: offset invalid hdr=%p off=%u\n", hdr, hdr_off);
        return;
    }

    if (hdr->tag != SMALL_MAGIC_USED) {
        release_spinlock(&bin->lock);
        printk("kfree: double/corrupt hdr=%p tag=%x\n", hdr, hdr->tag);
        return;
    }

    int was_full = (pg->first_free_off == 0xFFFF);

    // 回收块
    hdr->tag      = SMALL_MAGIC_FREE;
    hdr->next_off = pg->first_free_off;
    pg->first_free_off = hdr_off;
    pg->inuse--;

    if (pg->inuse == 0) {
        if (pg->next || pg->prev || bin->partial == pg) {
            bin_remove(bin, pg);
        }
        release_spinlock(&bin->lock);
        kfree_page(pg);
        return;
    }

    if (was_full) {
        bin_insert_head(bin, pg);
    }

    release_spinlock(&bin->lock);
}