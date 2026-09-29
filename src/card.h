// A printed card image (spice's printer_N.png) and the camera frame that shows its QR code.
#pragma once

#include <windows.h>

#include <vector>

struct Card {
    int width = 0, height = 0;
    std::vector<unsigned char> bgra;   // top-down 32-bit pixels, for display
    RECT qr{};                         // where the code is on the card (empty when none was found)
    std::vector<unsigned char> frame;  // frame::kSize camera frame (empty when no code was found)
};

// Reads the image with WIC (COM must be initialised). False when the file cannot be read as an image;
// true with an empty frame when there is no QR code in it.
bool load_card(const wchar_t *path, Card &card);
