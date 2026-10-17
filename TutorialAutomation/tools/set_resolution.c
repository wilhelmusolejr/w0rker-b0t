/* Sets the primary display to a fixed resolution so the desktop layout is the same over RDP,
   on the console after tscon, and in RustDesk. It changes only the display mode of the session
   it runs in and never sends input. When the primary display cannot show the size (a VM console
   is often 640x480 only), it moves the desktop to another display that can and detaches the rest.
   If no display can, it plugs in its own virtual monitor (RustDesk's Amyuni driver) first; that
   monitor stays when RustDesk disconnects. Plugging in needs administrator rights.
     set_resolution [WIDTH HEIGHT] [--wait SECONDS] [--log FILE]   (default 1920 1080, wait 20)
     set_resolution [WIDTH HEIGHT] --watch [--log FILE]            (keep re-applying every 5 s)
     set_resolution --list */
#include <windows.h>
#include <shellapi.h>
#include <setupapi.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static FILE *log_file;

static void note(const char *format, ...) {
    SYSTEMTIME now; va_list args;
    GetLocalTime(&now);
    printf("[%04u-%02u-%02u %02u:%02u:%02u] ", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    va_start(args, format); vprintf(format, args); va_end(args); putchar('\n'); fflush(stdout);
    if (log_file) {
        fprintf(log_file, "[%04u-%02u-%02u %02u:%02u:%02u] ", now.wYear, now.wMonth, now.wDay,
            now.wHour, now.wMinute, now.wSecond);
        va_start(args, format); vfprintf(log_file, format, args); va_end(args);
        fputc('\n', log_file); fflush(log_file);
    }
}

static int primary_display(DISPLAY_DEVICEW *out) {
    DWORD i;
    for (i = 0; ; ++i) {
        DISPLAY_DEVICEW device = {0};
        device.cb = sizeof(device);
        if (!EnumDisplayDevicesW(NULL, i, &device, 0)) return 0;
        if ((device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) &&
            (device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)) { *out = device; return 1; }
    }
}

static int supports(const wchar_t *name, DWORD width, DWORD height) {
    DWORD k;
    for (k = 0; ; ++k) {
        DEVMODEW mode = {0};
        mode.dmSize = sizeof(mode);
        if (!EnumDisplaySettingsW(name, k, &mode)) return 0;
        if (mode.dmPelsWidth == width && mode.dmPelsHeight == height) return 1;
    }
}

/* Distinct sizes a display supports, e.g. "800x600 1024x768", written into text. */
static void mode_list(const wchar_t *name, char *text, size_t capacity) {
    DWORD k;
    size_t used = 0;
    text[0] = 0;
    for (k = 0; ; ++k) {
        DEVMODEW mode = {0};
        char size[32];
        int length;
        mode.dmSize = sizeof(mode);
        if (!EnumDisplaySettingsW(name, k, &mode)) break;
        length = snprintf(size, sizeof(size), "%lux%lu", mode.dmPelsWidth, mode.dmPelsHeight);
        if (strstr(text, size)) continue;
        if (used + (size_t)length + 2 >= capacity) break;
        used += (size_t)snprintf(text + used, capacity - used, "%s%s", used ? " " : "", size);
    }
}

/* Every display adapter output, attached or not, with its current size and supported sizes. */
static void list_displays(void) {
    DWORD i;
    for (i = 0; ; ++i) {
        DISPLAY_DEVICEW device = {0};
        DEVMODEW current = {0};
        char modes[2048];
        device.cb = sizeof(device); current.dmSize = sizeof(current);
        if (!EnumDisplayDevicesW(NULL, i, &device, 0)) break;
        EnumDisplaySettingsW(device.DeviceName, ENUM_CURRENT_SETTINGS, &current);
        mode_list(device.DeviceName, modes, sizeof(modes));
        note("%ls%s%s: %ls, current %lux%lu; modes: %s", device.DeviceName,
            (device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) ? " (primary)" : "",
            (device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) ? " (attached)" : "",
            device.DeviceString, current.dmPelsWidth, current.dmPelsHeight, *modes ? modes : "none");
    }
}

/* Another output that can show the size, e.g. a plugged-in virtual monitor. */
static int other_display_with_mode(const wchar_t *skip, DWORD width, DWORD height, DISPLAY_DEVICEW *out) {
    DWORD i;
    for (i = 0; ; ++i) {
        DISPLAY_DEVICEW device = {0};
        device.cb = sizeof(device);
        if (!EnumDisplayDevicesW(NULL, i, &device, 0)) return 0;
        if ((device.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER) || !wcscmp(device.DeviceName, skip)) continue;
        if (supports(device.DeviceName, width, height)) { *out = device; return 1; }
    }
}

/* Amyuni usbmmidd virtual display driver (shipped with RustDesk): device interface and the
   control code that adds a monitor, as used by RustDesk's virtual_display_manager. A monitor
   plugged in here belongs to no RustDesk connection, so it stays when RustDesk disconnects. */
static const GUID usbmmidd_interface = {0xb5ffd75f, 0xda40, 0x4353, {0x8f, 0xf8, 0xb6, 0xda, 0xf6, 0xf1, 0xd8, 0xca}};
#define USBMMIDD_PLUG_CONTROL 2307084
#define USBMMIDD_ADD_MONITOR 0x10

static int plug_virtual_monitor(void) {
    HDEVINFO set = SetupDiGetClassDevsW(&usbmmidd_interface, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    SP_DEVICE_INTERFACE_DATA item = {0};
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail = NULL;
    DWORD size = 0, returned = 0;
    BYTE command[4] = {USBMMIDD_ADD_MONITOR, 0, 0, 0};
    HANDLE device = INVALID_HANDLE_VALUE;
    int ok = 0;
    if (set == INVALID_HANDLE_VALUE) { note("virtual display driver not found (error %lu)", GetLastError()); return 0; }
    item.cbSize = sizeof(item);
    if (!SetupDiEnumDeviceInterfaces(set, NULL, &usbmmidd_interface, 0, &item)) {
        note("virtual display driver interface not found (error %lu)", GetLastError()); goto done;
    }
    SetupDiGetDeviceInterfaceDetailW(set, &item, NULL, 0, &size, NULL);
    if (!size || !(detail = malloc(size))) goto done;
    detail->cbSize = sizeof(*detail);
    if (!SetupDiGetDeviceInterfaceDetailW(set, &item, detail, size, NULL, NULL)) goto done;
    device = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, 0, NULL);
    if (device == INVALID_HANDLE_VALUE) {
        note("cannot open the virtual display driver (error %lu); run as administrator", GetLastError()); goto done;
    }
    ok = DeviceIoControl(device, USBMMIDD_PLUG_CONTROL, command, sizeof(command), NULL, 0, &returned, NULL);
    if (ok) note("plugged in a virtual monitor");
    else note("virtual monitor plug-in failed (error %lu)", GetLastError());
done:
    if (device != INVALID_HANDLE_VALUE) CloseHandle(device);
    free(detail);
    SetupDiDestroyDeviceInfoList(set);
    return ok;
}

/* Make target the only desktop display at width x height; every other output is detached. */
static LONG use_only(const DISPLAY_DEVICEW *target, DWORD width, DWORD height) {
    DEVMODEW mode = {0};
    DWORD i;
    LONG code;
    mode.dmSize = sizeof(mode);
    mode.dmFields = DM_POSITION | DM_PELSWIDTH | DM_PELSHEIGHT;
    mode.dmPelsWidth = width; mode.dmPelsHeight = height;
    code = ChangeDisplaySettingsExW(target->DeviceName, &mode, NULL,
        CDS_SET_PRIMARY | CDS_UPDATEREGISTRY | CDS_NORESET, NULL);
    if (code != DISP_CHANGE_SUCCESSFUL) return code;
    for (i = 0; ; ++i) {
        DISPLAY_DEVICEW device = {0};
        DEVMODEW off = {0};
        device.cb = sizeof(device);
        if (!EnumDisplayDevicesW(NULL, i, &device, 0)) break;
        if (!(device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) || !wcscmp(device.DeviceName, target->DeviceName))
            continue;
        off.dmSize = sizeof(off);
        off.dmFields = DM_POSITION | DM_PELSWIDTH | DM_PELSHEIGHT;
        ChangeDisplaySettingsExW(device.DeviceName, &off, NULL, CDS_UPDATEREGISTRY | CDS_NORESET, NULL);
    }
    return ChangeDisplaySettingsExW(NULL, NULL, NULL, 0, NULL);
}

static const char *result_text(LONG code) {
    switch (code) {
        case DISP_CHANGE_SUCCESSFUL: return "changed";
        case DISP_CHANGE_RESTART: return "changed, restart required";
        case DISP_CHANGE_BADMODE: return "mode not supported";
        case DISP_CHANGE_NOTUPDATED: return "registry write failed";
        case DISP_CHANGE_BADFLAGS: return "bad flags";
        case DISP_CHANGE_BADPARAM: return "bad parameters";
        default: return "failed";
    }
}

/* Plug-ins allowed per run, so a driver that never shows modes cannot fill all four slots. */
#define MAX_PLUGS 2

/* One attempt to make the primary display width x height. Returns 1 when it is.
   quiet suppresses the "already" message for the repeated checks of --watch. */
static int ensure_resolution(DWORD width, DWORD height, unsigned *plugs, int quiet) {
    DISPLAY_DEVICEW primary, other;
    DEVMODEW current = {0};
    int found;
    LONG code;
    /* An RDP session has its own display sized by the RDP client; leave it alone. */
    if (GetSystemMetrics(SM_REMOTESESSION)) {
        if (!quiet) note("remote (RDP) session: display size is set by the RDP client; nothing changed");
        return 1;
    }
    if (!primary_display(&primary)) return 0;
    current.dmSize = sizeof(current);
    EnumDisplaySettingsW(primary.DeviceName, ENUM_CURRENT_SETTINGS, &current);
    if (current.dmPelsWidth == width && current.dmPelsHeight == height) {
        if (!quiet) note("%ls (%ls) is already %lux%lu", primary.DeviceName, primary.DeviceString, width, height);
        return 1;
    }
    if (supports(primary.DeviceName, width, height)) {
        DWORD old_width = current.dmPelsWidth, old_height = current.dmPelsHeight;
        current.dmPelsWidth = width; current.dmPelsHeight = height;
        current.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
        code = ChangeDisplaySettingsExW(primary.DeviceName, &current, NULL, CDS_UPDATEREGISTRY, NULL);
        note("%ls (%ls) %lux%lu -> %lux%lu: %s", primary.DeviceName, primary.DeviceString,
            old_width, old_height, width, height, result_text(code));
        return code == DISP_CHANGE_SUCCESSFUL;
    }
    /* The console's own adapter may be tiny (a VM's Basic Display is 640x480);
       move the desktop to a display that has the size, such as a virtual monitor. */
    found = other_display_with_mode(primary.DeviceName, width, height, &other);
    /* No display can show the size: plug in our own virtual monitor, then give Windows a
       moment to list its modes. */
    if (!found && *plugs < MAX_PLUGS) {
        ++*plugs;
        if (plug_virtual_monitor()) {
            ULONGLONG appear = GetTickCount64() + 5000;
            while (!(found = other_display_with_mode(primary.DeviceName, width, height, &other)) &&
                   GetTickCount64() < appear) Sleep(250);
        }
    }
    if (!found) return 0;
    code = use_only(&other, width, height);
    note("%ls (%ls) cannot show %lux%lu; using only %ls (%ls): %s", primary.DeviceName,
        primary.DeviceString, width, height, other.DeviceName, other.DeviceString, result_text(code));
    return code == DISP_CHANGE_SUCCESSFUL;
}

int main(void) {
    int argc, i, watch = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    DWORD width = 1920, height = 1080, wait_seconds = 20;
    const wchar_t *log_path = NULL;
    ULONGLONG deadline;
    unsigned plugs = 0;
    if (!argv) return 2;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (argc == 2 && !wcscmp(argv[1], L"--list")) { list_displays(); return 0; }
    for (i = 1; i < argc; ++i) {
        if (!wcscmp(argv[i], L"--wait") && i + 1 < argc) wait_seconds = wcstoul(argv[++i], NULL, 10);
        else if (!wcscmp(argv[i], L"--log") && i + 1 < argc) log_path = argv[++i];
        else if (!wcscmp(argv[i], L"--watch")) watch = 1;
        else if (i + 1 < argc && iswdigit(argv[i][0]) && iswdigit(argv[i+1][0])) {
            width = wcstoul(argv[i], NULL, 10); height = wcstoul(argv[++i], NULL, 10);
        } else {
            puts("set_resolution [WIDTH HEIGHT] [--wait SECONDS] [--watch] [--log FILE] | set_resolution --list");
            return 2;
        }
    }
    if (width < 640 || height < 480 || width > 7680 || height > 4320 || wait_seconds > 600) {
        puts("resolution must be 640x480 to 7680x4320 and --wait at most 600"); return 2;
    }
    if (log_path) log_file = _wfopen(log_path, L"ab");
    if (watch) {
        /* Keep the size from now on: re-apply whenever it drifts, e.g. when RustDesk
           disconnects and removes the monitor it plugged in. */
        int was_ok = -1;
        CreateMutexW(NULL, FALSE, L"Local\\TutorialSetResolutionWatch");
        if (GetLastError() == ERROR_ALREADY_EXISTS) { note("resolution watch is already running"); return 0; }
        note("watching: keeping the primary display at %lux%lu", width, height);
        for (;;) {
            int ok = ensure_resolution(width, height, &plugs, was_ok == 1);
            if (!ok && was_ok != 0) { note("could not keep %lux%lu; all displays:", width, height); list_displays(); }
            was_ok = ok;
            Sleep(5000);
        }
    }
    /* After tscon the console display can take a few seconds to appear; retry until it does. */
    deadline = GetTickCount64() + (ULONGLONG)wait_seconds * 1000;
    for (;;) {
        if (ensure_resolution(width, height, &plugs, 0)) { if (log_file) fclose(log_file); return 0; }
        if (GetTickCount64() >= deadline) break;
        Sleep(2000);
    }
    note("could not set %lux%lu; all displays:", width, height);
    list_displays();
    if (log_file) fclose(log_file);
    return 1;
}
