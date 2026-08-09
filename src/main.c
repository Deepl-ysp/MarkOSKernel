#include <efi.h>
#include <efilib.h>

void Run(){

    Print(L'MarkOS Load complete');
}

EFI_STATUS
EFIAPI
efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    InitializeLib(ImageHandle, SystemTable);

    Print(L"MarkOS Kernel Loding...\n");

    Run();

    return EFI_SUCCESS;
}