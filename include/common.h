#ifndef COMMON_H
#define COMMON_H

#define PAGE_PRESENT    (1 << 0)
#define PAGE_WRITE      (1 << 1)
#define PAGE_HUGE       (1 << 7)

#define KERNEL_VMA      0xFFFFFFFF80000000
#define KERNEL_LOAD_PHYS 0x2000000

struct GDTEntry {
    unsigned short limit_low;
    unsigned short base_low;
    unsigned char  base_mid;
    unsigned char  access;
    unsigned char  limit_high_and_flags;
    unsigned char  base_high;
} __attribute__((packed));

struct GDTDescriptor {
    unsigned short limit;
    unsigned long long base;
} __attribute__((packed));

typedef struct {
    unsigned char  e_ident[16];
    unsigned short e_type;
    unsigned short e_machine;
    unsigned int   e_version;
    unsigned long long e_entry;
    unsigned long long e_phoff;
    unsigned long long e_shoff;
    unsigned int   e_flags;
    unsigned short e_ehsize;
    unsigned short e_phentsize;
    unsigned short e_phnum;
    unsigned short e_shentsize;
    unsigned short e_shnum;
    unsigned short e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    unsigned int   p_type;
    unsigned int   p_flags;
    unsigned long long p_offset;
    unsigned long long p_vaddr;
    unsigned long long p_paddr;
    unsigned long long p_filesz;
    unsigned long long p_memsz;
    unsigned long long p_align;
} Elf64_Phdr;

#define PT_LOAD 1

#endif