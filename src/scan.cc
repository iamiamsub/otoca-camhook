// otoca-scan: holds a printed card in front of the otoca d'or camera while its button is pressed.
//
// Lists spice's printer_N.png in the chosen folder (the game folder), finds the QR code on the selected card and,
// while the button (or the space key) is held, puts it in the shared frame otoca-camhook shows the game.
// Cards no longer wanted can be thrown away (to the Recycle Bin). The window is in Japanese or English.
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "card.h"
#include "frame.h"

namespace {

using Microsoft::WRL::ComPtr;

enum { ID_FOLDER = 1, ID_PICK, ID_LIST, ID_HOLD, ID_STATUS, ID_DISCARD, ID_LANG };
enum { TIMER_TICK = 1, TIMER_HOLD };
constexpr DWORD kHoldMs = 500;  // how long the game keeps seeing the card without a refresh from us

struct File {
    std::wstring name;
    FILETIME written;
};

HWND g_wnd, g_folder, g_pick, g_lang_button, g_list, g_hold, g_status, g_discard;
HFONT g_font, g_big_font;
UINT g_dpi = 96;
std::wstring g_dir, g_ini;
std::vector<File> g_files;  // as listed
Card g_card;
std::wstring g_card_name;  // selected file ("" = none)
FILETIME g_card_written{};
bool g_card_read;  // the selected file could be read as an image
frame::Shared *g_shared;
bool g_holding;
RECT g_preview{};
int g_ticks;

int px(int dip) { return MulDiv(dip, g_dpi, 96); }

// Window texts in Japanese and English. %s is a card's file name, %lu a number of seconds.
enum Text {
    T_TITLE, T_FOLDER, T_LANG, T_DISCARD, T_HOLD, T_HOLDING, T_PICK_TITLE, T_NO_DIR, T_NO_FILES, T_SELECT,
    T_UNREADABLE, T_NO_QR, T_SHOWING, T_READY, T_NO_SHARED, T_CAM_NEVER, T_CAM_NOW, T_CAM_AGO, T_COUNT
};
enum Lang { JA, EN };
const wchar_t *const kText[2][T_COUNT] = {
    {
        L"otoca-scan - カードをかざす",
        L"フォルダ…",
        L"English",  // the button names the other language
        L"このカードを捨てる（ごみ箱へ）",
        L"押している間 カードをかざす",
        L"かざしています…",
        L"printer_N.png があるフォルダ（ゲームのフォルダ）",
        L"「フォルダ…」で、spice が printer_N.png を書き出すフォルダ（ゲームのフォルダ）を選んでください",
        L"このフォルダには printer_N.png がありません",
        L"読ませるカードを選んでください",
        L"%s: 画像を読めません",
        L"%s: QR コードが見つかりません",
        L"%s をカメラにかざしています",
        L"%s: ボタン（またはスペースキー）を押している間だけカメラに映ります",
        L"共有メモリを開けません",
        L"ゲームのカメラ: まだ読み取りがありません（spice の -k に otoca-camhook.dll が要ります）",
        L"ゲームのカメラ: 読み取り中",
        L"ゲームのカメラ: 最後の読み取りは %lu 秒前",
    },
    {
        L"otoca-scan - hold a card to the camera",
        L"Folder…",
        L"日本語",
        L"Throw this card away (Recycle Bin)",
        L"Hold to show the card",
        L"Showing the card…",
        L"Folder with printer_N.png (the game folder)",
        L"Use \"Folder…\" to choose the folder spice writes printer_N.png to (the game folder)",
        L"No printer_N.png in this folder",
        L"Select the card to scan",
        L"%s: cannot read the image",
        L"%s: no QR code found",
        L"Showing %s to the camera",
        L"%s: shown to the camera only while the button (or the space key) is held",
        L"Cannot open the shared memory",
        L"Game camera: not read yet (spice needs otoca-camhook.dll in -k)",
        L"Game camera: reading",
        L"Game camera: last read %lu s ago",
    },
};
Lang g_lang;

const wchar_t *tr(Text t) { return kText[g_lang][t]; }

template <typename... Args>
std::wstring trf(Text t, Args... args) {
    wchar_t buf[512];
    swprintf_s(buf, tr(t), args...);
    return buf;
}

void set_text(HWND h, const std::wstring &text) {
    wchar_t now[512];
    GetWindowTextW(h, now, 512);
    if (text != now) SetWindowTextW(h, text.c_str());
}

void update_status() {
    const wchar_t *name = g_card_name.c_str();
    std::wstring s;
    if (g_dir.empty())
        s = tr(T_NO_DIR);
    else if (g_card_name.empty())
        s = tr(g_files.empty() ? T_NO_FILES : T_SELECT);
    else if (!g_card_read)
        s = trf(T_UNREADABLE, name);
    else if (g_card.frame.empty())
        s = trf(T_NO_QR, name);
    else
        s = trf(g_holding ? T_SHOWING : T_READY, name);

    s += L"\r\n";
    if (!g_shared) {
        s += tr(T_NO_SHARED);
    } else if (!g_shared->last_read) {
        s += tr(T_CAM_NEVER);
    } else {
        DWORD age = GetTickCount() - g_shared->last_read;
        s += age < 1000 ? tr(T_CAM_NOW) : trf(T_CAM_AGO, age / 1000);
    }
    set_text(g_status, s);
    set_text(g_hold, tr(g_holding ? T_HOLDING : T_HOLD));
}

// Puts the texts of the current language on the window.
void apply_language() {
    SetWindowTextW(g_wnd, tr(T_TITLE));
    SetWindowTextW(g_pick, tr(T_FOLDER));
    SetWindowTextW(g_lang_button, tr(T_LANG));
    SetWindowTextW(g_discard, tr(T_DISCARD));
    update_status();
}

void toggle_language() {
    g_lang = g_lang == JA ? EN : JA;
    WritePrivateProfileStringW(L"otoca-scan", L"lang", g_lang == JA ? L"ja" : L"en", g_ini.c_str());
    apply_language();
}

void start_hold() {
    if (g_holding || g_card.frame.empty() || !g_shared) return;
    std::memcpy(g_shared->pixels, g_card.frame.data(), frame::kSize);
    InterlockedExchange(reinterpret_cast<volatile LONG *>(&g_shared->show_until),
                        static_cast<LONG>(GetTickCount() + kHoldMs));
    g_holding = true;
    SetTimer(g_wnd, TIMER_HOLD, 100, nullptr);
    update_status();
    InvalidateRect(g_wnd, &g_preview, FALSE);
}

void stop_hold() {
    if (!g_holding) return;
    g_holding = false;
    KillTimer(g_wnd, TIMER_HOLD);
    InterlockedExchange(reinterpret_cast<volatile LONG *>(&g_shared->show_until), static_cast<LONG>(GetTickCount()));
    update_status();
    InvalidateRect(g_wnd, &g_preview, FALSE);
}

void load_selected(const File &f) {
    g_card_name = f.name;
    g_card_written = f.written;
    g_card_read = load_card((g_dir + L"\\" + f.name).c_str(), g_card);
    EnableWindow(g_hold, !g_card.frame.empty());
    EnableWindow(g_discard, TRUE);
    update_status();
    InvalidateRect(g_wnd, &g_preview, FALSE);
}

void clear_selected() {
    g_card_name.clear();
    g_card = Card{};
    g_card_read = false;
    EnableWindow(g_hold, FALSE);
    EnableWindow(g_discard, FALSE);
    update_status();
    InvalidateRect(g_wnd, &g_preview, FALSE);
}

std::vector<File> list_files() {
    std::vector<File> out;
    if (g_dir.empty()) return out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((g_dir + L"\\printer_*.png").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do out.push_back({fd.cFileName, fd.ftLastWriteTime});
    while (FindNextFileW(h, &fd));
    FindClose(h);
    // newest first. Not by number alone: spice takes the lowest free number, so a print after a card was
    // thrown away fills the gap.
    auto number = [](const File &f) { return _wtoi(f.name.c_str() + 8); };  // after "printer_"
    std::sort(out.begin(), out.end(), [&](const File &a, const File &b) {
        LONG t = CompareFileTime(&a.written, &b.written);
        return t ? t > 0 : number(a) > number(b);
    });
    return out;
}

// Picks up new prints, and re-reads the selected card if spice rewrote it (or was still writing it).
void refresh_list() {
    auto files = list_files();
    bool same = std::equal(files.begin(), files.end(), g_files.begin(), g_files.end(), [](auto &a, auto &b) {
        return a.name == b.name && CompareFileTime(&a.written, &b.written) == 0;
    });
    if (same) return;
    g_files = std::move(files);
    SendMessageW(g_list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g_list, LB_RESETCONTENT, 0, 0);
    int sel = -1;
    for (size_t i = 0; i < g_files.size(); i++) {
        FILETIME local;
        SYSTEMTIME st;
        FileTimeToLocalFileTime(&g_files[i].written, &local);
        FileTimeToSystemTime(&local, &st);
        wchar_t text[MAX_PATH + 32];
        swprintf_s(text, L"%s    %02u/%02u %02u:%02u", g_files[i].name.c_str(), st.wMonth, st.wDay, st.wHour,
                   st.wMinute);
        SendMessageW(g_list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
        if (g_files[i].name == g_card_name) sel = static_cast<int>(i);
    }
    SendMessageW(g_list, LB_SETCURSEL, sel, 0);
    SendMessageW(g_list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_list, nullptr, TRUE);
    if (sel < 0 && !g_card_name.empty()) {
        stop_hold();
        clear_selected();
    } else if (sel >= 0 && !g_holding && CompareFileTime(&g_files[sel].written, &g_card_written) != 0) {
        load_selected(g_files[sel]);
    }
    update_status();
}

void set_dir(const std::wstring &dir) {
    stop_hold();
    g_dir = dir;
    WritePrivateProfileStringW(L"otoca-scan", L"folder", dir.c_str(), g_ini.c_str());
    SetWindowTextW(g_folder, dir.c_str());
    g_files.clear();
    SendMessageW(g_list, LB_RESETCONTENT, 0, 0);
    clear_selected();
    refresh_list();
}

// Sends the selected card to the Recycle Bin (Windows asks before deleting anything it cannot recycle) and
// selects the one that takes its place in the list.
void discard_selected() {
    int sel = static_cast<int>(SendMessageW(g_list, LB_GETCURSEL, 0, 0));
    if (sel < 0 || sel >= static_cast<int>(g_files.size())) return;
    stop_hold();
    ComPtr<IFileOperation> op;
    ComPtr<IShellItem> item;
    if (FAILED(CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&op))) ||
        FAILED(SHCreateItemFromParsingName((g_dir + L"\\" + g_files[sel].name).c_str(), nullptr,
                                           IID_PPV_ARGS(&item))) ||
        FAILED(op->SetOwnerWindow(g_wnd)) ||
        FAILED(op->SetOperationFlags(FOF_ALLOWUNDO | FOFX_RECYCLEONDELETE | FOF_SILENT)) ||
        FAILED(op->DeleteItem(item.Get(), nullptr)) || FAILED(op->PerformOperations()))
        return;
    refresh_list();
    const int n = static_cast<int>(g_files.size());
    if (g_card_name.empty() && n > 0) {
        sel = std::min(sel, n - 1);
        SendMessageW(g_list, LB_SETCURSEL, sel, 0);
        load_selected(g_files[sel]);
    }
}

void pick_folder() {
    ComPtr<IFileOpenDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return;
    DWORD opt = 0;
    dlg->GetOptions(&opt);
    dlg->SetOptions(opt | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dlg->SetTitle(tr(T_PICK_TITLE));
    ComPtr<IShellItem> cur, item;
    if (!g_dir.empty() && SUCCEEDED(SHCreateItemFromParsingName(g_dir.c_str(), nullptr, IID_PPV_ARGS(&cur))))
        dlg->SetFolder(cur.Get());
    PWSTR path = nullptr;
    if (FAILED(dlg->Show(g_wnd)) || FAILED(dlg->GetResult(&item)) ||
        FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
        return;
    set_dir(path);
    CoTaskMemFree(path);
}

void paint_preview(HDC dc) {
    RECT r = g_preview;
    HBRUSH edge = CreateSolidBrush(g_holding ? RGB(40, 170, 80) : GetSysColor(COLOR_BTNSHADOW));
    FillRect(dc, &r, edge);
    DeleteObject(edge);
    InflateRect(&r, -px(4), -px(4));
    FillRect(dc, &r, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    if (!g_card_read || r.right <= r.left || r.bottom <= r.top) return;
    double s = std::min(static_cast<double>(r.right - r.left) / g_card.width,
                        static_cast<double>(r.bottom - r.top) / g_card.height);
    int w = static_cast<int>(g_card.width * s), h = static_cast<int>(g_card.height * s);
    int x = r.left + (r.right - r.left - w) / 2, y = r.top + (r.bottom - r.top - h) / 2;
    BITMAPINFO bi{};
    bi.bmiHeader = {sizeof(BITMAPINFOHEADER), g_card.width, -g_card.height, 1, 32, BI_RGB};
    SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, nullptr);
    StretchDIBits(dc, x, y, w, h, 0, 0, g_card.width, g_card.height, g_card.bgra.data(), &bi, DIB_RGB_COLORS,
                  SRCCOPY);
    if (IsRectEmpty(&g_card.qr)) return;
    // the part the camera will see
    RECT q{x + static_cast<int>(g_card.qr.left * s), y + static_cast<int>(g_card.qr.top * s),
           x + static_cast<int>(g_card.qr.right * s), y + static_cast<int>(g_card.qr.bottom * s)};
    HBRUSH red = CreateSolidBrush(RGB(230, 40, 40));
    for (int i = 0; i < px(2); i++, InflateRect(&q, 1, 1)) FrameRect(dc, &q, red);
    DeleteObject(red);
}

void make_fonts() {
    if (g_font) DeleteObject(g_font);
    if (g_big_font) DeleteObject(g_big_font);
    NONCLIENTMETRICSW ncm{sizeof ncm};
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0, g_dpi);
    g_font = CreateFontIndirectW(&ncm.lfMessageFont);
    ncm.lfMessageFont.lfHeight = ncm.lfMessageFont.lfHeight * 3 / 2;
    ncm.lfMessageFont.lfWeight = FW_BOLD;
    g_big_font = CreateFontIndirectW(&ncm.lfMessageFont);
    for (HWND h : {g_folder, g_pick, g_lang_button, g_list, g_status, g_discard})
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
    SendMessageW(g_hold, WM_SETFONT, reinterpret_cast<WPARAM>(g_big_font), TRUE);
}

void layout() {
    RECT c;
    GetClientRect(g_wnd, &c);
    const int m = px(12), gap = px(8), row = px(28), pick = px(96), list = px(260), status = px(44),
              hold = px(64);
    const int w = c.right, h = c.bottom;
    const int top = m + row + gap, hold_y = h - m - hold, status_y = hold_y - gap - status;
    const int bottom = status_y - gap;
    MoveWindow(g_folder, m, m, w - 2 * m - 2 * (gap + pick), row, TRUE);
    MoveWindow(g_pick, w - m - 2 * pick - gap, m, pick, row, TRUE);
    MoveWindow(g_lang_button, w - m - pick, m, pick, row, TRUE);
    MoveWindow(g_list, m, top, list, bottom - top - row - gap, TRUE);
    MoveWindow(g_discard, m, bottom - row, list, row, TRUE);
    MoveWindow(g_status, m, status_y, w - 2 * m, status, TRUE);
    MoveWindow(g_hold, m, hold_y, w - 2 * m, hold, TRUE);
    g_preview = {m + list + gap, top, w - m, bottom};
    InvalidateRect(g_wnd, nullptr, TRUE);
}

// The hold button: pressed on mouse down, released when it lets go of the mouse (button up, or capture lost).
LRESULT CALLBACK hold_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) start_hold();
    if (msg == WM_CAPTURECHANGED) stop_hold();
    return DefSubclassProc(h, msg, wp, lp);
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_wnd = hwnd;
        g_dpi = GetDpiForWindow(hwnd);
        auto inst = reinterpret_cast<CREATESTRUCTW *>(lp)->hInstance;
        auto child = [&](const wchar_t *cls, const wchar_t *text, DWORD style, int id, DWORD ex = 0) {
            return CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, hwnd,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
        };
        g_folder = child(L"EDIT", L"", ES_READONLY | ES_AUTOHSCROLL | WS_TABSTOP, ID_FOLDER, WS_EX_CLIENTEDGE);
        g_pick = child(L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, ID_PICK);
        g_lang_button = child(L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, ID_LANG);
        g_list = child(L"LISTBOX", L"", LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_TABSTOP, ID_LIST,
                       WS_EX_CLIENTEDGE);
        g_status = child(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, ID_STATUS);
        g_discard = child(L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP | WS_DISABLED, ID_DISCARD);
        g_hold = child(L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP | WS_DISABLED, ID_HOLD);
        SetWindowSubclass(g_hold, hold_proc, 0, 0);
        make_fonts();
        layout();
        SetWindowTextW(g_folder, g_dir.c_str());
        refresh_list();
        apply_language();
        SetTimer(hwnd, TIMER_TICK, 250, nullptr);
        return 0;
    }
    case WM_SIZE:
        layout();
        return 0;
    case WM_GETMINMAXINFO: {
        auto mm = reinterpret_cast<MINMAXINFO *>(lp);
        mm->ptMinTrackSize = {px(560), px(420)};
        return 0;
    }
    case WM_DPICHANGED: {
        g_dpi = HIWORD(wp);
        make_fonts();
        auto r = reinterpret_cast<RECT *>(lp);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        layout();
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == ID_PICK && HIWORD(wp) == BN_CLICKED) pick_folder();
        if (LOWORD(wp) == ID_DISCARD && HIWORD(wp) == BN_CLICKED) discard_selected();
        if (LOWORD(wp) == ID_LANG && HIWORD(wp) == BN_CLICKED) toggle_language();
        if (LOWORD(wp) == ID_LIST && HIWORD(wp) == LBN_SELCHANGE) {
            int sel = static_cast<int>(SendMessageW(g_list, LB_GETCURSEL, 0, 0));
            if (sel >= 0 && sel < static_cast<int>(g_files.size())) load_selected(g_files[sel]);
        }
        return 0;
    case WM_TIMER:
        if (wp == TIMER_HOLD && g_holding)
            InterlockedExchange(reinterpret_cast<volatile LONG *>(&g_shared->show_until),
                                static_cast<LONG>(GetTickCount() + kHoldMs));
        if (wp == TIMER_TICK) {
            if (++g_ticks % 4 == 0) refresh_list();
            update_status();
        }
        return 0;
    case WM_ACTIVATEAPP:
        if (!wp) stop_hold();  // a space key-up would go to another window
        return 0;
    case WM_CTLCOLORSTATIC:
        // the status line on the window background
        if (reinterpret_cast<HWND>(lp) == g_status) {
            SetBkMode(reinterpret_cast<HDC>(wp), TRANSPARENT);
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        paint_preview(dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        stop_hold();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    g_shared = frame::open();

    // settings next to the exe; a new file starts with a BOM so the profile API keeps it UTF-16
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    g_ini = exe;
    g_ini = g_ini.substr(0, g_ini.find_last_of(L'\\') + 1) + L"otoca-scan.ini";
    HANDLE f = CreateFileW(g_ini.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD n;
        WriteFile(f, "\xff\xfe", 2, &n, nullptr);
        CloseHandle(f);
    }
    wchar_t dir[MAX_PATH] = L"";
    GetPrivateProfileStringW(L"otoca-scan", L"folder", L"", dir, MAX_PATH, g_ini.c_str());
    g_dir = dir;
    // language: the saved choice, otherwise Japanese on a Japanese Windows and English elsewhere
    wchar_t lang[8] = L"";
    GetPrivateProfileStringW(L"otoca-scan", L"lang", L"", lang, 8, g_ini.c_str());
    if (!wcscmp(lang, L"ja") || !wcscmp(lang, L"en"))
        g_lang = !wcscmp(lang, L"ja") ? JA : EN;
    else
        g_lang = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_JAPANESE ? JA : EN;

    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"otoca-scan";
    RegisterClassExW(&wc);
    g_dpi = GetDpiForSystem();
    RECT r{0, 0, px(780), px(620)};
    AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, g_dpi);
    CreateWindowExW(WS_EX_CONTROLPARENT, L"otoca-scan", tr(T_TITLE), WS_OVERLAPPEDWINDOW,
                    CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst,
                    nullptr);
    ShowWindow(g_wnd, show);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        // space holds the card from anywhere in the window, like the button
        if ((msg.message == WM_KEYDOWN || msg.message == WM_KEYUP) && msg.wParam == VK_SPACE) {
            if (msg.message == WM_KEYUP)
                stop_hold();
            else if (!(msg.lParam & (1 << 30)))  // not auto-repeat
                start_hold();
            continue;
        }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_DELETE && msg.hwnd == g_list) {
            discard_selected();
            continue;
        }
        if (!IsDialogMessageW(g_wnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return 0;
}
