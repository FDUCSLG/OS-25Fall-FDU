#pragma once

extern void putch(char);

void printk_init();
__attribute__((format(printf, 1, 2))) void printk(const char *, ...);
