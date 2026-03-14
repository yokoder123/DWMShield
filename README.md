# DWMShield – Kernel‑mode exploration of window capture protection on Windows

## Overview

This project demonstrates how the Windows Desktop Window Manager (DWM) enforces the `WDA_EXCLUDEFROMCAPTURE` flag at the kernel level. It calls an undocumented internal function (`win32kfull!GreProtectSpriteContent`) to mark any window as excluded from screen capture, effectively making it invisible to all user‑mode capture APIs.

The code serves as a **research proof‑of‑concept** for understanding the boundary between user‑mode and kernel‑mode window management. It is intended to illustrate how deeply the capture-exclusion mechanism is embedded in the Windows graphics stack and how the documented user-mode API (`SetWindowDisplayAffinity`) relates to lower-level enforcement.

---

## How it works

1. The driver creates a device `\Device\HideWindow` and a symbolic link `\DosDevices\HideWindow` (accessible as `\\.\HideWindow` from user‑mode).
2. A user‑mode application (`protect.exe`) sends an IOCTL containing the HWND of a target window.
3. The driver locates the internal `win32kfull!GreProtectSpriteContent` function (address must be updated manually due to KASLR) and calls it with that HWND and the flag `0x11` (`WDA_EXCLUDEFROMCAPTURE`).
4. After the call, the Desktop Window Manager applies the corresponding capture-exclusion state in a way that mirrors the behavior associated with the official `SetWindowDisplayAffinity` path.

---

## Building the driver

- In Visual Studio 2022 (with WDK), create a new **Empty WDM Driver** project.
- Add `HideWindow.c` to the Source Files folder.
- Set solution platform to **x64**.
- Build → Build Solution. The `.sys` file will be in `x64\Release\`.

## Building the user‑mode client

Compile `protect.c` with any C compiler (GCC, MSVC, etc.):
```bash
gcc -o protect.exe protect.c
```

---

## Installation and testing

1. Enable test signing and reboot:
   ```powershell
   bcdedit /set testsigning on
   Restart-Computer
   ```
2. Copy `HideWindow.sys` and `protect.exe` to a folder on the target machine (e.g., `C:\Drivers`).
3. Install and start the driver (as Administrator):
   ```powershell
   sc.exe create HideWindow type= kernel binPath= C:\Drivers\HideWindow.sys
   sc.exe start HideWindow
   ```
4. Run `protect.exe` in any console window (not Administrator):
   ```powershell
   C:\Drivers\protect.exe
   ```
5. The console window will no longer appear in screen captures taken with different tools.
6. When done, stop and delete the driver (as Administrator):
   ```powershell
   sc.exe stop HideWindow
   sc.exe delete HideWindow
   ```

---

## Important notes

- Because of KASLR, the address of `GreProtectSpriteContent` changes after every reboot. You need to update the `#define GRE_PROTECT_SPRITE_CONTENT_ADDR` in `HideWindow.c` with the new address found in WinDbg:
  ```
  lkd> x win32kfull!GreProtectSpriteContent *
  ```
  Then rebuild the driver.
- The driver only works on Windows 10 and 11 (it relies on an internal function that exists in those versions).

---

## Known limitations (research context)

- **Address hardcoding**: The function address must be updated manually after each reboot. This is a direct consequence of KASLR and the fact that `GreProtectSpriteContent` is not exported. A more robust research implementation would need a safer and more maintainable way to resolve the address dynamically.
- **Windows version sensitivity**: The signature and availability of internal functions can change with Windows updates. This code was tested on build 19041 (Windows 10 2004).
- **Minimal validation**: The driver does not validate the HWND it receives; a malformed handle could lead to unpredictable behaviour. In a real driver, proper validation would be essential.
- **Test signing only**: The driver is signed with a test certificate and requires `testsigning` mode. For deployment on locked‑down systems, a valid EV certificate or disabled DSE would be needed – neither is recommended for production.

---

## Research motivation

This project started from a technical question: *"How does the `WDA_EXCLUDEFROMCAPTURE` flag actually work below the official API?"*  
The public function `SetWindowDisplayAffinity` can be detected with `GetWindowDisplayAffinity`, so we wanted to see whether the exclusion is truly enforced at the kernel level.

By reverse‑engineering `win32kfull.sys` we located the internal function `GreProtectSpriteContent` – the routine that ultimately implements the flag. Calling it directly bypasses the user‑mode API and confirms that the protection is applied inside the Desktop Window Manager, completely invisible to any user‑mode process.

The experiment shows that **any security mechanism that depends solely on user‑mode cooperation can be observed or circumvented from user‑mode itself**. This is a fundamental design reality of Windows, not a vulnerability – it simply highlights the boundary between user and kernel space, a topic well understood by operating‑system researchers and kernel developers.

---

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.