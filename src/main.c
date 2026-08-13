#include <efi.h>
#include <efilib.h>
#include <common.h>

// GOP 信息结构体，必须与内核定义完全一致
struct GOPInfo {
    unsigned long long FrameBufferBase;
    unsigned int HorizontalResolution;
    unsigned int VerticalResolution;
    unsigned int PixelsPerScanLine;
} __attribute__((packed));

// ==================== 分配并清零一个物理页 ====================
static UINT64 *alloc_page_zero(EFI_BOOT_SERVICES *bs) {
    EFI_PHYSICAL_ADDRESS addr = 0;
    bs->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &addr);
    UINT64 *ptr = (UINT64 *)(UINTN)addr;
    bs->SetMem(ptr, 4096, 0);
    return ptr;
}

// ==================== 页表构建（映射低 4GB + 内核高地址） ====================
static UINT64 *build_page_tables(EFI_BOOT_SERVICES *bs,
                                 EFI_PHYSICAL_ADDRESS kernel_phys) {
    UINT64 *pml4 = alloc_page_zero(bs);

    // ========== 映射低 4GB 物理地址到相同虚拟地址（恒等映射） ==========
    // 使用一个 PDPT，其前 4 个条目分别指向 4 个 PD，每个 PD 映射 1GB
    UINT64 *pdpt_low = alloc_page_zero(bs);
    pml4[0] = (UINT64)(UINTN)pdpt_low | PAGE_PRESENT | PAGE_WRITE;

    for (int i = 0; i < 4; i++) {
        UINT64 *pd = alloc_page_zero(bs);
        pdpt_low[i] = (UINT64)(UINTN)pd | PAGE_PRESENT | PAGE_WRITE;

        for (int j = 0; j < 512; j++) {
            UINT64 phys = (UINT64)i * 0x40000000ULL + (UINT64)j * 0x200000ULL;
            pd[j] = phys | PAGE_PRESENT | PAGE_WRITE | PAGE_HUGE;
        }
    }

    // ========== 映射内核高地址（KERNEL_VMA）到物理地址 kernel_phys（2MB 页） ==========
    UINT64 *pdpt_high = alloc_page_zero(bs);
    UINT64 *pd_high   = alloc_page_zero(bs);

    int pml4_high_idx = (KERNEL_VMA >> 39) & 0x1FF;
    int pdpt_high_idx = (KERNEL_VMA >> 30) & 0x1FF;
    int pd_high_idx   = (KERNEL_VMA >> 21) & 0x1FF;

    pml4[pml4_high_idx] = (UINT64)(UINTN)pdpt_high | PAGE_PRESENT | PAGE_WRITE;
    pdpt_high[pdpt_high_idx] = (UINT64)(UINTN)pd_high | PAGE_PRESENT | PAGE_WRITE;

    UINT64 phys_2m_aligned = kernel_phys & ~((1ULL << 21) - 1);
    pd_high[pd_high_idx] = phys_2m_aligned | PAGE_PRESENT | PAGE_WRITE | PAGE_HUGE;

    return pml4;
}

// ==================== GDT 和临时栈准备 ====================
static EFI_PHYSICAL_ADDRESS gdt_stack_phys;

static void setup_gdt_and_stack(EFI_BOOT_SERVICES *bs) {
    EFI_PHYSICAL_ADDRESS addr = 0;
    // 分配 8 页（32KB），第一页放 GDT，其余作为栈
    bs->AllocatePages(AllocateAnyPages, EfiLoaderData, 8, &addr);
    UINT8 *base = (UINT8 *)(UINTN)addr;

    // GDT 放在 base 偏移 10 处
    struct GDTEntry *gdt = (struct GDTEntry *)(base + 10);
    // 空描述符
    gdt[0].limit_low = 0; gdt[0].base_low = 0; gdt[0].base_mid = 0;
    gdt[0].access = 0; gdt[0].limit_high_and_flags = 0; gdt[0].base_high = 0;
    // 64 位代码段
    gdt[1].limit_low = 0; gdt[1].base_low = 0; gdt[1].base_mid = 0;
    gdt[1].access = 0x9A; gdt[1].limit_high_and_flags = 0xA0; gdt[1].base_high = 0;
    // 64 位数据段
    gdt[2].limit_low = 0; gdt[2].base_low = 0; gdt[2].base_mid = 0;
    gdt[2].access = 0x92; gdt[2].limit_high_and_flags = 0xC0; gdt[2].base_high = 0;

    UINT16 *limit_ptr = (UINT16 *)base;
    UINT64 *base_ptr  = (UINT64 *)(base + 2);
    *limit_ptr = sizeof(struct GDTEntry) * 3 - 1;
    *base_ptr  = addr + 10;

    gdt_stack_phys = addr;
}
// ==================== GOP 信息填充到 0x1000 ====================
#define GOP_INFO_ADDR 0x1000

