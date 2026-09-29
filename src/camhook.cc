// otoca-camhook: stands in for the cabinet camera of otoca d'or.
//
// The game reads cards through libcamera.dll (DirectShow, physical USB cameras only). This DLL replaces
// its exports so LibCameraGetImage hands the game scan.bmp from this DLL's folder as the camera frame
// (640x480, 24-bit). Make scan.bmp with tools/make_scan.py; while it is missing the frame is blank.
// Load it with spice2x `-k`; spice loads -k DLLs after its own libcamera hooks, so the jumps written here win.
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr int kWidth = 640, kHeight = 480, kFrameSize = kWidth * kHeight * 3;

wchar_t g_scan_path[MAX_PATH];
std::vector<unsigned char> g_frame(kFrameSize, 0xff);  // top-down rows, as a camera delivers them
FILETIME g_loaded{};

void log(const char *msg) {
    OutputDebugStringA(msg);
    std::printf("%s", msg);
}

// Reloads scan.bmp when it changed. Missing or unusable file -> white frame (no card in front of the camera).
void refresh_frame() {
    WIN32_FILE_ATTRIBUTE_DATA attr;
    if (!GetFileAttributesExW(g_scan_path, GetFileExInfoStandard, &attr)) {
        if (g_loaded.dwLowDateTime || g_loaded.dwHighDateTime) {
            std::memset(g_frame.data(), 0xff, kFrameSize);
            g_loaded = {};
            log("otoca-camhook: scan.bmp removed, camera shows nothing\n");
        }
        return;
    }
    if (CompareFileTime(&attr.ftLastWriteTime, &g_loaded) == 0) return;

    FILE *f = _wfopen(g_scan_path, L"rb");
    if (!f) return;  // still being written; try again on the next frame
    BITMAPFILEHEADER fh{};
    BITMAPINFOHEADER ih{};
    std::vector<unsigned char> pixels(kFrameSize);
    bool ok = std::fread(&fh, sizeof fh, 1, f) == 1 && std::fread(&ih, sizeof ih, 1, f) == 1 &&
              fh.bfType == 0x4d42 && ih.biWidth == kWidth && (ih.biHeight == kHeight || ih.biHeight == -kHeight) &&
              ih.biBitCount == 24 && ih.biCompression == BI_RGB && std::fseek(f, fh.bfOffBits, SEEK_SET) == 0 &&
              std::fread(pixels.data(), 1, kFrameSize, f) == static_cast<size_t>(kFrameSize);
    std::fclose(f);
    if (!ok) {
        log("otoca-camhook: scan.bmp must be a 640x480 24-bit BMP (use make_scan.py)\n");
        return;
    }
    constexpr int stride = kWidth * 3;
    for (int y = 0; y < kHeight; y++) {
        int src = ih.biHeight > 0 ? kHeight - 1 - y : y;  // positive height = bottom-up file
        std::memcpy(&g_frame[y * stride], &pixels[src * stride], stride);
    }
    g_loaded = attr.ftLastWriteTime;
    log("otoca-camhook: scan.bmp loaded\n");
}

// Replacements. LIBCAMERA_STATUS 0 = success.
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

int __cdecl cam_get_image(int, void *buffer) {
    refresh_frame();
    if (buffer) std::memcpy(buffer, g_frame.data(), kFrameSize);
    return 0;
}

bool jump(HMODULE mod, const char *name, void *to) {
    auto from = reinterpret_cast<unsigned char *>(GetProcAddress(mod, name));
    DWORD old;
    if (!from || !VirtualProtect(from, 5, PAGE_EXECUTE_READWRITE, &old)) return false;
    from[0] = 0xe9;  // jmp rel32
    *reinterpret_cast<int *>(from + 1) = static_cast<int>(reinterpret_cast<unsigned char *>(to) - (from + 5));
    VirtualProtect(from, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), from, 5);
    return true;
}

void install(HMODULE self) {
    GetModuleFileNameW(self, g_scan_path, MAX_PATH);
    wchar_t *slash = wcsrchr(g_scan_path, L'\\');
    wcscpy_s(slash ? slash + 1 : g_scan_path, MAX_PATH - (slash ? slash + 1 - g_scan_path : 0), L"scan.bmp");

    // arkkep.dll imports libcamera.dll, so it is loaded before spice2x loads -k DLLs.
    HMODULE cam = GetModuleHandleW(L"libcamera.dll");
    if (!cam) {
        log("otoca-camhook: libcamera.dll not loaded, nothing hooked\n");
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
        install(self);
    }
    return TRUE;
}
