// The camera frame otoca-scan hands otoca-camhook, in a named shared memory block.
#pragma once

#include <windows.h>
#include <sddl.h>

namespace frame {

constexpr int kWidth = 640, kHeight = 480, kSize = kWidth * kHeight * 3;

struct Shared {
    // GetTickCount() until which the game sees `pixels`. The scanner keeps pushing it forward while its button
    // is held, so a scanner that dies mid-scan takes the card away within half a second.
    volatile DWORD show_until;
    volatile DWORD last_read;     // GetTickCount() of the game's last camera read: tells the scanner the hook is alive
    unsigned char pixels[kSize];  // top-down BGR rows, as a camera delivers them
};

inline bool showing(const Shared *s) { return static_cast<LONG>(s->show_until - GetTickCount()) > 0; }

// Opens the block, creating it if the other side has not. Everyone in the session may write it
// (low integrity label): the game may run elevated and the scanner not, or the other way round.
inline Shared *open() {
    PSECURITY_DESCRIPTOR sd = nullptr;
    ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GA;;;WD)S:(ML;;NW;;;LW)", SDDL_REVISION_1, &sd,
                                                         nullptr);
    SECURITY_ATTRIBUTES sa{sizeof sa, sd, FALSE};
    HANDLE h = CreateFileMappingW(INVALID_HANDLE_VALUE, sd ? &sa : nullptr, PAGE_READWRITE, 0, sizeof(Shared),
                                  L"Local\\otoca-camhook-frame");
    LocalFree(sd);
    // the handle stays open for the life of the process, which keeps the block alive
    return h ? static_cast<Shared *>(MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared))) : nullptr;
}

}  // namespace frame
