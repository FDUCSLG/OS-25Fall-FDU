#include <common/format.h>
#include <common/spinlock.h>
#include <common/string.h>
#include <kernel/printk.h>

static SpinLock printk_lock;

void printk_init()
{
    init_spinlock(&printk_lock);
}

static void _put_char(void *_ctx, char c)
{
    (void)_ctx;
    putch(c);
}

static void _vprintf(const char *fmt, va_list arg)
{
    acquire_spinlock(&printk_lock);
    int colorize = 0;
    if (fmt) {
        // Color lines that start with "ERROR" or "[ERROR" in red
        if (strncmp(fmt, "ERROR", 5) == 0 || strncmp(fmt, "[ERROR", 6) == 0) {
            colorize = 1;
        }
    }
    if (colorize) {
        const char *red = "\033[1;31m";
        while (*red) { _put_char(NULL, *red++); }
    }
    vformat(_put_char, NULL, fmt, arg);
    if (colorize) {
        const char *rst = "\033[0m";
        while (*rst) { _put_char(NULL, *rst++); }
    }
    release_spinlock(&printk_lock);
}

void printk(const char *fmt, ...)
{
    va_list arg;
    va_start(arg, fmt);
    _vprintf(fmt, arg);
    va_end(arg);
}