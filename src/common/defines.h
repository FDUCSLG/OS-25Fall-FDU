#pragma once

#ifndef __cplusplus
#define true 1
#define false 0
#define auto __auto_type
typedef char bool;
#endif

#define TRUE true
#define FALSE false
#ifndef NULL
#define NULL 0
#endif

typedef signed char i8;
typedef unsigned char u8;
typedef signed short i16;
typedef unsigned short u16;
typedef signed int i32;
typedef unsigned int u32;
typedef signed long long i64;
typedef unsigned long long u64;

typedef i64 isize;
typedef u64 usize;

/* Efficient min and max operations */
#define MIN(_a, _b)             \
    ({                          \
        typeof(_a) __a = (_a);  \
        typeof(_b) __b = (_b);  \
        __a <= __b ? __a : __b; \
    })

#define MAX(_a, _b)             \
    ({                          \
        typeof(_a) __a = (_a);  \
        typeof(_b) __b = (_b);  \
        __a >= __b ? __a : __b; \
    })

#define BIT(i) (1ull << (i))

#define NO_BSS __attribute__((section(".data")))
#define NO_RETURN __attribute__((noreturn))
#define INLINE inline __attribute__((unused))
#define ALWAYS_INLINE inline __attribute__((unused, always_inline))
#define NO_INLINE __attribute__((noinline))
#define NO_IPA __attribute__((noipa))
#define WARN_RESULT __attribute__((warn_unused_result))

// NOTE: no_return will disable traps.
// NO_RETURN NO_INLINE void no_return();

/* Return the offset of `member` inside struct `type`. */
#define offset_of(type, member) ((usize)(&((type *)NULL)->member))

/**
 * The following macro assumes that `mptr` is a pointer to a `member` within
 * a struct of type `type`. It returns a pointer to the encompassing struct of
 * type `type` that contains this `member`.
 *
 * This macro is particularly useful in scenarios involving lists. For instance,
 * it is common practice to embed a `ListNode` within a struct, as demonstrated
 * below:
 * 
 * typedef struct {
 *     u64 data;
 *     ListNode node;
 * } Container;
 *
 * Container a;
 * ListNode *b = &a.node;
 *
 * In this example, the expression `container_of(b, Container, node)` will yield
 * the same result as `&a`.
 */
#define container_of(mptr, type, member)                      \
    ({                                                        \
        const typeof(((type *)NULL)->member) *_mptr = (mptr); \
        (type *)((u8 *)_mptr - offset_of(type, member));      \
    })

/* Return the largest c that c is a multiple of b and c <= a. */
static INLINE u64 round_down(u64 a, u64 b)
{
    return a - a % b;
}

/* Return the smallest c that c is a multiple of b and c >= a. */
static INLINE u64 round_up(u64 a, u64 b)
{
    return round_down(a + b - 1, b);
}

void _panic(const char *, int);
NO_INLINE NO_RETURN void _panic(const char *, int);
#define PANIC() _panic(__FILE__, __LINE__)
#define ASSERT(expr) \
    ({               \
        if (!(expr)) \
            PANIC(); \
    })

#define LO(addr) (u32)((addr) & 0xffffffff)
#define HI(addr) (u32)(((addr) >> 32) & 0xffffffff)
#define REG(addr) (*(volatile u32 *)(u64)(addr))
