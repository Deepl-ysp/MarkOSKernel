# ============================================================
# markos - UEFI Application + Kernel Build System
# ============================================================

# Target architecture: x86_64 only for now
ARCH ?= x86_64

# GNU-EFI source tree location
EFI_SRCDIR := $(CURDIR)/efi

# Application source directory
APP_SRCDIR := $(CURDIR)/src

# Build output directory
BUILDDIR   := $(CURDIR)/build/$(ARCH)

# --- Include gnu-efi build defaults ---
export TOPDIR := $(EFI_SRCDIR)
SRCDIR := $(APP_SRCDIR)
include $(EFI_SRCDIR)/Make.defaults

# Additional include paths
INCDIR += \
    -I$(CURDIR)/include \
    -I$(EFI_SRCDIR)/inc/internal \
    -I$(EFI_SRCDIR)/inc/legacy \
    -I$(EFI_SRCDIR)/inc/subst \
    -I$(EFI_SRCDIR)/inc/ia32 \
    -I$(EFI_SRCDIR)/inc/aarch64 \
    -I$(EFI_SRCDIR)/inc/arm \
    -I$(EFI_SRCDIR)/inc/riscv64 \
    -I$(EFI_SRCDIR)/inc/loongarch64

# C++ compiler (gnu-efi only defines CC for C)
CXX := $(prefix)$(CROSS_COMPILE)g++

