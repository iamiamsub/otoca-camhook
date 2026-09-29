// qrtest: checks that the game's QR reader decodes scan.bmp the way the cabinet path would.
//
//   qrtest <modules dir> <otoca-camhook.dll>      (scan.bmp next to the DLL)
//
// Loads the game's libcamera.dll, then otoca-camhook on top of it (as spice2x -k does), calls
// LibCameraRun / LibCameraGetImage through libcamera's own exports, converts the frame like arkkep's
// default_convert (flag 0), and runs QRDecode.dll with arkkep's settings (arkQRInit / FUN_10013480).
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <vector>

using run_fn = int(__cdecl *)(int);
using image_fn = int(__cdecl *)(int, void *);
using init_fn = int(__stdcall *)(BITMAPINFO *);
using decode_fn = int(__stdcall *)(void *);
using count_fn = int(__stdcall *)(int *);
using info_fn = int(__stdcall *)(void *, int);
using free_fn = void(__stdcall *)(void *);
using term_fn = int(__stdcall *)();
using mode_fn = void(__stdcall *)(int *);

int wmain(int argc, wchar_t **argv) {
    if (argc != 3) {
        std::printf("usage: qrtest <modules dir> <otoca-camhook.dll>\n");
        return 2;
    }
    SetDllDirectoryW(argv[1]);  // the game's DLLs (QRDecode.dll needs pintl.dll from the same folder)
    HMODULE cam = LoadLibraryW(L"libcamera.dll");
    HMODULE hook = cam ? LoadLibraryW(argv[2]) : nullptr;
    HMODULE qr = LoadLibraryW(L"QRDecode.dll");
    if (!cam || !hook || !qr) {
        std::printf("load failed: libcamera=%p hook=%p qr=%p (%lu)\n", cam, hook, qr, GetLastError());
        return 1;
    }
    auto run = reinterpret_cast<run_fn>(GetProcAddress(cam, "?LibCameraRun@@YA?AW4LIBCAMERA_STATUS@@H@Z"));
    auto get_image =
        reinterpret_cast<image_fn>(GetProcAddress(cam, "?LibCameraGetImage@@YA?AW4LIBCAMERA_STATUS@@HPAX@Z"));
    auto init = reinterpret_cast<init_fn>(GetProcAddress(qr, "?QRDecoderInit@@YGHPAUtagBITMAPINFO@@@Z"));
    auto decode = reinterpret_cast<decode_fn>(GetProcAddress(qr, "?QRDecode@@YGHPAUIDR_RECT@@@Z"));
    auto count = reinterpret_cast<count_fn>(GetProcAddress(qr, "?QRGetCount@@YGHPAH@Z"));
    auto info = reinterpret_cast<info_fn>(GetProcAddress(qr, "?QRGetInfo@@YGHPAUtagQRInfo@@H@Z"));
    auto free_info = reinterpret_cast<free_fn>(GetProcAddress(qr, "?QRFreeInfo@@YGXPAUtagQRInfo@@@Z"));
    auto term = reinterpret_cast<term_fn>(GetProcAddress(qr, "?QRDecoderTerminate@@YGHXZ"));
    auto get_mode = reinterpret_cast<mode_fn>(GetProcAddress(qr, "?QRGetDefaultMode@@YGXPAUtagQR_MODE@@@Z"));
    auto set_mode = reinterpret_cast<mode_fn>(GetProcAddress(qr, "?QRSetProcMode@@YGXPBUtagQR_MODE@@@Z"));

    const int w = 640, h = 480, stride = w * 3;
    std::vector<unsigned char> frame(stride * h);
    int rr = run(0), rg = get_image(0, frame.data());
    std::printf("LibCameraRun=%d LibCameraGetImage=%d\n", rr, rg);
    if (rr || rg) return 1;

    // arkQRInit: 640x480 24-bit bottom-up DIB, mode {0xc, 1, 4}
    int mode[3] = {0xc, 0, 0};
    get_mode(mode);
    mode[1] = 1;
    mode[2] = 4;
    set_mode(mode);
    std::vector<unsigned char> dib(sizeof(BITMAPINFOHEADER) + stride * h);
    auto bih = reinterpret_cast<BITMAPINFOHEADER *>(dib.data());
    bih->biSize = sizeof(BITMAPINFOHEADER);
    bih->biWidth = w;
    bih->biHeight = h;
    bih->biPlanes = 1;
    bih->biBitCount = 24;
    unsigned char *dst = dib.data() + sizeof(BITMAPINFOHEADER);

    // default_convert(640, 480, 1920, flag 0, src, dst): source pixels last to first, each written mirrored in its row
    int k = 0;
    auto at = [&](int i) -> unsigned char & { return dst[(i / stride + 1) * stride - i % stride - 1]; };
    for (int u = w * h - 1; u >= 0; u--) {
        const unsigned char *p = &frame[u * 3];
        at(k) = p[2];
        at(k + 1) = p[1];
        at(k + 2) = p[0];
        k += 3;
    }

    int r = init(reinterpret_cast<BITMAPINFO *>(bih));
    short rect[4] = {0, 0, w, h};
    int rd = r == 0 ? decode(rect) : -1, n = 0;
    int rc = rd == 0 ? count(&n) : -1;
    std::printf("init=%d decode=%d count=%d found=%d\n", r, rd, rc, n);
    if (rc != 0 || n < 1) return 1;
    int qi[24] = {};
    int ri = info(qi, 0);
    // arkkep accepts: version (qi[2]) 2 or 3, ECC level (qi[3]) 0-3, qi[5] == 0, qi[12] > 0; data pointer at *qi[13]
    std::printf("info=%d version=%d ecc=%d err=%d segments=%d\n", ri, qi[2], qi[3], qi[5], qi[12]);
    bool ok = ri == 0 && (qi[2] == 2 || qi[2] == 3) && qi[3] >= 0 && qi[3] <= 3 && qi[5] == 0 && qi[12] > 0;
    if (ok) {
        // arkkep keeps (version, mode, ecc) as the 12-byte header the game compares per card type,
        // then copies the version/ECC capacity worth of bytes (FUN_10013480)
        const int *seg = reinterpret_cast<const int *>(qi[13]);
        static const int mode_map[][2] = {{-1, 0}, {1, 1}, {2, 2}, {4, 3}, {8, 4}, {16, 5}};
        static const int capacity[2][4] = {{0x20, 0x1a, 0x14, 0x0e}, {0x35, 0x2a, 0x20, 0x18}};
        int seg_mode = -1;
        for (const auto &mm : mode_map)
            if (mm[0] == seg[1]) seg_mode = mm[1];
        std::printf("header: %d %d %d\ndata:", qi[2], seg_mode, qi[3]);
        const unsigned char *data = *reinterpret_cast<unsigned char *const *>(seg);
        for (int i = 0; i < capacity[qi[2] - 2][qi[3]]; i++) std::printf(" %02x", data[i]);
        std::printf("\n");
    }
    free_info(qi);
    term();
    std::printf(ok ? "OK: arkkep would accept this QR\n" : "NG: arkkep would reject this QR\n");
    return ok ? 0 : 1;
}
