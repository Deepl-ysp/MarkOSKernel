ENTRY(_start)

KERNEL_VMA = 0xFFFFFFFF80000000;
KERNEL_LOAD_PHYS = 0x2000000;

SECTIONS
{
    . = KERNEL_VMA;

    .text : AT(ADDR(.text) - KERNEL_VMA + KERNEL_LOAD_PHYS)
    {
        *(.text)
    }

    .data : AT(ADDR(.data) - KERNEL_VMA + KERNEL_LOAD_PHYS)
    {
        *(.data)
    }

    .bss : AT(ADDR(.bss) - KERNEL_VMA + KERNEL_LOAD_PHYS)
    {
        __bss_start = .;
        *(.bss)
        __bss_end = .;
    }

    _kernel_end = .;
    _kernel_size = _kernel_end - KERNEL_VMA;

    /DISCARD/ : { *(.comment) *(.eh_frame) *(.note*) }
}