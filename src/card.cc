// Finds the QR code on a printed card by its three finder patterns and makes the camera frame arkkep reads:
// the code black on white, scaled up in the middle of 640x480 (arkkep binarises the centre 360x360).
#include "card.h"

#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

#include "frame.h"

namespace {

using Microsoft::WRL::ComPtr;
using Found = std::vector<std::pair<double, double>>;  // (centre, module size)

constexpr int kFit = 340;  // longest side of the code with its quiet zone, in frame pixels

// Each 1:1:3:1:1 dark/light/dark/light/dark run group along a line of n dark flags, `step` apart.
Found pattern(const unsigned char *p, int n, int step) {
    struct Run {
        int start, len;
        bool dark;
    };
    std::vector<Run> r;
    for (int i = 0; i < n;) {
        int j = i;
        while (j < n && p[j * step] == p[i * step]) j++;
        r.push_back({i, j - i, p[i * step] != 0});
        i = j;
    }
    static const int ratio[5] = {1, 1, 3, 1, 1};
    Found out;
    for (size_t i = 0; i + 4 < r.size(); i++) {
        if (!r[i].dark) continue;
        int sum = 0;
        for (int k = 0; k < 5; k++) sum += r[i + k].len;
        double m = sum / 7.0;
        bool ok = m >= 1;
        for (int k = 0; k < 5 && ok; k++) ok = std::abs(r[i + k].len - ratio[k] * m) <= m * 0.6;
        if (ok) out.push_back({r[i + 2].start + r[i + 2].len / 2.0, m});
    }
    return out;
}

struct Hit {
    double x, y, m;
};

// Finder pattern centres: the row pattern must repeat in the column through its centre.
std::vector<Hit> finder_hits(const std::vector<unsigned char> &dark, int w, int h) {
    std::vector<Found> cols(w);
    std::vector<bool> scanned(w);
    std::vector<Hit> hits;
    for (int y = 0; y < h; y++) {
        for (auto [x, m] : pattern(&dark[static_cast<size_t>(y) * w], w, 1)) {
            int cx = static_cast<int>(x);
            if (!scanned[cx]) {
                cols[cx] = pattern(&dark[cx], h, w);
                scanned[cx] = true;
            }
            for (auto [cy, cm] : cols[cx]) {
                if (std::abs(cy - y) < m && std::abs(cm - m) < m * 0.4) {
                    hits.push_back({x, cy, (m + cm) / 2});
                    break;
                }
            }
        }
    }
    return hits;
}

// The code's box (x0, y0, x1, y1) and module size: three finder patterns of one size forming an L.
bool find_qr(const std::vector<Hit> &hits, double box[4], double &module) {
    struct Group {
        double x, y, m;  // sums, then means
        int n;
    };
    std::vector<Group> groups;
    for (const auto &h : hits) {
        auto g = std::find_if(groups.begin(), groups.end(), [&](const Group &g) {
            return std::abs(g.x / g.n - h.x) < 2 * h.m && std::abs(g.y / g.n - h.y) < 2 * h.m;
        });
        if (g == groups.end()) {
            groups.push_back({h.x, h.y, h.m, 1});
        } else {
            g->x += h.x;
            g->y += h.y;
            g->m += h.m;
            g->n++;
        }
    }
    std::vector<Group> c;
    for (const auto &g : groups)
        if (g.n >= 2) c.push_back({g.x / g.n, g.y / g.n, g.m / g.n, g.n});

    const Group *ba = nullptr, *bb = nullptr, *bd = nullptr;  // a = the L's corner (top left)
    double bm = 0;
    int best = 0;
    for (const auto &a : c) {
        for (const auto &b : c) {
            for (const auto &d : c) {
                if (&a == &b || &a == &d || &b == &d || !(b.x > a.x && d.y > a.y)) continue;
                double m = (a.m + b.m + d.m) / 3, ab = b.x - a.x, ad = d.y - a.y, size = ab / m + 7;
                if (std::max({a.m, b.m, d.m}) > 1.3 * std::min({a.m, b.m, d.m}) || std::abs(ab - ad) > 2 * m ||
                    std::abs(b.y - a.y) > 2 * m || std::abs(d.x - a.x) > 2 * m || size < 21 - 2 || size > 57 + 2)
                    continue;
                if (a.n + b.n + d.n > best) {
                    best = a.n + b.n + d.n;
                    ba = &a, bb = &b, bd = &d, bm = m;
                }
            }
        }
    }
    if (!ba) return false;
    // finder centres sit 3.5 modules in from the code's edges
    box[0] = ba->x - 3.5 * bm;
    box[1] = ba->y - 3.5 * bm;
    box[2] = bb->x + 3.5 * bm;
    box[3] = bd->y + 3.5 * bm;
    module = bm;
    return true;
}

// The code with a 4-module quiet zone, scaled (nearest neighbour) to kFit and centred in a white frame.
void make_frame(Card &card, const std::vector<unsigned char> &dark, const double box[4], double m) {
    const double q = 4 * m;
    const int bx = std::lround(box[0] - q), by = std::lround(box[1] - q);
    const int bw = std::lround(box[2] + q) - bx, bh = std::lround(box[3] + q) - by;
    // inside the quiet zone box, only the code itself is copied; the rest stays white
    const int ix0 = std::lround(q), iy0 = std::lround(q);
    const int ix1 = std::lround(q + box[2] - box[0]), iy1 = std::lround(q + box[3] - box[1]);
    const double scale = static_cast<double>(kFit) / std::max(bw, bh);
    const int sw = std::lround(bw * scale), sh = std::lround(bh * scale);
    const int ox = (frame::kWidth - sw) / 2, oy = (frame::kHeight - sh) / 2;
    card.frame.assign(frame::kSize, 0xff);
    for (int y = 0; y < sh; y++) {
        for (int x = 0; x < sw; x++) {
            int cx = static_cast<int>((x + 0.5) * bw / sw), cy = static_cast<int>((y + 0.5) * bh / sh);
            if (cx < ix0 || cx >= ix1 || cy < iy0 || cy >= iy1) continue;
            int px = bx + cx, py = by + cy;
            if (px < 0 || py < 0 || px >= card.width || py >= card.height ||
                !dark[static_cast<size_t>(py) * card.width + px])
                continue;
            std::memset(&card.frame[(static_cast<size_t>(oy + y) * frame::kWidth + ox + x) * 3], 0, 3);
        }
    }
    card.qr = {std::lround(box[0]), std::lround(box[1]), std::lround(box[2]), std::lround(box[3])};
}

bool read_image(const wchar_t *path, Card &card) {
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> image;
    ComPtr<IWICFormatConverter> bgra;
    UINT w = 0, h = 0;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) ||
        FAILED(wic->CreateDecoderFromFilename(path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                              &decoder)) ||
        FAILED(decoder->GetFrame(0, &image)) || FAILED(wic->CreateFormatConverter(&bgra)) ||
        FAILED(bgra->Initialize(image.Get(), GUID_WICPixelFormat32bppBGR, WICBitmapDitherTypeNone, nullptr, 0,
                                WICBitmapPaletteTypeCustom)) ||
        FAILED(bgra->GetSize(&w, &h)) || !w || !h)
        return false;
    card.bgra.resize(static_cast<size_t>(w) * h * 4);
    if (FAILED(bgra->CopyPixels(nullptr, w * 4, static_cast<UINT>(card.bgra.size()), card.bgra.data()))) return false;
    card.width = static_cast<int>(w);
    card.height = static_cast<int>(h);
    return true;
}

}  // namespace

bool load_card(const wchar_t *path, Card &card) {
    card = Card{};
    if (!read_image(path, card)) return false;
    // grey like Pillow's "L" (ITU-R 601-2), dark below 110
    std::vector<unsigned char> dark(static_cast<size_t>(card.width) * card.height);
    for (size_t i = 0; i < dark.size(); i++) {
        const unsigned char *p = &card.bgra[i * 4];
        dark[i] = ((p[2] * 19595 + p[1] * 38470 + p[0] * 7471 + 0x8000) >> 16) < 110;
    }
    double box[4], m;
    if (find_qr(finder_hits(dark, card.width, card.height), box, m)) make_frame(card, dark, box, m);
    return true;
}
