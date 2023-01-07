#include "EFIApp.h"

// This is the actual entrypoint.
// Application entrypoint (must be set to 'efi_main' for gnu-efi crt0 compatibility)
EFI_STATUS efi_main(
	EFI_HANDLE image, 
	EFI_SYSTEM_TABLE *systab
)
{
    EFI_LOADED_IMAGE_PROTOCOL *loaded_image;
    EFI_DEVICE_PATH_PROTOCOL *device_path;
    EFI_HANDLE device;
    EFI_FILE_IO_INTERFACE *disk;
    EFI_FILE *root;
    EFI_FILE *file;
    VOID *buffer;
    UINTN size;
    EFI_STATUS status;
    EFI_FILE_INFO *FileInfo;
    
    // Allocate memory for FileInfo
    FileInfo = AllocatePool(sizeof(EFI_FILE_INFO));
    if (FileInfo == NULL) {
        // Handle error
    }

#if defined(_GNU_EFI)
	InitializeLib(
		image, 
		systab
	);
#endif

    // Get the Loaded Image Protocol for this image
    status = uefi_call_wrapper(BS->HandleProtocol, 3, image, &LoadedImageProtocol, (VOID **) &loaded_image);
    if (EFI_ERROR(status)) {
        Print(L"Failed to get Loaded Image Protocol\n");
        return status;
    }

    // Get the device handle and device path for the boot partition
    device = loaded_image->DeviceHandle;
    status = uefi_call_wrapper(BS->HandleProtocol, 3, device, &DevicePathProtocol, (VOID **) &device_path);
    if (EFI_ERROR(status)) {
        Print(L"Failed to get Device Path Protocol\n");
        return status;
    }

    // Open the disk for the boot partition
    status = uefi_call_wrapper(BS->HandleProtocol, 3, device, &FileSystemProtocol, (VOID **) &disk);
    if (EFI_ERROR(status)) {
        Print(L"Failed to get File System Protocol\n");
        return status;
    }

    // Open the root directory of the boot partition
    status = uefi_call_wrapper(disk->OpenVolume, 2, disk, &root);
    if (EFI_ERROR(status)) {
        Print(L"Failed to open root directory\n");
        return status;
    }

    // Open the Linux kernel file
    status = uefi_call_wrapper(root->Open, 5, root, &file, L"Image", EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status)) {
        Print(L"Failed to open Image\n");
       
    // Allocate a buffer for the kernel
    size = 0;
    status = uefi_call_wrapper(file->GetInfo, 4, file, &FileInfo, &size, NULL);
    if (EFI_ERROR(status) && status != EFI_BUFFER_TOO_SMALL) {
        Print(L"Failed to get file size\n");
        return status;
    }

    buffer = AllocatePool(size);
    if (buffer == NULL) {
        Print(L"Failed to allocate buffer for kernel\n");
        return EFI_OUT_OF_RESOURCES;
    }

    // Read the kernel into the buffer
    size = 0;
    status = uefi_call_wrapper(file->Read, 3, file, &size, buffer);
    if (EFI_ERROR(status)) {
        Print(L"Failed to read kernel\n");
        return status;
    }

    // Close the kernel file
    status = uefi_call_wrapper(file->Close, 1, file);
    if (EFI_ERROR(status)) {
        Print(L"Failed to close Image\n");
        return status;
    }

    // Open the DTB file
    status = uefi_call_wrapper(root->Open, 5, root, &file, L"lumia.dtb", EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status)) {
        Print(L"Failed to open dtb\n");
        return status;
    }

    // Allocate a buffer for the DTB
    size = 0;
    status = uefi_call_wrapper(file->GetInfo, 4, file, &FileInfo, &size, NULL);
    if (EFI_ERROR(status) && status != EFI_BUFFER_TOO_SMALL) {
        Print(L"Failed to get file size\n");
        return status;
    }

    VOID *dtb = AllocatePool(size);
    if (dtb == NULL) {
        Print(L"Failed to allocate buffer for dtb\n");
        return EFI_OUT_OF_RESOURCES;
    }

    // Read the DTB into the buffer
    size = 0;
    status = uefi_call_wrapper(file->Read, 3, file, &size, dtb);
    if (EFI_ERROR(status)) {
        Print(L"Failed to read dtb\n");
        return status;
    }

    // Close the DTB file
    status = uefi_call_wrapper(file->Close, 1, file);
    if (EFI_ERROR(status)) {
        Print(L"Failed to close dtb\n");
        return status;
    }

    // Set the command line arguments
    CHAR16 *cmdline = L"";
    EFI_PHYSICAL_ADDRESS cmdline_paddr;
    cmdline_paddr = (EFI_PHYSICAL_ADDRESS)(UINTN)cmdline;

    // Set up the EFI boot information
    EFI_BOOT_SERVICES *bs = systab->BootServices;
    // Allocate pages for the kernel and DTB
    EFI_PHYSICAL_ADDRESS kernel_paddr;
    status = uefi_call_wrapper(bs->AllocatePages, 4, AllocateAnyPages, EfiLoaderData, EFI_SIZE_TO_PAGES(size), &kernel_paddr);
    if (EFI_ERROR(status)) {
        Print(L"Failed to allocate pages for kernel\n");
        return status;
    }

    EFI_PHYSICAL_ADDRESS dtb_paddr;
    status = uefi_call_wrapper(bs->AllocatePages, 4, AllocateAnyPages, EfiLoaderData, EFI_SIZE_TO_PAGES(size), &dtb_paddr);
    if (EFI_ERROR(status)) {
        Print(L"Failed to allocate pages for dtb\n");
        return status;
    }

    // Copy the kernel and DTB to their respective pages
    CopyMem((VOID *)(UINTN)kernel_paddr, buffer, size);
    CopyMem((VOID *)(UINTN)dtb_paddr, dtb, size);

    // Free the buffers
    FreePool(buffer);
    FreePool(dtb);

    // Set up the boot information
    EFI_LOAD_OPTION boot_info;
    boot_info.Attributes = LOAD_OPTION_CATEGORY_BOOT;
    boot_info.FilePathListLength = GetDevicePathSize(device_path);
    boot_info.Description = L"Linux";
    boot_info.FilePathList = device_path;
    boot_info.OptionalData = cmdline_paddr;
    boot_info.OptionalDataSize = (UINT32)(StrLen(cmdline) + 1) * sizeof(CHAR16);

    // Load and start the kernel
    status = uefi_call_wrapper(bs->LoadImage, 6, FALSE, image, &boot_info, NULL, 0, &image);
    if (EFI_ERROR(status)) {
        Print(L"Failed to load kernel\n");
        return status;
    }
	/* De-initialize */
	ArmDeInitialize();

	/* Disable GIC */
	writel(0, GIC_DIST_CTRL);
    
    status = uefi_call_wrapper(bs->StartImage, 3, image, NULL, NULL);
    if (EFI_ERROR(status)) {
        Print(L"Failed to start kernel\n");
        return status;
    }

    return EFI_SUCCESS;
}
