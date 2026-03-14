#include <windows.h>
#include <stdio.h>

// We define the constants needed for IOCTL ourselves
#ifndef FILE_DEVICE_UNKNOWN
#define FILE_DEVICE_UNKNOWN 0x00000022
#endif

#ifndef METHOD_BUFFERED
#define METHOD_BUFFERED 0
#endif

#ifndef FILE_ANY_ACCESS
#define FILE_ANY_ACCESS 0
#endif

// Standard CTL_CODE macro (copied from winioctl.h)
#define CTL_CODE(DeviceType, Function, Method, Access) \
    (((DeviceType) << 16) | ((Access) << 14) | ((Function) << 2) | (Method))

// Our IOCTL – must match the one in the driver
#define IOCTL_HIDE_WINDOW CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

// Structure sent to the driver – same as in the driver
typedef struct _HIDE_WINDOW_DATA {
    ULONG64 WindowHandle;
    ULONG   Flags;
} HIDE_WINDOW_DATA;

int main() {
    // Open the driver (the symbolic link created by the driver)
    HANDLE hDevice = CreateFileA("\\\\.\\HideWindow",
                                  GENERIC_READ | GENERIC_WRITE,
                                  0, NULL, OPEN_EXISTING, 0, NULL);
    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("Failed to open driver. Error: %d\n", GetLastError());
        return 1;
    }

    // Get the handle of the current console window
    HWND hWnd = GetConsoleWindow();
    if (hWnd == NULL) {
        printf("Failed to get console window.\n");
        CloseHandle(hDevice);
        return 1;
    }

    printf("Console HWND: %p\n", hWnd);

    // Prepare the data structure – HWND is pointer sized, cast safely to 64‑bit
    HIDE_WINDOW_DATA data;
    data.WindowHandle = (ULONG64)(ULONG_PTR)hWnd;
    data.Flags = 0;

    DWORD bytesReturned;
    // Send the IOCTL to the driver
    if (DeviceIoControl(hDevice, IOCTL_HIDE_WINDOW,
                        &data, sizeof(data),
                        NULL, 0, &bytesReturned, NULL)) {
        printf("Window hiding activated successfully!\n");
        printf("This window is now invisible to any screen capture tool.\n");
    } else {
        printf("Failed to send IOCTL. Error: %d\n", GetLastError());
    }

    CloseHandle(hDevice);
    printf("\nPress Enter to exit...\n");
    getchar();
    return 0;
}