#include <efi.h>
#include <efilib.h>



EFI_STATUS InitFileSystem(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_STATUS Status;
    EFI_LOADED_IMAGE *LoadedImage;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs;
    EFI_FILE_PROTOCOL *Root;

    // 获取当前镜像的加载信息
    Status = SystemTable->BootServices->HandleProtocol(
        ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&LoadedImage);
    if (EFI_ERROR(Status))
        return Status;

    // 获取磁盘上的文件系统协议
    Status = SystemTable->BootServices->HandleProtocol(
        LoadedImage->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
    if (EFI_ERROR(Status))
        return Status;

    // 打开根目录
    Status = Fs->OpenVolume(Fs, &Root);
    if (EFI_ERROR(Status))
        return Status;

    // 接下来可以用 Root 来打开文件了
    // ...

    return EFI_SUCCESS;
}