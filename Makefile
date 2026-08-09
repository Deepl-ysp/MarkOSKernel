# ============================================================
# markos - UEFI Application Build System
# Based on the gnu-efi build infrastructure
# ============================================================

# Target architecture: x86_64, ia32, aarch64, arm, riscv64, loongarch64
ARCH ?= x86_64

# GNU-EFI source tree location
EFI_SRCDIR := $(CURDIR)/efi

# Application source directory
APP_SRCDIR := $(CURDIR)/src

# Build output directory
BUILDDIR   := $(CURDIR)/build/$(ARCH)

# --- Include gnu-efi build defaults ---
# Provides: CC, AS, LD, AR, OBJCOPY, CFLAGS, LDFLAGS, INCDIR, ARCH detection, etc.
# TOPDIR must point to the gnu-efi source tree so include paths resolve correctly.
export TOPDIR := $(EFI_SRCDIR)
SRCDIR := $(APP_SRCDIR)
include $(EFI_SRCDIR)/Make.defaults

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

# --- Application source files ---
APP_SRCS_C   := $(wildcard $(APP_SRCDIR)/*.c)
APP_SRCS_CPP := $(wildcard $(APP_SRCDIR)/*.cpp)
APP_OBJS := $(patsubst $(APP_SRCDIR)/%.c,$(BUILDDIR)/%.o,$(APP_SRCS_C)) \
            $(patsubst $(APP_SRCDIR)/%.cpp,$(BUILDDIR)/%.o,$(APP_SRCS_CPP))

# --- Build targets ---
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

# UEFI subsystem (0xa = application, 0xb = boot service driver, 0xc = runtime driver)
SUBSYSTEM := 0xa

# Set FORMAT and SUBSYSTEM_DEFINES based on objcopy EFI support
ifeq ($(SYSTEM_HAS_EFI_OBJCOPY),1)
  FORMAT := -O efi-app-$(SRC_ARCH)
  ifneq ($(IS_MINGW32),)
    # MinGW: gcc does the linking, pass subsystem via -Wl
    SUBSYSTEM_DEFINES = -Wl,--subsystem,$(SUBSYSTEM)
    EFI_LDFLAGS += -s -Wl,-dll
    ifeq ($(ARCH),ia32)
      EFI_LDFLAGS += -e _efi_main
    else
      EFI_LDFLAGS += -e efi_main
    endif
  else
    # ELF: subsystem is embedded by objcopy's efi-app format
    SUBSYSTEM_DEFINES =
  endif
else
  # No EFI objcopy: binary format + --defsym for subsystem (ld only)
  FORMAT := -O binary
  SUBSYSTEM_DEFINES = --defsym=EFI_SUBSYSTEM=$(SUBSYSTEM)
endif

# Ensure wide character strings are 2 bytes (UEFI CHAR16 requirement)
EFI_CFLAGS += -fshort-wchar

# ============================================================
# Phony targets
# ============================================================
.PHONY: all clean gnuefi lib info run run-win

all: $(TARGET)

# Build gnu-efi gnuefi library (crt0 + libgnuefi.a)
# This is an order-only prerequisite of the link step, so it runs
# before linking but does not trigger unnecessary rebuilds.
gnuefi:
	@echo "==> Building gnu-efi gnuefi library (crt0 + libgnuefi.a)"
	$(MAKE) -C $(EFI_SRCDIR) TOPDIR=$(EFI_SRCDIR) gnuefi

# Build gnu-efi lib (libefi.a)
lib:
	@echo "==> Building gnu-efi lib (libefi.a)"
	$(MAKE) -C $(EFI_SRCDIR) TOPDIR=$(EFI_SRCDIR) lib

# ============================================================
# Build rules
# ============================================================

ifeq ($(IS_MINGW32),)

# --- ELF toolchain: link .o -> .so, then objcopy .so -> .efi ---

# Link object files into a shared object (ELF)
$(TARGET_SO): $(APP_OBJS) | gnuefi
	@mkdir -p $(BUILDDIR)
	@echo "  LD       $(notdir $@)"
	$(LD) $(LDFLAGS) $(SUBSYSTEM_DEFINES) $(APP_OBJS) -o $@ $(LOADLIBES)

# Convert ELF shared object to PE/COFF EFI binary
$(TARGET): $(TARGET_SO)
	@echo "  OBJCOPY  $(notdir $@)"
	$(OBJCOPY) -j .text -j .sdata -j .data -j .dynamic -j .rodata \
	    -j .rel -j .rela -j .rel.* -j .rela.* -j .rel* -j .rela* \
	    -j .areloc -j .reloc $(FORMAT) $< $@

else

# --- MinGW toolchain: compile + link directly to .efi ---

$(TARGET): $(APP_OBJS)
	@mkdir -p $(BUILDDIR)
	@echo "  CCLD     $(notdir $@)"
	$(CC) $(LDFLAGS) $(SUBSYSTEM_DEFINES) $(APP_OBJS) -o $@ $(LOADLIBES)

endif

# Compile C source files
$(BUILDDIR)/%.o: $(APP_SRCDIR)/%.c
	@mkdir -p $(BUILDDIR)
	@echo "  CC       $(notdir $@)"
	$(CC) $(INCDIR) $(CFLAGS) -c $< -o $@

# Compile C++ source files
# Note: removes C-only flags (-Wstrict-prototypes, -std=c11) and
#       disables exceptions/RTTI since UEFI has no C++ runtime support.
$(BUILDDIR)/%.o: $(APP_SRCDIR)/%.cpp
	@mkdir -p $(BUILDDIR)
	@echo "  CXX      $(notdir $@)"
	$(CXX) $(INCDIR) \
	    $(filter-out -Wstrict-prototypes -std=c11,$(CFLAGS)) \
	    -std=gnu++11 -fno-exceptions -fno-rtti -c $< -o $@

# ============================================================
# Utility targets
# ============================================================

clean:
	rm -rf $(CURDIR)/build

# ============================================================
# Run in QEMU virtual machine
# ============================================================

# QEMU executable (auto-detect or override with QEMU=...)
# Try MSYS2 path first, then PATH
QEMU ?= $(shell which qemu-system-x86_64 2>/dev/null || echo "C:/msys64/mingw64/bin/qemu-system-x86_64.exe")

# OVMF firmware paths
OVMF_CODE := $(CURDIR)/tools/firmware/OVMF_CODE.fd
OVMF_VARS := $(CURDIR)/tools/firmware/OVMF_VARS.fd
OVMF_VARS_COPY := $(BUILDDIR)/OVMF_VARS_copy.fd

# Disk image path (created by make_efi_disk.py if needed)
DISK_IMG := $(BUILDDIR)/disk.img

# Memory size for QEMU (MB)
QEMU_MEM ?= 256

# Run the UEFI application in QEMU with OVMF firmware
run: $(TARGET)
	@echo "==> Preparing QEMU UEFI environment"
	@# Create a writable copy of OVMF_VARS (QEMU needs write access)
	@cp "$(OVMF_VARS)" "$(OVMF_VARS_COPY)"
	@# Create FAT disk image with the EFI application
	@python3 tools/make_efi_disk.py "$(TARGET)" "$(DISK_IMG)" $(SRC_ARCH) || \
		echo "Warning: disk image creation failed, using virtual FAT directory"
	@echo "==> Starting QEMU ($(QEMU_MEM)MB RAM, OVMF firmware)"
	@if [ -f "$(DISK_IMG)" ]; then \
		$(QEMU) \
			-drive if=pflash,format=raw,readonly=on,file="$(OVMF_CODE)" \
			-drive if=pflash,format=raw,file="$(OVMF_VARS_COPY)" \
			-drive format=raw,file="$(DISK_IMG)" \
			-m $(QEMU_MEM) \
			-serial stdio \
			-display gtk \
			-no-reboot; \
	else \
		mkdir -p $(BUILDDIR)/boot_dir/EFI/BOOT; \
		cp "$(TARGET)" $(BUILDDIR)/boot_dir/EFI/BOOT/BOOTX64.EFI; \
		$(QEMU) \
			-drive if=pflash,format=raw,readonly=on,file="$(OVMF_CODE)" \
			-drive if=pflash,format=raw,file="$(OVMF_VARS_COPY)" \
			-drive file=fat:rw:$(BUILDDIR)/boot_dir,format=raw \
			-m $(QEMU_MEM) \
			-serial stdio \
			-display gtk \
			-no-reboot; \
		rm -rf $(BUILDDIR)/boot_dir; \
	fi

# Windows batch runner (use when make is unavailable or for convenience)
run-win: $(TARGET)
	@tools\run.bat

# Print build configuration for debugging
info:
	@echo "=== markos Build Configuration ==="
	@echo "  ARCH:       $(ARCH)"
	@echo "  SRC_ARCH:   $(SRC_ARCH)"
	@echo "  CC:         $(CC)"
	@echo "  CXX:        $(CXX)"
	@echo "  LD:         $(LD)"
	@echo "  OBJCOPY:    $(OBJCOPY)"
	@echo "  CFLAGS:     $(CFLAGS)"
	@echo "  INCDIR:     $(INCDIR)"
	@echo "  LDFLAGS:    $(LDFLAGS)"
	@echo "  LOADLIBES:  $(LOADLIBES)"
	@echo "  TARGET:     $(TARGET)"
	@echo "  CRTOBJS:    $(CRTOBJS)"
	@echo "  LDSCRIPT:   $(LDSCRIPT)"
	@echo "  IS_MINGW32: $(IS_MINGW32)"
	@echo "  LIBGCC:     $(LIBGCC)"