static EFI_STATUS fill_gop_info(EFI_SYSTEM_TABLE *ST) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    EFI_STATUS Status;

    Status = ST->BootServices->LocateProtocol(
        &gEfiGraphicsOutputProtocolGuid, NULL, (VOID**)&gop);

    if (EFI_ERROR(Status) || gop == NULL)
        return EFI_NOT_FOUND;

    struct GOPInfo *info = (struct GOPInfo *)GOP_INFO_ADDR;

    info->FrameBufferBase      = gop->Mode->FrameBufferBase;
    info->HorizontalResolution = gop->Mode->Info->HorizontalResolution;
    info->VerticalResolution   = gop->Mode->Info->VerticalResolution;
    info->PixelsPerScanLine    = gop->Mode->Info->PixelsPerScanLine;

    return EFI_SUCCESS;
}

// ==================== ELF 内核加载器 ====================
static EFI_STATUS load_kernel_elf(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *ST,
                                   EFI_PHYSICAL_ADDRESS *kernel_phys, void **kernel_entry) {
    EFI_STATUS Status;
    EFI_LOADED_IMAGE *LoadedImage;
    Status = ST->BootServices->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID**)&LoadedImage);
    if (EFI_ERROR(Status)) return Status;

    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs;
    Status = ST->BootServices->HandleProtocol(LoadedImage->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID**)&Fs);
    if (EFI_ERROR(Status)) return Status;

    EFI_FILE_PROTOCOL *Root;
    Status = Fs->OpenVolume(Fs, &Root);
    if (EFI_ERROR(Status)) return Status;

    EFI_FILE_PROTOCOL *File;
    Status = Root->Open(Root, &File, L"kernel.elf", EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(Status)) { Root->Close(Root); return Status; }

    // 读取 ELF 头
    Elf64_Ehdr ehdr;
    UINTN HeaderSize = sizeof(Elf64_Ehdr);
    Status = File->Read(File, &HeaderSize, &ehdr);
    if (EFI_ERROR(Status) || HeaderSize != sizeof(Elf64_Ehdr) ||
        ehdr.e_ident[0] != 0x7f || ehdr.e_ident[1] != 'E' ||
        ehdr.e_ident[2] != 'L' || ehdr.e_ident[3] != 'F') {
        File->Close(File); Root->Close(Root);
        return EFI_LOAD_ERROR;
    }

    // 读取程序头
    UINTN PhdrSize = ehdr.e_phentsize * ehdr.e_phnum;
    Elf64_Phdr *phdrs;
    Status = ST->BootServices->AllocatePool(EfiLoaderData, PhdrSize, (VOID**)&phdrs);
    if (EFI_ERROR(Status)) { File->Close(File); Root->Close(Root); return Status; }

    Status = File->SetPosition(File, ehdr.e_phoff);
    if (EFI_ERROR(Status)) { ST->BootServices->FreePool(phdrs); File->Close(File); Root->Close(Root); return Status; }
    Status = File->Read(File, &PhdrSize, phdrs);
    if (EFI_ERROR(Status)) { ST->BootServices->FreePool(phdrs); File->Close(File); Root->Close(Root); return Status; }

    // 计算内核所需物理地址范围
    UINT64 min_phys = 0xFFFFFFFFFFFFFFFF, max_phys = 0;
    for (int i = 0; i < ehdr.e_phnum; i++) {
        if (phdrs[i].p_type != PT_LOAD) continue;
        UINT64 seg_start = phdrs[i].p_paddr;
        UINT64 seg_end = seg_start + phdrs[i].p_memsz;
        if (seg_start < min_phys) min_phys = seg_start;
        if (seg_end > max_phys) max_phys = seg_end;
    }

    if (min_phys == 0xFFFFFFFFFFFFFFFF) {
        ST->BootServices->FreePool(phdrs); File->Close(File); Root->Close(Root);
        return EFI_LOAD_ERROR;
    }

    UINTN total_pages = (max_phys - min_phys + 0xFFF) >> 12;
    EFI_PHYSICAL_ADDRESS alloc_addr = min_phys;
    Status = ST->BootServices->AllocatePages(AllocateAddress, EfiLoaderData, total_pages, &alloc_addr);
    if (EFI_ERROR(Status)) {
        ST->BootServices->FreePool(phdrs); File->Close(File); Root->Close(Root);
        Print(L"Failed to allocate %d pages at 0x%llx\n", total_pages, min_phys);
        return Status;
    }

    // 加载每个 LOAD 段
    for (int i = 0; i < ehdr.e_phnum; i++) {
        if (phdrs[i].p_type != PT_LOAD) continue;
        UINT8 *dest = (UINT8 *)(UINTN)phdrs[i].p_paddr;
        UINTN filesz = (UINTN)phdrs[i].p_filesz;
        UINTN memsz  = (UINTN)phdrs[i].p_memsz;

        if (filesz > 0) {
            File->SetPosition(File, phdrs[i].p_offset);
            UINTN read = filesz;
            Status = File->Read(File, &read, dest);
            if (EFI_ERROR(Status) || read != filesz) break;
        }
        if (memsz > filesz) {
            ST->BootServices->SetMem(dest + filesz, memsz - filesz, 0);
        }
    }

    ST->BootServices->FreePool(phdrs);
    File->Close(File);
    Root->Close(Root);

    if (EFI_ERROR(Status)) return Status;

    *kernel_phys   = min_phys;
    *kernel_entry  = (void *)(UINTN)(min_phys + (ehdr.e_entry - KERNEL_VMA));

    return EFI_SUCCESS;
}

