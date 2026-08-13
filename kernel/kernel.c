extern char __bss_start[], __bss_end[];

#include <utils/print/print.h>

void kernel_main(void) {
    for (char *p = __bss_start; p < __bss_end; p++)
        *p = 0;

    clear_screen();
    reset_cursor();

    puts("Welcome to MarkOS!\n");
    puts("MarkOS>");
    printf("string: %s,char: %c\n", "OS Dev", 'A');

    while (1)
        __asm__ volatile ("hlt");
}

__asm__ (
    ".global kmain\n"
    "kmain:\n"
    "    call kernel_main\n"
    "1:  hlt\n"
    "    jmp 1b\n"
);