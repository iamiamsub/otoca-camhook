// otoca-camhook: stands in for the cabinet camera of otoca d'or.
//
// The game reads cards through libcamera.dll (DirectShow, physical USB cameras only). This DLL replaces
// its exports so LibCameraGetImage hands the game the frame otoca-scan puts in shared memory (frame.h)
// while its button is held; otherwise the camera sees nothing (a white frame).
// Load it with spice2x `-k`; spice loads -k DLLs after its own libcamera hooks, so the jumps written here win.
#include <windows.h>

#include <cstdio>
#include <cstring>

#include "frame.h"

namespace {

void log(const char *msg) { OutputDebugStringA(msg); }  // spice's debughook puts it in log.txt

// Camera replacements. LIBCAMERA_STATUS 0 = success.
void __cdecl cam_init() {}
int __cdecl cam_open(double, int, void *) { return 0; }
int __cdecl cam_stop(int) { return 0; }
int __cdecl cam_run(int) { return 0; }
int __cdecl cam_get_camera_nr() { return 1; }
int __cdecl cam_set_brightness(long, int) { return 0; }

int __cdecl cam_get_brightness(long *value, int) {
    if (value) *value = 0;
    return 0;
}

int __cdecl cam_get_brightness_range(long *min, long *max, int) {
    if (min) *min = -64;
    if (max) *max = 64;
    return 0;
}

// arkkep calls this with its camera lock held: no file or console I/O here.
int __cdecl cam_get_image(int, void *buffer) {
    static frame::Shared *shared = frame::open();
    if (!buffer) return 0;
    if (shared) {
        shared->last_read = GetTickCount();
        if (frame::showing(shared)) {
            std::memcpy(buffer, shared->pixels, frame::kSize);
            return 0;
        }
    }
    std::memset(buffer, 0xff, frame::kSize);
    return 0;
}

bool jump(unsigned char *from, void *to) {
    DWORD old;
    if (!from || !VirtualProtect(from, 5, PAGE_EXECUTE_READWRITE, &old)) return false;
    from[0] = 0xe9;  // jmp rel32
    *reinterpret_cast<int *>(from + 1) = static_cast<int>(reinterpret_cast<unsigned char *>(to) - (from + 5));
    VirtualProtect(from, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), from, 5);
    return true;
}

bool jump(HMODULE mod, const char *name, void *to) {
    return jump(reinterpret_cast<unsigned char *>(GetProcAddress(mod, name)), to);
}

void hook_camera() {
    // arkkep.dll imports libcamera.dll, so it is loaded before spice2x loads -k DLLs.
    HMODULE cam = GetModuleHandleW(L"libcamera.dll");
    if (!cam) {
        log("otoca-camhook: libcamera.dll not loaded, camera not hooked\n");
        return;
    }
    const struct {
        const char *name;
        void *to;
    } hooks[] = {
        {"?LibCameraInit@@YAXXZ", reinterpret_cast<void *>(cam_init)},
        {"?LibCameraOpen@@YA?AW4LIBCAMERA_STATUS@@NHPAUt_libcamera_open_param@@@Z", reinterpret_cast<void *>(cam_open)},
        {"?LibCameraStop@@YA?AW4LIBCAMERA_STATUS@@H@Z", reinterpret_cast<void *>(cam_stop)},
        {"?LibCameraRun@@YA?AW4LIBCAMERA_STATUS@@H@Z", reinterpret_cast<void *>(cam_run)},
        {"?LibCameraGetCameraNr@@YAHXZ", reinterpret_cast<void *>(cam_get_camera_nr)},
        {"?LibCameraGetImage@@YA?AW4LIBCAMERA_STATUS@@HPAX@Z", reinterpret_cast<void *>(cam_get_image)},
        {"?LibCameraSetBrightness@@YA?AW4LIBCAMERA_STATUS@@JH@Z", reinterpret_cast<void *>(cam_set_brightness)},
        {"?LibCameraGetBrightness@@YA?AW4LIBCAMERA_STATUS@@PAJH@Z", reinterpret_cast<void *>(cam_get_brightness)},
        {"?LibCameraGetBrightnessRange@@YA?AW4LIBCAMERA_STATUS@@PAJ0H@Z",
         reinterpret_cast<void *>(cam_get_brightness_range)},
    };
    int n = 0;
    for (const auto &h : hooks) n += jump(cam, h.name, h.to);
    char msg[128];
    std::snprintf(msg, sizeof msg, "otoca-camhook: %d/%d libcamera functions replaced\n", n,
                  static_cast<int>(sizeof hooks / sizeof hooks[0]));
    log(msg);
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(self);
        hook_camera();
    }
    return TRUE;
}
