.intel_syntax noprefix
.code64
.section .text
.global _start

_start:
    cli
    mov cr3, rdi
    lgdt [rsi]

    lea rsp, [rsi + 8 * 4096]
    xor rbp, rbp

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    push 0x08
    lea rax, [high_code]
    push rax
    retfq

high_code:
    call kmain
    cli
    hlt
    jmp .