// ==================== UEFI 主入口 ====================
EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);
    Print(L"MarkOS Bootloader\n");

    EFI_PHYSICAL_ADDRESS kernel_phys;
    void *kernel_entry;
    EFI_STATUS Status = load_kernel_elf(ImageHandle, SystemTable, &kernel_phys, &kernel_entry);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to load kernel: %r\n", Status);
        return Status;
    }
    Print(L"Kernel loaded at 0x%llx, entry %p\n", kernel_phys, kernel_entry);

    // 获取 GOP 信息并填充到 0x1000
    Status = fill_gop_info(SystemTable);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to get GOP: %r\n", Status);
    }

    // 构建页表，映射低 4GB + 内核高地址
    UINT64 *pml4 = build_page_tables(SystemTable->BootServices, kernel_phys);
    setup_gdt_and_stack(SystemTable->BootServices);

    // 获取内存映射并退出 Boot Services
    UINTN MapSize = 0, MapKey = 0, DescriptorSize = 0;
    UINT32 DescriptorVersion = 0;
    EFI_MEMORY_DESCRIPTOR *MemoryMap = NULL;

    Status = SystemTable->BootServices->GetMemoryMap(&MapSize, MemoryMap, &MapKey, &DescriptorSize, &DescriptorVersion);
    if (Status != EFI_BUFFER_TOO_SMALL && EFI_ERROR(Status)) {
        Print(L"GetMemoryMap (first) error: %r\n", Status);
        return Status;
    }
    Status = SystemTable->BootServices->AllocatePool(EfiLoaderData, MapSize, (VOID**)&MemoryMap);
    if (EFI_ERROR(Status)) {
        Print(L"AllocatePool for MemoryMap failed: %r\n", Status);
        return Status;
    }
    while (1) {
        Status = SystemTable->BootServices->GetMemoryMap(&MapSize, MemoryMap, &MapKey, &DescriptorSize, &DescriptorVersion);
        if (Status == EFI_BUFFER_TOO_SMALL) {
            SystemTable->BootServices->FreePool(MemoryMap);
            Status = SystemTable->BootServices->AllocatePool(EfiLoaderData, MapSize, (VOID**)&MemoryMap);
            if (EFI_ERROR(Status)) return Status;
            continue;
        } else if (EFI_ERROR(Status)) {
            Print(L"GetMemoryMap error: %r\n", Status);
            return Status;
        }
        break;
    }

    Print(L"Jumping to kernel - PML4:%llx GDT:%llx Entry:%llx\n",
          (UINT64)(UINTN)pml4, (UINT64)gdt_stack_phys, (UINT64)(UINTN)kernel_entry);

    while (1) {
        Status = SystemTable->BootServices->ExitBootServices(ImageHandle, MapKey);
        if (Status == EFI_INVALID_PARAMETER) {
            SystemTable->BootServices->FreePool(MemoryMap);
            MapSize = 0;
            Status = SystemTable->BootServices->GetMemoryMap(&MapSize, NULL, &MapKey, &DescriptorSize, &DescriptorVersion);
            if (Status != EFI_BUFFER_TOO_SMALL && EFI_ERROR(Status)) return Status;
            Status = SystemTable->BootServices->AllocatePool(EfiLoaderData, MapSize, (VOID**)&MemoryMap);
            if (EFI_ERROR(Status)) return Status;
            Status = SystemTable->BootServices->GetMemoryMap(&MapSize, MemoryMap, &MapKey, &DescriptorSize, &DescriptorVersion);
            if (EFI_ERROR(Status)) return Status;
        } else if (EFI_ERROR(Status)) {
            Print(L"ExitBootServices error: %r\n", Status);
            return Status;
        } else {
            break;
        }
    }

    // 直接跳转，严禁再使用任何 Boot Services
    __asm__ volatile (
        "cli\n\t"
        "mov %0, %%rdi\n\t"
        "mov %1, %%rsi\n\t"
        "jmp *%2\n\t"
        :
        : "r"((UINT64)(UINTN)pml4), "r"((UINT64)gdt_stack_phys), "r"((UINT64)(UINTN)kernel_entry)
        : "rdi", "rsi", "memory"
    );

    __builtin_unreachable();
}