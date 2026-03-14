#include <ntddk.h>
#include <wdm.h>

/*
 *  HideWindow – Kernel driver that hides a window from all screen captures.
 *  
 *  It receives a window handle (HWND) from user-mode via IOCTL and calls the
 *  internal win32kfull!GreProtectSpriteContent function to mark that window
 *  with WDA_EXCLUDEFROMCAPTURE (0x11). After that, any screen capture tool
 *  (OBS, Zoom, Teams, PrintScreen) will see a black/transparent area instead
 *  of the window.
 *  
 *  The address of GreProtectSpriteContent must be updated after every reboot
 *  because of KASLR. You can find it with WinDbg:
 *  
 *      lkd> x win32kfull!GreProtectSpriteContent *
 *      Example: ffffcfb5`0cd4f8c8 win32kfull!GreProtectSpriteContent
 *  
 *  Then replace the value below.
 */

// Update this value after every reboot
#define GRE_PROTECT_SPRITE_CONTENT_ADDR 0xffffcfb50cd4f8c8

// Function type – how the kernel expects to call it
typedef LONG64(*GreProtectSpriteContent_t)(LONG64, ULONG64, LONG32, CHAR);

// IOCTL that user-mode sends to pass a window handle
#define IOCTL_HIDE_WINDOW CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

// Structure received from user-mode
typedef struct _HIDE_WINDOW_DATA {
    ULONG64 WindowHandle;  // HWND (64‑bit, works everywhere)
    ULONG   Flags;         // currently unused
} HIDE_WINDOW_DATA, * PHIDE_WINDOW_DATA;

PDEVICE_OBJECT g_DeviceObject = NULL;

// Driver unload routine – cleans up
VOID DriverUnload(PDRIVER_OBJECT DriverObject) {
    UNREFERENCED_PARAMETER(DriverObject);
    DbgPrint("HideWindow: Unloaded\n");
}

// Handles IRP_MJ_CREATE and IRP_MJ_CLOSE – just succeed
NTSTATUS DispatchCreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

// Handles the IOCTL – receives HWND, calls the internal function
NTSTATUS DispatchDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    ULONG info = 0;

    if (stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_HIDE_WINDOW) {
        if (stack->Parameters.DeviceIoControl.InputBufferLength >= sizeof(HIDE_WINDOW_DATA)) {
            PHIDE_WINDOW_DATA data = (PHIDE_WINDOW_DATA)Irp->AssociatedIrp.SystemBuffer;
            DbgPrint("HideWindow: Received HWND %p\n", (HANDLE)data->WindowHandle);

            // Get the function address from the hardcoded value
            GreProtectSpriteContent_t GreProtectSpriteContent = (GreProtectSpriteContent_t)GRE_PROTECT_SPRITE_CONTENT_ADDR;
            DbgPrint("HideWindow: GreProtectSpriteContent at %p\n", GreProtectSpriteContent);

            // Call it: parameters (0, HWND, 1, 0x11) where 0x11 = WDA_EXCLUDEFROMCAPTURE
            LONG64 result = GreProtectSpriteContent(0, data->WindowHandle, 1, 0x11);
            DbgPrint("HideWindow: GreProtectSpriteContent returned %lld\n", result);

            status = STATUS_SUCCESS;
        } else {
            status = STATUS_INFO_LENGTH_MISMATCH;
        }
    }

    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = info;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

// Driver entry point – creates the device and sets up the dispatch routines
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    UNREFERENCED_PARAMETER(RegistryPath);
    DbgPrint("HideWindow: Loading...\n");

    // Create the device object
    UNICODE_STRING devName;
    RtlInitUnicodeString(&devName, L"\\Device\\HideWindow");
    NTSTATUS status = IoCreateDevice(DriverObject, 0, &devName, FILE_DEVICE_UNKNOWN, 0, FALSE, &g_DeviceObject);
    if (!NT_SUCCESS(status)) {
        DbgPrint("HideWindow: IoCreateDevice failed 0x%x\n", status);
        return status;
    }

    // Create a symbolic link so user-mode can open \\.\HideWindow
    UNICODE_STRING symLink;
    RtlInitUnicodeString(&symLink, L"\\DosDevices\\HideWindow");
    status = IoCreateSymbolicLink(&symLink, &devName);
    if (!NT_SUCCESS(status)) {
        DbgPrint("HideWindow: IoCreateSymbolicLink failed 0x%x\n", status);
        IoDeleteDevice(g_DeviceObject);
        return status;
    }

    // Set the dispatch routines
    DriverObject->MajorFunction[IRP_MJ_CREATE] = DispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]  = DispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchDeviceControl;
    DriverObject->DriverUnload = DriverUnload;

    DbgPrint("HideWindow: Loaded successfully\n");
    return STATUS_SUCCESS;
}