# --- UEFI Application source files ---
APP_SRCS_C   := $(wildcard $(APP_SRCDIR)/*.c)
APP_SRCS_CPP := $(wildcard $(APP_SRCDIR)/*.cpp)
APP_OBJS := $(patsubst $(APP_SRCDIR)/%.c,$(BUILDDIR)/%.o,$(APP_SRCS_C)) \
            $(patsubst $(APP_SRCDIR)/%.cpp,$(BUILDDIR)/%.o,$(APP_SRCS_CPP))

# --- UEFI Build targets ---
TARGET    := $(BUILDDIR)/main.efi
TARGET_SO := $(BUILDDIR)/main.so

# --- gnuefi CRT0, linker script, and libraries ---
ifeq ($(IS_MINGW32),)
# ELF toolchain (Linux/WSL): needs crt0, linker script, and objcopy step
CRTOBJS  := $(EFI_SRCDIR)/$(ARCH)/gnuefi/crt0-efi-$(SRC_ARCH).o
LDSCRIPT := $(EFI_SRCDIR)/gnuefi/elf_$(SRC_ARCH)_efi.lds
EFI_LDFLAGS += -L$(EFI_SRCDIR)/$(ARCH)/lib -L$(EFI_SRCDIR)/$(ARCH)/gnuefi $(CRTOBJS)
LOADLIBES  += -T $(LDSCRIPT) -lefi -lgnuefi $(LIBGCC)
else
# MinGW toolchain: direct compilation, no crt0/linker script needed
LOADLIBES  += -L$(EFI_SRCDIR)/$(ARCH)/lib -lefi $(LIBGCC)
endif

# UEFI subsystem (0xa = application)
SUBSYSTEM := 0xa

# Set FORMAT and SUBSYSTEM_DEFINES based on objcopy EFI support
ifeq ($(SYSTEM_HAS_EFI_OBJCOPY),1)
  FORMAT := -O efi-app-$(SRC_ARCH)
  ifneq ($(IS_MINGW32),)
    SUBSYSTEM_DEFINES = -Wl,--subsystem,$(SUBSYSTEM)
    EFI_LDFLAGS += -s -Wl,-dll
    ifeq ($(ARCH),ia32)
      EFI_LDFLAGS += -e _efi_main
    else
      EFI_LDFLAGS += -e efi_main
    endif
  else
    SUBSYSTEM_DEFINES =
  endif
else
  FORMAT := -O binary
  SUBSYSTEM_DEFINES = --defsym=EFI_SUBSYSTEM=$(SUBSYSTEM)
endif

EFI_CFLAGS += -fshort-wchar

# ============================================================
# Kernel build (using x86_64-elf toolchain)
# ============================================================
KERNEL_DIR    := kernel
KERNEL_SRCS   := $(KERNEL_DIR)/start.S $(KERNEL_DIR)/kernel.c
KERNEL_OBJS   := $(BUILDDIR)/kernel_start.o $(BUILDDIR)/kernel_kernel.o
KERNEL_ELF    := $(BUILDDIR)/kernel.elf
KERNEL_BIN    := $(BUILDDIR)/kernel.bin

# Kernel compiler (ELF cross compiler)
KERNEL_CC     := x86_64-elf-gcc
KERNEL_LD     := x86_64-elf-ld
KERNEL_OBJCOPY := x86_64-elf-objcopy

KERNEL_CFLAGS := -I$(CURDIR)/include -ffreestanding -nostdlib -fno-stack-protector \
                 -fno-builtin -mno-red-zone -std=gnu11 -Wall -Wextra \
                 -O2 -fomit-frame-pointer -mcmodel=kernel
KERNEL_LDFLAGS := -T $(KERNEL_DIR)/linker.ld -nostdlib -static

# ============================================================
# Phony targets
# ============================================================
.PHONY: all clean gnuefi lib info run run-win kernel

all: $(TARGET) $(KERNEL_BIN)

kernel: $(KERNEL_BIN)

# Build gnu-efi libraries (only needed for UEFI)
gnuefi:
	@echo "==> Building gnu-efi gnuefi library"
	$(MAKE) -C $(EFI_SRCDIR) TOPDIR=$(EFI_SRCDIR) gnuefi

lib:
	@echo "==> Building gnu-efi lib"
	$(MAKE) -C $(EFI_SRCDIR) TOPDIR=$(EFI_SRCDIR) lib

# ============================================================
# UEFI Build rules
# ============================================================
ifeq ($(IS_MINGW32),)

# ELF toolchain: link .o -> .so, then objcopy .so -> .efi
$(TARGET_SO): $(APP_OBJS) | gnuefi
	@mkdir -p $(BUILDDIR)
	@echo "  LD       $(notdir $@)"
	$(LD) $(LDFLAGS) $(SUBSYSTEM_DEFINES) $(APP_OBJS) -o $@ $(LOADLIBES)

$(TARGET): $(TARGET_SO)
	@echo "  OBJCOPY  $(notdir $@)"
	$(OBJCOPY) -j .text -j .sdata -j .data -j .dynamic -j .rodata \
	    -j .rel -j .rela -j .rel.* -j .rela.* -j .rel* -j .rela* \
	    -j .areloc -j .reloc $(FORMAT) $< $@

else

# MinGW: direct link
$(TARGET): $(APP_OBJS)
	@mkdir -p $(BUILDDIR)
	@echo "  CCLD     $(notdir $@)"
	$(CC) $(LDFLAGS) $(SUBSYSTEM_DEFINES) $(APP_OBJS) -o $@ $(LOADLIBES)

endif

# Compile UEFI C sources
$(BUILDDIR)/%.o: $(APP_SRCDIR)/%.c
	@mkdir -p $(BUILDDIR)
	@echo "  CC       $(notdir $@)"
	$(CC) $(INCDIR) $(CFLAGS) -c $< -o $@

# Compile UEFI C++ sources (if any)
$(BUILDDIR)/%.o: $(APP_SRCDIR)/%.cpp
	@mkdir -p $(BUILDDIR)
	@echo "  CXX      $(notdir $@)"
	$(CXX) $(INCDIR) \
	    $(filter-out -Wstrict-prototypes -std=c11,$(CFLAGS)) \
	    -std=gnu++11 -fno-exceptions -fno-rtti -c $< -o $@

# ============================================================
# Kernel Build rules
# ============================================================
$(BUILDDIR)/kernel_start.o: $(KERNEL_DIR)/start.S
	@mkdir -p $(BUILDDIR)
	@echo "  AS       $(notdir $@)"
	$(KERNEL_CC) -c $< -o $@

$(BUILDDIR)/kernel_kernel.o: $(KERNEL_DIR)/kernel.c
	@mkdir -p $(BUILDDIR)
	@echo "  CC       $(notdir $@)"
	$(KERNEL_CC) $(KERNEL_CFLAGS) -c $< -o $@

$(KERNEL_ELF): $(KERNEL_OBJS)
	@echo "  LD       $(notdir $@)"
	$(KERNEL_LD) $(KERNEL_LDFLAGS) $^ -o $@

$(KERNEL_BIN): $(KERNEL_ELF)
	@echo "  OBJCOPY  $(notdir $@)"
	$(KERNEL_OBJCOPY) -O binary $< $@

# ============================================================
# Clean
# ============================================================
clean:
	rm -rf $(BUILDDIR)

# ============================================================
# Run in QEMU
# ============================================================
QEMU ?= $(shell which qemu-system-x86_64 2>/dev/null || echo "C:/msys64/mingw64/bin/qemu-system-x86_64.exe")
OVMF_CODE := $(CURDIR)/tools/firmware/OVMF_CODE.fd
OVMF_VARS := $(CURDIR)/tools/firmware/OVMF_VARS.fd
OVMF_VARS_COPY := $(BUILDDIR)/OVMF_VARS_copy.fd
QEMU_MEM ?= 256

run: $(TARGET) $(KERNEL_ELF)
	@mkdir -p $(BUILDDIR)/boot_dir/EFI/BOOT
	@cp $(TARGET) $(BUILDDIR)/boot_dir/EFI/BOOT/BOOTX64.EFI
	@cp $(KERNEL_ELF) $(BUILDDIR)/boot_dir/
	@if [ ! -f $(BUILDDIR)/boot_dir/kernel.elf ]; then \
		echo "ERROR: kernel.elf not copied!"; exit 1; \
	fi
	@cp "$(OVMF_VARS)" "$(OVMF_VARS_COPY)"
	@echo "==> Starting QEMU..."
	$(QEMU) \
	    -drive if=pflash,format=raw,readonly=on,file="$(OVMF_CODE)" \
	    -drive if=pflash,format=raw,file="$(OVMF_VARS_COPY)" \
	    -drive file=fat:rw:$(shell cygpath -m $(BUILDDIR)/boot_dir 2>/dev/null || echo $(BUILDDIR)/boot_dir),format=raw \
	    -m $(QEMU_MEM) \
	    -serial stdio \
	    -display gtk \
	    -no-reboot

# ============================================================
# Info
# ============================================================
info:
	@echo "=== markos Build Configuration ==="
	@echo "  ARCH:       $(ARCH)"
	@echo "  SRC_ARCH:   $(SRC_ARCH)"
	@echo "  CC (UEFI):  $(CC)"
	@echo "  LD (UEFI):  $(LD)"
	@echo "  OBJCOPY:    $(OBJCOPY)"
	@echo "  KERNEL_CC:  $(KERNEL_CC)"
	@echo "  KERNEL_LD:  $(KERNEL_LD)"
	@echo "  TARGET:     $(TARGET)"
	@echo "  KERNEL_BIN: $(KERNEL_BIN)"
	@echo "  IS_MINGW32: $(IS_MINGW32)"