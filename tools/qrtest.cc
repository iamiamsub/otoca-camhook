// qrtest: checks the whole path from a printed card image to the game's QR reader.
//
//   qrtest <modules dir> <otoca-camhook.dll> <card.png>...
//
// Loads the game's libcamera.dll, then otoca-camhook on top
// (as spice2x -k does). For each card it finds the QR as otoca-scan does, puts the frame in the shared block,
// reads it back through libcamera's own LibCameraGetImage export, converts it like arkkep's default_convert
// (flag 0) and runs the game's QRDecode.dll with arkkep's settings (arkQRInit / FUN_10013480).
#include <windows.h>
#include <objbase.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include "../src/card.h"
#include "../src/frame.h"

namespace {

using run_fn = int(__cdecl *)(int);
using image_fn = int(__cdecl *)(int, void *);
using init_fn = int(__stdcall *)(BITMAPINFO *);
using decode_fn = int(__stdcall *)(void *);
using count_fn = int(__stdcall *)(int *);
using info_fn = int(__stdcall *)(void *, int);
using free_fn = void(__stdcall *)(void *);
using term_fn = int(__stdcall *)();
using mode_fn = void(__stdcall *)(int *);

HMODULE g_qr;

bool decode(const std::vector<unsigned char> &frame) {
    auto init = reinterpret_cast<init_fn>(GetProcAddress(g_qr, "?QRDecoderInit@@YGHPAUtagBITMAPINFO@@@Z"));
    auto run_decode = reinterpret_cast<decode_fn>(GetProcAddress(g_qr, "?QRDecode@@YGHPAUIDR_RECT@@@Z"));
    auto count = reinterpret_cast<count_fn>(GetProcAddress(g_qr, "?QRGetCount@@YGHPAH@Z"));
    auto info = reinterpret_cast<info_fn>(GetProcAddress(g_qr, "?QRGetInfo@@YGHPAUtagQRInfo@@H@Z"));
    auto free_info = reinterpret_cast<free_fn>(GetProcAddress(g_qr, "?QRFreeInfo@@YGXPAUtagQRInfo@@@Z"));
    auto term = reinterpret_cast<term_fn>(GetProcAddress(g_qr, "?QRDecoderTerminate@@YGHXZ"));
    auto get_mode = reinterpret_cast<mode_fn>(GetProcAddress(g_qr, "?QRGetDefaultMode@@YGXPAUtagQR_MODE@@@Z"));
    auto set_mode = reinterpret_cast<mode_fn>(GetProcAddress(g_qr, "?QRSetProcMode@@YGXPBUtagQR_MODE@@@Z"));

    // arkQRInit: 640x480 24-bit bottom-up DIB, mode {0xc, 1, 4}
    const int w = frame::kWidth, h = frame::kHeight, stride = w * 3;
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
    int rd = r == 0 ? run_decode(rect) : -1, n = 0;
    int rc = rd == 0 ? count(&n) : -1;
    if (rc != 0 || n < 1) {
        std::printf("  init=%d decode=%d count=%d found=%d\n", r, rd, rc, n);
        term();
        return false;
    }
    int qi[24] = {};
    int ri = info(qi, 0);
    // arkkep accepts: version (qi[2]) 2 or 3, ECC level (qi[3]) 0-3, qi[5] == 0, qi[12] > 0; data pointer at *qi[13]
    bool ok = ri == 0 && (qi[2] == 2 || qi[2] == 3) && qi[3] >= 0 && qi[3] <= 3 && qi[5] == 0 && qi[12] > 0;
    std::printf("  version=%d ecc=%d err=%d segments=%d\n", qi[2], qi[3], qi[5], qi[12]);
    free_info(qi);
    term();
    return ok;
}

}  // namespace

int wmain(int argc, wchar_t **argv) {
    if (argc < 4) {
        std::printf("usage: qrtest <modules dir> <otoca-camhook.dll> <card.png>...\n");
        return 2;
    }
    SetDllDirectoryW(argv[1]);  // the game's DLLs (QRDecode.dll needs pintl.dll from the same folder)
    HMODULE cam = LoadLibraryW(L"libcamera.dll");
    g_qr = LoadLibraryW(L"QRDecode.dll");
    if (!cam || !g_qr) {
        std::printf("load failed: libcamera=%p qr=%p (%lu)\n", cam, g_qr, GetLastError());
        return 1;
    }
    if (!LoadLibraryW(argv[2])) {
        std::printf("load failed: %ls (%lu)\n", argv[2], GetLastError());
        return 1;
    }
    auto run = reinterpret_cast<run_fn>(GetProcAddress(cam, "?LibCameraRun@@YA?AW4LIBCAMERA_STATUS@@H@Z"));
    auto get_image =
        reinterpret_cast<image_fn>(GetProcAddress(cam, "?LibCameraGetImage@@YA?AW4LIBCAMERA_STATUS@@HPAX@Z"));
    frame::Shared *shared = frame::open();
    if (!shared || run(0) != 0) {
        std::printf("shared frame or LibCameraRun failed\n");
        return 1;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    int failed = 0;
    std::vector<unsigned char> seen(frame::kSize);
    for (int i = 3; i < argc; i++) {
        Card card;
        const wchar_t *name = wcsrchr(argv[i], L'\\') ? wcsrchr(argv[i], L'\\') + 1 : argv[i];
        if (!load_card(argv[i], card) || card.frame.empty()) {
            std::printf("%ls: NG, %s\n", name, card.width ? "no QR code found" : "cannot read the image");
            failed++;
            continue;
        }
        std::memcpy(shared->pixels, card.frame.data(), frame::kSize);
        shared->show_until = GetTickCount() + 5000;
        get_image(0, seen.data());
        std::printf("%ls: QR at %ld,%ld-%ld,%ld\n", name, card.qr.left, card.qr.top, card.qr.right, card.qr.bottom);
        bool ok = decode(seen);
        // released: the camera must see nothing again
        shared->show_until = GetTickCount();
        get_image(0, seen.data());
        bool blank = true;
        for (unsigned char b : seen) blank &= b == 0xff;
        std::printf("  %s%s\n", ok ? "OK: arkkep would accept this QR" : "NG: arkkep would reject this QR",
                    blank ? "" : ", NG: frame still shown after release");
        failed += !ok || !blank;
    }
    std::printf(failed ? "%d FAILED\n" : "all OK\n", failed);
    return failed ? 1 : 0;
}
