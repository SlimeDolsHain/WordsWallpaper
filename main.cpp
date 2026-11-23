// WordsWallpaper - a word wallpaper generator.
// Copyright (C) 2026 @SlimeDolsHain
// SPDX-License-Identifier: AGPL-3.0-only

// ============================================================================
//  Words Wallpaper  -  native Win32 rewrite
//  Single-file Windows GUI: word list -> JPEG wallpaper (Microsoft YaHei Bold)
//  Build: see build.ps1   (MinGW-w64 g++, static runtime)
// ============================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <gdiplus.h>

#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <new>

using namespace Gdiplus;


// ---------------------------------------------------------------------------
//  Application constants
// ---------------------------------------------------------------------------
#define IDI_APP 101
#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

static const wchar_t* kAppTitle        = L"单词壁纸生成器";
static const wchar_t* kMainClass       = L"WWMainWindow";
static const wchar_t* kPanelClass      = L"WWWordPanel";
static const wchar_t* kPreviewClass    = L"WWPreview";
static const wchar_t* kButtonClass     = L"WWButton";
static const wchar_t* kNumberClass     = L"WWNumber";
static const wchar_t* kAboutClass      = L"WWAbout";

static const int kDefW = 1160, kDefH = 800;      // logical (96 dpi) window size
static const int kMinW = 1000, kMinH = 680;
static const int kFontMin = 8, kFontMax = 2000;
static const int kPosMin = 0, kPosMax = 16384;
static const int kResMin = 16, kResMax = 8192;
static const int kMaxPixels = 40 * 1000 * 1000; // refuse absurd memory use
static const int kJpegQuality = 50;             // keep original behaviour

enum ControlId {
    IDC_ADD_ROW       = 1001,
    IDC_FONT_SIZE     = 1002,
    IDC_POS_X         = 1003,
    IDC_POS_Y         = 1004,
    IDC_FONT_COLOR    = 1005,
    IDC_BG_COLOR      = 1006,
    IDC_PRESET        = 1007,
    IDC_RES_W         = 1008,
    IDC_RES_H         = 1009,
    IDC_THEME_DARK    = 1010,
    IDC_THEME_LIGHT   = 1011,
    IDC_BROWSE        = 1012,
    IDC_OPEN_DIR      = 1013,
    IDC_SAVE_ONLY     = 1014,
    IDC_APPLY         = 1015,
    IDC_WORD_PANEL    = 1016,
    IDC_PREVIEW       = 1017,
    IDC_IMPORT        = 1018,
    IDC_ABOUT         = 1019,
    IDC_ABOUT_GITHUB  = 1020,
    IDC_ABOUT_BILIBILI= 1021,
    IDC_ABOUT_SPONSOR = 1022,
    IDC_ABOUT_CLOSE   = 1023
};

enum CustomMessage {
    WM_APP_NUMCHANGE   = WM_APP + 11,  // wParam: control id, lParam: HWND
    WM_APP_ROWENTER    = WM_APP + 12,
    WM_APP_PANELWHEEL  = WM_APP + 13,
    WM_APP_INITFOCUS   = WM_APP + 14
};

enum TimerId {
    TIMER_PREVIEW = 1,
    TIMER_SAVE    = 2
};

// Number box protocol
#define NB_SETVALUE  (WM_APP + 40)
#define NB_GETVALUE  (WM_APP + 41)
#define NB_SETRANGE  (WM_APP + 42)
#define NB_STEP      (WM_APP + 43)
#define NB_GETEDIT   (WM_APP + 44)
#define NB_SETSTEP   (WM_APP + 45)
#define NB_SETFOCUS_NOTIFY (WM_APP + 46)

// Button styles
enum ButtonStyle {
    BTN_NORMAL = 0,
    BTN_PRIMARY,
    BTN_GHOST,
    BTN_TOGGLE,
    BTN_DANGER,
    BTN_COLOR,
    BTN_SMALL,
    BTN_SELECT
};

struct App;
static App* g_app = nullptr;
static HINSTANCE g_hInst = nullptr;
static ULONG_PTR g_gdiplusToken = 0;
static CLSID g_jpegClsid = {};
static bool g_jpegClsidValid = false;
static std::wstring g_imageFace = L"Microsoft YaHei UI";
static std::wstring g_uiFace = L"Microsoft YaHei UI";
static std::wstring g_forcedFontFile;
static bool g_privateFontLoaded = false;
static bool g_demoMode = false;
static std::wstring g_startupImportPath;
static HBRUSH g_mainClassBrush = nullptr;

// ---------------------------------------------------------------------------
//  Tiny helpers
// ---------------------------------------------------------------------------
static int ClampInt(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static COLORREF Rgb(int r, int g, int b) {
    return RGB(ClampInt(r, 0, 255), ClampInt(g, 0, 255), ClampInt(b, 0, 255));
}

static std::wstring Fmt(const wchar_t* fmt, ...) {
    wchar_t buf[2048];
    va_list ap;
    va_start(ap, fmt);
    int n = vswprintf(buf, 2048, fmt, ap);
    va_end(ap);
    if (n < 0) {
        buf[2047] = 0;
    }
    return std::wstring(buf);
}

static std::wstring TrimW(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && iswspace(s[a])) ++a;
    while (b > a && iswspace(s[b - 1])) --b;
    return s.substr(a, b - a);
}

static std::wstring LowerW(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}

static std::wstring ColorToHex(COLORREF c) {
    wchar_t buf[16];
    swprintf(buf, 16, L"#%02x%02x%02x", GetRValue(c), GetGValue(c), GetBValue(c));
    return buf;
}

static COLORREF HexToColor(const std::wstring& in, COLORREF def) {
    std::wstring s = TrimW(in);
    if (!s.empty() && s[0] == L'#') s.erase(0, 1);
    if (s.size() != 6) return def;
    for (wchar_t c : s) {
        if (!iswxdigit(c)) return def;
    }
    unsigned int v = (unsigned int)wcstoul(s.c_str(), nullptr, 16);
    return RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

static COLORREF Blend(COLORREF a, COLORREF b, int percentB) {
    int pb = ClampInt(percentB, 0, 100);
    int pa = 100 - pb;
    return RGB((GetRValue(a) * pa + GetRValue(b) * pb) / 100,
               (GetGValue(a) * pa + GetGValue(b) * pb) / 100,
               (GetBValue(a) * pa + GetBValue(b) * pb) / 100);
}

static std::wstring ExeDir() {
    wchar_t path[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, path, (DWORD)(sizeof(path) / sizeof(path[0])));
    std::wstring s(path, n);
    size_t p = s.find_last_of(L"\\/");
    if (p == std::wstring::npos) return L".";
    return s.substr(0, p);
}

static std::wstring JoinPath(const std::wstring& dir, const std::wstring& name) {
    if (dir.empty()) return name;
    if (dir.back() == L'\\' || dir.back() == L'/') return dir + name;
    return dir + L"\\" + name;
}

static std::wstring GetFileNamePart(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? path : path.substr(p + 1);
}

static bool FileExists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES;
}

static bool DirExists(const std::wstring& path) {
    if (path.empty()) return false;
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static std::wstring FormatBytes(unsigned long long bytes) {
    wchar_t buf[64];
    if (bytes < 1024) {
        return std::to_wstring(bytes) + L" B";
    } else if (bytes < 1024ull * 1024ull) {
        swprintf(buf, 64, L"%.1f KB", (double)bytes / 1024.0);
    } else {
        swprintf(buf, 64, L"%.1f MB", (double)bytes / (1024.0 * 1024.0));
    }
    return buf;
}

static std::wstring ReadTextFile(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return L"";
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size) || size.QuadPart <= 0 || size.QuadPart > 4 * 1024 * 1024) {
        CloseHandle(h);
        return L"";
    }
    std::string data;
    data.resize((size_t)size.QuadPart);
    DWORD read = 0;
    BOOL ok = ReadFile(h, &data[0], (DWORD)data.size(), &read, nullptr);
    CloseHandle(h);
    if (!ok) return L"";
    data.resize(read);
    if (data.size() >= 2 && (unsigned char)data[0] == 0xFF && (unsigned char)data[1] == 0xFE) {
        std::wstring out;
        out.resize((data.size() - 2) / 2);
        memcpy(&out[0], data.data() + 2, out.size() * 2);
        return out;
    }
    size_t offset = 0;
    if (data.size() >= 3 && (unsigned char)data[0] == 0xEF &&
        (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF) {
        offset = 3;
    }
    int need = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data.data() + offset,
                                   (int)(data.size() - offset), nullptr, 0);
    UINT cp = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (need <= 0) {
        cp = CP_ACP;
        flags = 0;
        need = MultiByteToWideChar(cp, flags, data.data() + offset,
                                   (int)(data.size() - offset), nullptr, 0);
        if (need <= 0) return L"";
    }
    std::wstring out;
    out.resize((size_t)need);
    MultiByteToWideChar(cp, flags, data.data() + offset, (int)(data.size() - offset),
                        &out[0], need);
    return out;
}

static bool WriteTextFileUtf8(const std::wstring& path, const std::wstring& text) {
    int need = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(),
                                   nullptr, 0, nullptr, nullptr);
    if (need < 0) return false;
    std::string bytes;
    bytes.resize((size_t)need);
    if (need > 0) {
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(),
                            &bytes[0], need, nullptr, nullptr);
    }
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(h, bytes.data(), (DWORD)bytes.size(), &written, nullptr);
    CloseHandle(h);
    return ok && written == (DWORD)bytes.size();
}

static std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return std::string();
    int need = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(),
                                   nullptr, 0, nullptr, nullptr);
    if (need <= 0) return std::string();
    std::string out((size_t)need, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(),
                        &out[0], need, nullptr, nullptr);
    return out;
}

static bool Utf8ToWide(const std::string& s, std::wstring& out) {
    out.clear();
    if (s.empty()) return true;
    int need = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                   s.data(), (int)s.size(), nullptr, 0);
    if (need <= 0) return false;
    out.resize((size_t)need);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(),
                        &out[0], need);
    return true;
}

static bool ReadAllBytes(const std::wstring& path, std::vector<BYTE>& out) {
    out.clear();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size) || size.QuadPart < 0 ||
        size.QuadPart > 256ll * 1024 * 1024) {
        CloseHandle(h);
        return false;
    }
    out.resize((size_t)size.QuadPart);
    DWORD total = 0;
    while (total < out.size()) {
        DWORD chunk = (DWORD)std::min<size_t>(out.size() - total, 1u << 20);
        DWORD got = 0;
        if (!ReadFile(h, out.data() + total, chunk, &got, nullptr) || got == 0) {
            CloseHandle(h);
            out.clear();
            return false;
        }
        total += got;
    }
    CloseHandle(h);
    return true;
}

static bool WriteBytesToFile(const std::wstring& path, const std::vector<BYTE>& data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD total = 0;
    while (total < data.size()) {
        DWORD chunk = (DWORD)std::min<size_t>(data.size() - total, 1u << 20);
        DWORD wrote = 0;
        if (!WriteFile(h, data.data() + total, chunk, &wrote, nullptr) || wrote == 0) {
            CloseHandle(h);
            return false;
        }
        total += wrote;
    }
    FlushFileBuffers(h);
    CloseHandle(h);
    return true;
}

// Write through a sibling temp file so a failed metadata update never destroys
// the freshly encoded JPEG.
static bool WriteAllBytesAtomic(const std::wstring& path, const std::vector<BYTE>& data) {
    std::wstring temp = path + L".wwtmp";
    if (!WriteBytesToFile(temp, data)) {
        DeleteFileW(temp.c_str());
        return false;
    }
    if (!MoveFileExW(temp.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp.c_str());
        return false;
    }
    return true;
}

static unsigned long long FileSizeBytes(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) return 0;
    return ((unsigned long long)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
}

// ---------------------------------------------------------------------------
//  Settings + theme
// ---------------------------------------------------------------------------
struct Settings {
    COLORREF fontColor = RGB(255, 255, 255);
    COLORREF bgColor = RGB(0, 0, 0);
    int fontSize = 100;
    int posX = 200;
    int posY = 350;
    int resW = 1920;
    int resH = 1080;
    std::wstring addr;
    bool dark = true;
    bool hasConfig = false;
    bool maximized = false;
    int winW = kDefW;
    int winH = kDefH;
};

struct Palette {
    bool dark = true;
    COLORREF bg = 0, panel = 0, input = 0, border = 0, text = 0, muted = 0;
    COLORREF accent = 0, accentSoft = 0;
    COLORREF btnBg = 0, btnHover = 0, btnPressed = 0;
    COLORREF primary = 0, primaryHover = 0, primaryPressed = 0;
    COLORREF danger = 0, dangerHover = 0;
    COLORREF previewBg = 0, statusBg = 0, rowActive = 0, scrollThumb = 0;
};

static Palette MakePalette(bool dark) {
    Palette p;
    p.dark = dark;
    if (dark) {
        p.bg          = Rgb(27, 28, 30);
        p.panel       = Rgb(34, 35, 38);
        p.input       = Rgb(42, 44, 48);
        p.border      = Rgb(60, 63, 68);
        p.text        = Rgb(230, 228, 223);
        p.muted       = Rgb(154, 154, 146);
        p.accent      = Rgb(122, 166, 203);
        p.accentSoft  = Rgb(50, 65, 78);
        p.btnBg       = Rgb(45, 47, 51);
        p.btnHover    = Rgb(55, 58, 63);
        p.btnPressed  = Rgb(66, 70, 76);
        p.primary     = Rgb(62, 108, 147);
        p.primaryHover= Rgb(74, 124, 166);
        p.primaryPressed = Rgb(51, 92, 127);
        p.danger      = Rgb(170, 74, 74);
        p.dangerHover = Rgb(196, 88, 88);
        p.previewBg   = Rgb(18, 19, 20);
        p.statusBg    = Rgb(30, 31, 34);
        p.rowActive   = Rgb(44, 49, 54);
        p.scrollThumb = Rgb(92, 96, 102);
    } else {
        p.bg          = Rgb(243, 236, 224);
        p.panel       = Rgb(250, 245, 235);
        p.input       = Rgb(255, 252, 245);
        p.border      = Rgb(214, 201, 178);
        p.text        = Rgb(59, 52, 42);
        p.muted       = Rgb(132, 119, 99);
        p.accent      = Rgb(158, 102, 55);
        p.accentSoft  = Rgb(233, 219, 198);
        p.btnBg       = Rgb(237, 228, 211);
        p.btnHover    = Rgb(229, 218, 197);
        p.btnPressed  = Rgb(219, 206, 183);
        p.primary     = Rgb(158, 102, 55);
        p.primaryHover= Rgb(176, 118, 68);
        p.primaryPressed = Rgb(137, 85, 43);
        p.danger      = Rgb(178, 74, 63);
        p.dangerHover = Rgb(200, 92, 78);
        p.previewBg   = Rgb(232, 223, 207);
        p.statusBg    = Rgb(238, 230, 216);
        p.rowActive   = Rgb(241, 232, 216);
        p.scrollThumb = Rgb(178, 165, 143);
    }
    return p;
}

static std::wstring ConfigPathForApp() {
    return JoinPath(ExeDir(), L"config.ini");
}

static void LoadSettings(Settings& s, const std::wstring& path) {
    std::wstring text = ReadTextFile(path);
    s.hasConfig = !text.empty();
    std::wstring section;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find_first_of(L"\r\n", pos);
        if (end == std::wstring::npos) end = text.size();
        std::wstring line = TrimW(text.substr(pos, end - pos));
        pos = end + 1;
        if (line.empty() || line[0] == L';' || line[0] == L'#') continue;
        if (line.front() == L'[' && line.back() == L']') {
            section = LowerW(TrimW(line.substr(1, line.size() - 2)));
            continue;
        }
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring key = LowerW(TrimW(line.substr(0, eq)));
        std::wstring value = TrimW(line.substr(eq + 1));
        auto asInt = [&](int def) -> int {
            if (value.empty()) return def;
            wchar_t* stop = nullptr;
            long v = wcstol(value.c_str(), &stop, 10);
            if (stop == value.c_str()) return def;
            return (int)v;
        };
        if (section == L"color") {
            if (key == L"font_color") s.fontColor = HexToColor(value, s.fontColor);
            else if (key == L"bg_color") s.bgColor = HexToColor(value, s.bgColor);
        } else if (section == L"location") {
            if (key == L"value_w") s.posX = ClampInt(asInt(s.posX), kPosMin, kPosMax);
            else if (key == L"value_h") s.posY = ClampInt(asInt(s.posY), kPosMin, kPosMax);
        } else if (section == L"font") {
            if (key == L"size") s.fontSize = ClampInt(asInt(s.fontSize), kFontMin, kFontMax);
        } else if (section == L"address") {
            if (key == L"addr") s.addr = value;
        } else if (section == L"resolution") {
            if (key == L"res_w") s.resW = ClampInt(asInt(s.resW), kResMin, kResMax);
            else if (key == L"res_h") s.resH = ClampInt(asInt(s.resH), kResMin, kResMax);
        } else if (section == L"ui") {
            if (key == L"theme") s.dark = !(value == L"light" || value == L"warm");
        } else if (section == L"window") {
            if (key == L"width") s.winW = ClampInt(asInt(s.winW), kMinW, 10000);
            else if (key == L"height") s.winH = ClampInt(asInt(s.winH), kMinH, 10000);
            else if (key == L"maximized") s.maximized = (asInt(0) != 0);
        }
    }
    if (!DirExists(s.addr)) {
        // Keep the text for display, but the save flow will ask if invalid.
    }
}

static bool SaveSettings(const Settings& s, const std::wstring& path) {
    std::wstring t;
    t += L"[color]\r\n";
    t += L"font_color=" + ColorToHex(s.fontColor) + L"\r\n";
    t += L"bg_color=" + ColorToHex(s.bgColor) + L"\r\n\r\n";
    t += L"[location]\r\n";
    t += Fmt(L"value_w=%d\r\nvalue_h=%d\r\n\r\n", s.posX, s.posY);
    t += L"[font]\r\n";
    t += Fmt(L"size=%d\r\n\r\n", s.fontSize);
    t += L"[address]\r\n";
    t += L"addr=" + s.addr + L"\r\n\r\n";
    t += L"[resolution]\r\n";
    t += Fmt(L"res_w=%d\r\nres_h=%d\r\n\r\n", s.resW, s.resH);
    t += L"[ui]\r\n";
    t += std::wstring(L"theme=") + (s.dark ? L"dark" : L"light") + L"\r\n\r\n";
    t += L"[window]\r\n";
    t += Fmt(L"width=%d\r\nheight=%d\r\nmaximized=%d\r\n", s.winW, s.winH,
             s.maximized ? 1 : 0);
    return WriteTextFileUtf8(path, t);
}

// ---------------------------------------------------------------------------
//  Resolution presets
// ---------------------------------------------------------------------------
struct ResolutionPreset {
    const wchar_t* ratio;
    const wchar_t* label;
    int w, h;
};

static const ResolutionPreset kPresets[] = {
    { L"16:9",  L"1920 × 1080", 1920, 1080 },
    { L"16:9",  L"2560 × 1440", 2560, 1440 },
    { L"16:9",  L"3840 × 2160", 3840, 2160 },
    { L"16:9",  L"1366 × 768",  1366, 768  },
    { L"16:10", L"1920 × 1200", 1920, 1200 },
    { L"16:10", L"1680 × 1050", 1680, 1050 },
    { L"16:10", L"2560 × 1600", 2560, 1600 },
    { L"21:9",  L"3440 × 1440", 3440, 1440 },
    { L"21:9",  L"2560 × 1080", 2560, 1080 },
    { L"4:3",   L"1600 × 1200", 1600, 1200 },
    { L"3:2",   L"2160 × 1440", 2160, 1440 },
    { L"1:1",   L"1080 × 1080", 1080, 1080 },
};
static const int kPresetCount = (int)(sizeof(kPresets) / sizeof(kPresets[0]));

static int FindMatchingPreset(int w, int h) {
    for (int i = 0; i < kPresetCount; ++i) {
        if (kPresets[i].w == w && kPresets[i].h == h) return i;
    }
    return -1;
}

static std::wstring PresetDisplay(int index) {
    if (index < 0 || index >= kPresetCount) return L"自定义";
    return std::wstring(kPresets[index].ratio) + L"   " + kPresets[index].label;
}

// ---------------------------------------------------------------------------
//  Rendering
// ---------------------------------------------------------------------------
struct Row {
    std::wstring word;
    std::wstring meaning;
    HWND hWord = nullptr;
    HWND hMeaning = nullptr;
    HWND hDelete = nullptr;
};

// ---------------------------------------------------------------------------
//  Project notes embedded in the JPEG COM segment
// ---------------------------------------------------------------------------
static const wchar_t* kProjectMagic = L"WordsWallpaper-Project-1";
static const char* kProjectMagicBytes = "WordsWallpaper-Project-1";
static const size_t kProjectMagicLen = sizeof("WordsWallpaper-Project-1") - 1;
static const size_t kMaxProjectComment = 60000;

struct ProjectEntry {
    std::wstring word;
    std::wstring meaning;
};

struct ProjectData {
    int fontSize = 100;
    int posX = 200;
    int posY = 350;
    int resW = 1920;
    int resH = 1080;
    COLORREF fontColor = RGB(255, 255, 255);
    COLORREF bgColor = RGB(0, 0, 0);
    bool dark = true;
    std::vector<ProjectEntry> entries;
};

static std::wstring CleanMetaText(const std::wstring& text) {
    std::wstring out = text;
    for (wchar_t& c : out) {
        if (c == L'\r' || c == L'\n') c = L' ';
    }
    return out;
}

static bool ProjectRowEmpty(const Row& r) {
    return TrimW(r.word).empty() && TrimW(r.meaning).empty();
}

// Build the human-readable project block stored in the JPEG comment.
// The last (auto-added) empty row is not part of the stored project.
static std::wstring BuildProjectComment(const Settings& s, const std::vector<Row>& rows) {
    size_t count = rows.size();
    while (count > 0 && ProjectRowEmpty(rows[count - 1])) --count;
    if (count == 0) return L"";

    std::wstring t = kProjectMagic;
    t += L"\napp=WordsWallpaper\nformat=2\napp_version=1.0\n";
    t += Fmt(L"font_size=%d\npos_x=%d\npos_y=%d\nres_w=%d\nres_h=%d\n",
             s.fontSize, s.posX, s.posY, s.resW, s.resH);
    t += L"font_color=" + ColorToHex(s.fontColor) + L"\n";
    t += L"bg_color=" + ColorToHex(s.bgColor) + L"\n";
    t += std::wstring(L"theme=") + (s.dark ? L"dark" : L"light") + L"\n";
    t += Fmt(L"quality=%d\ncount=%d\n", kJpegQuality, (int)count);
    for (size_t i = 0; i < count; ++i) {
        t += Fmt(L"word_%d=", (int)i) + CleanMetaText(rows[i].word) + L"\n";
        t += Fmt(L"mean_%d=", (int)i) + CleanMetaText(rows[i].meaning) + L"\n";
    }
    std::string utf8 = WideToUtf8(t);
    if (utf8.size() > kMaxProjectComment) return L"";
    return t;
}

static bool ParseProjectComment(const std::string& utf8, ProjectData& p) {
    std::wstring text;
    if (!Utf8ToWide(utf8, text)) return false;
    while (!text.empty() && text.back() == L'\0') text.pop_back();

    std::vector<std::pair<std::wstring, std::wstring>> fields;
    size_t pos = 0;
    bool firstLine = true;
    while (pos <= text.size()) {
        size_t end = text.find(L'\n', pos);
        if (end == std::wstring::npos) end = text.size();
        std::wstring line = text.substr(pos, end - pos);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        pos = end + 1;
        if (firstLine) {
            if (line != kProjectMagic) return false;
            firstLine = false;
            continue;
        }
        if (line.empty()) continue;
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        fields.emplace_back(LowerW(TrimW(line.substr(0, eq))), line.substr(eq + 1));
    }
    if (firstLine) return false;

    auto findField = [&](const std::wstring& key) -> const std::wstring* {
        for (const auto& f : fields) {
            if (f.first == key) return &f.second;
        }
        return nullptr;
    };
    auto intField = [&](const std::wstring& key, int def) -> int {
        const std::wstring* v = findField(key);
        if (!v || v->empty()) return def;
        wchar_t* stop = nullptr;
        long n = wcstol(v->c_str(), &stop, 10);
        if (stop == v->c_str()) return def;
        return (int)n;
    };

    int count = intField(L"count", -1);
    if (count <= 0 || count > 1000) return false;
    p.fontSize = ClampInt(intField(L"font_size", p.fontSize), kFontMin, kFontMax);
    p.posX = ClampInt(intField(L"pos_x", p.posX), kPosMin, kPosMax);
    p.posY = ClampInt(intField(L"pos_y", p.posY), kPosMin, kPosMax);
    p.resW = ClampInt(intField(L"res_w", p.resW), kResMin, kResMax);
    p.resH = ClampInt(intField(L"res_h", p.resH), kResMin, kResMax);
    if (const std::wstring* v = findField(L"font_color")) p.fontColor = HexToColor(*v, p.fontColor);
    if (const std::wstring* v = findField(L"bg_color")) p.bgColor = HexToColor(*v, p.bgColor);
    if (const std::wstring* v = findField(L"theme")) p.dark = !(*v == L"light" || *v == L"warm");

    p.entries.clear();
    p.entries.reserve((size_t)count);
    for (int i = 0; i < count; ++i) {
        ProjectEntry e;
        if (const std::wstring* v = findField(L"word_" + std::to_wstring(i))) e.word = *v;
        if (const std::wstring* v = findField(L"mean_" + std::to_wstring(i))) e.meaning = *v;
        p.entries.push_back(std::move(e));
    }
    return true;
}

// Build an EXIF APP1 segment holding the project text as UserComment.
// This is what Windows shows in the file "Comments" property.
static bool BuildExifProjectSegment(const std::wstring& text, std::vector<BYTE>& segment) {
    segment.clear();
    size_t valueLen = 8 + text.size() * 2;
    if (valueLen > 64000) return false;
    size_t tiffSize = 44 + valueLen;
    if (tiffSize & 1) ++tiffSize;              // keep TIFF word aligned
    size_t app1Len = 2 + 6 + tiffSize;
    if (app1Len > 65535) return false;

    std::vector<BYTE> tiff(tiffSize, 0);
    auto put16 = [&](size_t o, unsigned v) {
        tiff[o] = (BYTE)(v & 0xFF);
        tiff[o + 1] = (BYTE)((v >> 8) & 0xFF);
    };
    auto put32 = [&](size_t o, unsigned v) {
        tiff[o] = (BYTE)(v & 0xFF);
        tiff[o + 1] = (BYTE)((v >> 8) & 0xFF);
        tiff[o + 2] = (BYTE)((v >> 16) & 0xFF);
        tiff[o + 3] = (BYTE)((v >> 24) & 0xFF);
    };

    tiff[0] = 'I';
    tiff[1] = 'I';
    put16(2, 42);          // TIFF magic
    put32(4, 8);           // IFD0 offset
    put16(8, 1);           // IFD0: one entry
    put16(10, 0x8769);     // Exif IFD pointer
    put16(12, 4);          // LONG
    put32(14, 1);
    put32(18, 26);         // Exif IFD offset
    put32(22, 0);          // no next IFD
    put16(26, 1);          // Exif IFD: one entry
    put16(28, 0x9286);     // UserComment
    put16(30, 7);          // UNDEFINED
    put32(32, (unsigned)valueLen);
    put32(36, 44);         // value offset
    put32(40, 0);
    memcpy(&tiff[44], "UNICODE\0", 8);
    memcpy(&tiff[52], text.data(), text.size() * 2);

    segment.reserve(app1Len + 2);
    segment.push_back(0xFF);
    segment.push_back(0xE1);
    segment.push_back((BYTE)((app1Len >> 8) & 0xFF));
    segment.push_back((BYTE)(app1Len & 0xFF));
    const char exifHeader[6] = { 'E', 'x', 'i', 'f', 0, 0 };
    segment.insert(segment.end(), exifHeader, exifHeader + 6);
    segment.insert(segment.end(), tiff.begin(), tiff.end());
    return true;
}

// Insert EXIF UserComment (for Windows) and a JPEG COM segment (for our
// importer) immediately after SOI.  Both are best-effort.
static bool InjectProjectMetadata(const std::wstring& path,
                                  const std::wstring& projectText,
                                  const std::string& comment,
                                  bool& comOk, bool& exifOk) {
    comOk = false;
    exifOk = false;
    std::vector<BYTE> data;
    if (!ReadAllBytes(path, data)) return false;
    if (data.size() < 2 || data[0] != 0xFF || data[1] != 0xD8) return false;

    std::vector<BYTE> exif;
    bool hasExif = BuildExifProjectSegment(projectText, exif);
    bool hasCom = !comment.empty() && comment.size() <= 65533;
    if (!hasExif && !hasCom) return false;

    std::vector<BYTE> out;
    out.reserve(data.size() + exif.size() + comment.size() + 8);
    out.push_back(0xFF);
    out.push_back(0xD8);
    if (hasExif) out.insert(out.end(), exif.begin(), exif.end());
    if (hasCom) {
        size_t len = comment.size() + 2;
        out.push_back(0xFF);
        out.push_back(0xFE);
        out.push_back((BYTE)((len >> 8) & 0xFF));
        out.push_back((BYTE)(len & 0xFF));
        out.insert(out.end(), comment.begin(), comment.end());
    }
    out.insert(out.end(), data.begin() + 2, data.end());
    bool written = WriteAllBytesAtomic(path, out);
    comOk = written && hasCom;
    exifOk = written && hasExif;
    return written;
}

static bool ExtractProjectComment(const std::wstring& path, std::string& comment) {
    comment.clear();
    std::vector<BYTE> data;
    if (!ReadAllBytes(path, data)) return false;
    if (data.size() < 4 || data[0] != 0xFF || data[1] != 0xD8) return false;

    size_t p = 2;
    while (p + 1 < data.size()) {
        if (data[p] != 0xFF) {
            ++p;
            continue;
        }
        while (p < data.size() && data[p] == 0xFF) ++p;
        if (p >= data.size()) break;
        BYTE marker = data[p++];
        if (marker == 0xD8 || marker == 0x01 ||
            (marker >= 0xD0 && marker <= 0xD7)) {
            continue;
        }
        if (marker == 0xD9 || marker == 0xDA) break;
        if (p + 2 > data.size()) break;
        size_t len = ((size_t)data[p] << 8) | data[p + 1];
        if (len < 2 || p + len > data.size()) break;
        if (marker == 0xFE) {
            const char* payload = reinterpret_cast<const char*>(data.data() + p + 2);
            size_t payloadLen = len - 2;
            if (payloadLen >= kProjectMagicLen &&
                memcmp(payload, kProjectMagicBytes, kProjectMagicLen) == 0) {
                comment.assign(payload, payloadLen);
                while (!comment.empty() && comment.back() == '\0') comment.pop_back();
                return true;
            }
        }
        p += len;
    }
    return false;
}

// The original PIL version advanced each line by the line's ink height + 4px.
// For msyhbd this works out to ~1.10 em, which is also what looks closest to
// the reference screenshots while staying even for every line.
static const double kLinePitchEm = 1.10;

static std::wstring MakeLine(const Row& r) {
    return r.word + L"   " + r.meaning;
}

static bool GetEncoderClsid(const WCHAR* mime, CLSID* clsid) {
    UINT num = 0, size = 0;
    if (GetImageEncodersSize(&num, &size) != Ok || size == 0) return false;
    std::vector<BYTE> mem(size);
    ImageCodecInfo* info = reinterpret_cast<ImageCodecInfo*>(mem.data());
    if (GetImageEncoders(num, size, info) != Ok) return false;
    for (UINT i = 0; i < num; ++i) {
        if (wcscmp(info[i].MimeType, mime) == 0) {
            *clsid = info[i].Clsid;
            return true;
        }
    }
    return false;
}

static HFONT CreateImageFont(int px, const std::wstring& face) {
    LOGFONTW lf{};
    lf.lfHeight = -px;
    lf.lfWeight = FW_BOLD;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = ANTIALIASED_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    lstrcpynW(lf.lfFaceName, face.c_str(), LF_FACESIZE);
    return CreateFontIndirectW(&lf);
}

// Render the wallpaper (scale = 1 for full size, <1 for the live preview).
static Bitmap* RenderImage(const Settings& s, const std::vector<Row>& rows,
                           int width, int height, double scale) {
    if (width < 1 || height < 1) return nullptr;
    Bitmap* bmp = nullptr;
    try {
        bmp = new Bitmap(width, height, PixelFormat24bppRGB);
    } catch (...) {
        return nullptr;
    }
    if (!bmp) return nullptr;
    if (bmp->GetLastStatus() != Ok) {
        delete bmp;
        return nullptr;
    }

    Graphics g(bmp);
    g.SetSmoothingMode(SmoothingModeNone);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.Clear(Color(255, GetRValue(s.bgColor), GetGValue(s.bgColor), GetBValue(s.bgColor)));

    if (rows.empty()) return bmp;

    HDC hdc = g.GetHDC();
    if (!hdc) return bmp;

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, s.fontColor);
    SetTextAlign(hdc, TA_LEFT | TA_TOP | TA_NOUPDATECP);

    int fontPx = std::max(1, (int)std::lround(s.fontSize * scale));
    HFONT hf = CreateImageFont(fontPx, g_imageFace);
    HGDIOBJ oldFont = SelectObject(hdc, hf);

    const double pitch = s.fontSize * kLinePitchEm;
    for (size_t i = 0; i < rows.size(); ++i) {
        std::wstring line = MakeLine(rows[i]);
        int x = (int)std::lround(s.posX * scale);
        int y = (int)std::lround((s.posY + (double)i * pitch) * scale);
        if (y > height + fontPx) break;
        TextOutW(hdc, x, y, line.c_str(), (int)line.size());
    }

    SelectObject(hdc, oldFont);
    DeleteObject(hf);
    g.ReleaseHDC(hdc);
    return bmp;
}

static bool SaveJpeg(Bitmap* bmp, const std::wstring& path, int quality, unsigned long long* outBytes) {
    if (!g_jpegClsidValid) return false;
    EncoderParameters ep;
    ep.Count = 1;
    ep.Parameter[0].Guid = EncoderQuality;
    ep.Parameter[0].Type = EncoderParameterValueTypeLong;
    ep.Parameter[0].NumberOfValues = 1;
    ULONG q = (ULONG)ClampInt(quality, 1, 100);
    ep.Parameter[0].Value = &q;
    Status st = bmp->Save(path.c_str(), &g_jpegClsid, &ep);
    if (st != Ok) return false;
    if (outBytes) {
        WIN32_FILE_ATTRIBUTE_DATA fad{};
        if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) {
            *outBytes = ((unsigned long long)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
        } else {
            *outBytes = 0;
        }
    }
    return true;
}

static std::wstring SanitizeFileBase(std::wstring name) {
    name = TrimW(name);
    std::wstring out;
    for (wchar_t c : name) {
        if (c < 32 || c == L'<' || c == L'>' || c == L':' || c == L'"' ||
            c == L'/' || c == L'\\' || c == L'|' || c == L'?' || c == L'*') {
            out.push_back(L'_');
        } else {
            out.push_back(c);
        }
    }
    while (!out.empty() && (out.back() == L'.' || out.back() == L' ')) out.pop_back();
    if (out.size() > 120) out.resize(120);
    if (out.empty()) out = L"words";
    const wchar_t* reserved[] = { L"CON", L"PRN", L"AUX", L"NUL" };
    for (const wchar_t* r : reserved) {
        if (_wcsicmp(out.c_str(), r) == 0) out = L"_" + out;
    }
    if (out.size() == 1 && iswdigit(out[0])) out = L"_" + out;
    return out;
}

static std::wstring BuildFileName(const std::vector<Row>& rows) {
    std::wstring base;
    for (size_t i = 0; i < rows.size(); ++i) {
        if (i) base += L" ";
        base += rows[i].word;
    }
    base = SanitizeFileBase(base);
    return base + L".jpg";
}

static bool SetDesktopWallpaper(const std::wstring& path) {
    BOOL ok = SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, (PVOID)path.c_str(),
                                    SPIF_UPDATEINIFILE | SPIF_SENDWININICHANGE);
    return ok != FALSE;
}

// ---------------------------------------------------------------------------
//  Fonts
// ---------------------------------------------------------------------------
static bool IsYaHeiName(const std::wstring& s) {
    return s.find(L"YaHei") != std::wstring::npos ||
           s.find(L"雅黑") != std::wstring::npos;
}

static void InitImageFont() {
    wchar_t windir[MAX_PATH] = {};
    GetWindowsDirectoryW(windir, MAX_PATH);
    std::wstring fontFile = std::wstring(windir) + L"\\Fonts\\msyhbd.ttc";
    if (FileExists(fontFile)) {
        if (AddFontResourceExW(fontFile.c_str(), FR_PRIVATE, 0) > 0) {
            g_privateFontLoaded = true;
            g_forcedFontFile = fontFile;
        }
    }

    HDC dc = GetDC(nullptr);
    const wchar_t* candidates[] = { L"Microsoft YaHei UI", L"Microsoft YaHei" };
    std::wstring found;
    for (const wchar_t* cand : candidates) {
        LOGFONTW lf{};
        lf.lfHeight = -24;
        lf.lfWeight = FW_BOLD;
        lf.lfCharSet = DEFAULT_CHARSET;
        lstrcpynW(lf.lfFaceName, cand, LF_FACESIZE);
        HFONT f = CreateFontIndirectW(&lf);
        if (!f) continue;
        HGDIOBJ old = SelectObject(dc, f);
        wchar_t got[LF_FACESIZE] = {};
        GetTextFaceW(dc, LF_FACESIZE, got);
        SelectObject(dc, old);
        DeleteObject(f);
        std::wstring actual = got;
        if (!actual.empty() && IsYaHeiName(actual)) {
            found = actual;
            break;
        }
    }
    ReleaseDC(nullptr, dc);

    if (!found.empty()) {
        g_imageFace = found;
        g_uiFace = found;
    } else {
        g_imageFace = L"Segoe UI";
        g_uiFace = L"Segoe UI";
    }
}

static void ReleaseImageFont() {
    if (g_privateFontLoaded && !g_forcedFontFile.empty()) {
        RemoveFontResourceExW(g_forcedFontFile.c_str(), FR_PRIVATE, 0);
        g_privateFontLoaded = false;
    }
}

// ---------------------------------------------------------------------------
//  Small GDI paint helpers (used by the UI)
// ---------------------------------------------------------------------------
static void FillSolid(HDC dc, const RECT& r, COLORREF color) {
    HBRUSH br = CreateSolidBrush(color);
    FillRect(dc, &r, br);
    DeleteObject(br);
}

static void RoundRectPaint(HDC dc, const RECT& r, int radius, COLORREF fill,
                           COLORREF border, int borderWidth = 1) {
    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, borderWidth, border);
    HGDIOBJ ob = SelectObject(dc, br);
    HGDIOBJ op = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius * 2, radius * 2);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
    DeleteObject(pen);
}

static void DrawTextRect(HDC dc, const std::wstring& text, const RECT& r, COLORREF color,
                         HFONT font, UINT flags) {
    if (font) SelectObject(dc, font);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    RECT rr = r;
    DrawTextW(dc, text.c_str(), (int)text.size(), &rr, flags);
}

static void DrawChevron(HDC dc, int cx, int cy, int size, COLORREF color, bool down) {
    POINT pts[3];
    if (down) {
        pts[0] = { cx - size, cy - size / 2 };
        pts[1] = { cx + size, cy - size / 2 };
        pts[2] = { cx, cy + size / 2 + 1 };
    } else {
        pts[0] = { cx - size, cy + size / 2 };
        pts[1] = { cx + size, cy + size / 2 };
        pts[2] = { cx, cy - size / 2 - 1 };
    }
    HBRUSH br = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ ob = SelectObject(dc, br);
    HGDIOBJ op = SelectObject(dc, pen);
    Polygon(dc, pts, 3);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
    DeleteObject(pen);
}

struct DoubleBuffer {
    HDC mem = nullptr;
    HBITMAP bmp = nullptr;
    HGDIOBJ old = nullptr;
    int w = 0, h = 0;

    bool Begin(HDC target, int width, int height) {
        if (width <= 0 || height <= 0) return false;
        mem = CreateCompatibleDC(target);
        bmp = CreateCompatibleBitmap(target, width, height);
        if (!mem || !bmp) {
            End(target);
            return false;
        }
        old = SelectObject(mem, bmp);
        w = width;
        h = height;
        return true;
    }

    void End(HDC) {
        if (mem && old) SelectObject(mem, old);
        if (bmp) DeleteObject(bmp);
        if (mem) DeleteDC(mem);
        mem = nullptr;
        bmp = nullptr;
        old = nullptr;
    }

    void Blit(HDC target) {
        if (mem) BitBlt(target, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    }
};

// ---------------------------------------------------------------------------
//  Layout + application state
// ---------------------------------------------------------------------------
struct Layout {
    int dpi = 96;
    RECT client{};
    RECT wordHeader{}, panel{}, addRowBtn{}, wordHint{};
    RECT styleHeader{};
    RECT fontLabel{}, fontBox{};
    RECT posLabel{}, posXLabel{}, posXBox{}, posYLabel{}, posYBox{};
    RECT colorLabel{}, fontColorBtn{}, bgColorBtn{};
    RECT presetLabel{}, presetBtn{};
    RECT resLabel{}, resWLabel{}, resWBox{}, resHLabel{}, resHBox{};
    RECT themeLabel{}, themeDarkBtn{}, themeLightBtn{}, aboutBtn{};
    RECT saveHeader{}, pathField{}, browseBtn{};
    RECT previewHeader{}, preview{}, previewHint{};
    RECT actionBar{}, importBtn{}, openDirBtn{}, saveOnlyBtn{}, applyBtn{};
    RECT statusBar{};
};

struct App {
    HINSTANCE inst = nullptr;
    HWND hwnd = nullptr;
    int dpi = 96;
    Palette pal;
    Settings s;
    std::vector<Row> rows;
    int activeRow = 0;
    int scrollY = 0;
    bool needScrollbar = false;
    RECT scrollbarThumb{};
    bool dragScrollbar = false;
    int dragScrollbarOffset = 0;

    HWND panel = nullptr;
    HWND preview = nullptr;
    HWND fontBox = nullptr, posXBox = nullptr, posYBox = nullptr;
    HWND resWBox = nullptr, resHBox = nullptr;
    HWND fontEdit = nullptr, posXEdit = nullptr, posYEdit = nullptr;
    HWND resWEdit = nullptr, resHEdit = nullptr;
    HWND btnFontColor = nullptr, btnBgColor = nullptr, presetBtn = nullptr;
    HWND themeDarkBtn = nullptr, themeLightBtn = nullptr;
    HWND aboutBtn = nullptr;
    HWND aboutWindow = nullptr;
    HWND aboutGithubBtn = nullptr, aboutBiliBtn = nullptr;
    HWND aboutSponsorBtn = nullptr, aboutCloseBtn = nullptr;
    std::vector<HWND> aboutTabStops;
    int aboutDpi = 96;
    HWND addBtn = nullptr, browseBtn = nullptr, openDirBtn = nullptr;
    HWND importBtn = nullptr;
    HWND saveOnlyBtn = nullptr, applyBtn = nullptr;
    HWND tip = nullptr;

    HFONT fontUi = nullptr, fontBold = nullptr, fontSmall = nullptr;
    HFONT fontSection = nullptr, fontTitle = nullptr;
    HBRUSH brInput = nullptr, brBg = nullptr, brPanel = nullptr, brStatus = nullptr;

    Layout L;

    Bitmap* previewBmp = nullptr;
    int previewW = 0, previewH = 0;
    RECT imageRect{};
    bool hoveringPreview = false;
    bool draggingPos = false;
    bool dragMoved = false;
    int dragX = 0, dragY = 0;

    UINT_PTR previewTimer = 0, saveTimer = 0;
    std::wstring status;
    std::wstring previewInfo;
    bool statusError = false;
    std::wstring configPath;
    std::vector<HWND> tabStops;
    bool suppressAutoAdd = false;
    bool firstRun = false;
    bool creating = true;
    HACCEL accel = nullptr;

    int Px(int v) const { return MulDiv(v, dpi, 96); }
};

static void FocusStep(HWND current, int dir);
static void Relayout(App& a);
static void SchedulePreview(App& a, int delayMs = 45);
static void RenderPreviewNow(App& a);
static void UpdatePreviewInfo(App& a);
static void PanelLayout(App& a);
static void PanelEnsureVisible(App& a, int row);
static void SetStatus(App& a, const std::wstring& text, bool isError = false);
static void SaveSettingsNow(App& a);
static void SetTheme(App& a, bool dark);
static void AddRow(App& a, bool focusNew);
static void DeleteRow(App& a, int index);
static void RebuildTabStops(App& a);
static void SyncPresetFromSize(App& a);
static void ApplyResPreset(App& a, int presetIndex);
static bool EnsureSaveAddress(App& a, bool firstUse);
static bool DoSave(App& a, bool apply);
static void BrowseForSaveDir(App& a);
static void SetColorAndPreview(App& a, bool fontColor);
static void ShowOpenFolder(App& a);
static void ShowAboutDialog(App& a);
static void UpdateTitleBarTheme(App& a);
static void ImportProjectDialog(App& a);
static bool ImportProjectFile(App& a, const std::wstring& path, bool showErrors);
static void ApplyImportedProject(App& a, const ProjectData& p, const std::wstring& path);

// ---------------------------------------------------------------------------
//  Custom button control
// ---------------------------------------------------------------------------
struct ButtonData {
    std::wstring text;
    int style = BTN_NORMAL;
    bool checked = false;
    bool hover = false;
    bool pressed = false;
    bool focused = false;
    COLORREF swatch = RGB(0, 0, 0);
};

static COLORREF ButtonSurfaceColor(HWND h) {
    if (!g_app) return RGB(0, 0, 0);
    if (g_app->panel && GetParent(h) == g_app->panel) return g_app->pal.panel;
    return g_app->pal.bg;
}

static LRESULT CALLBACK ButtonProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    ButtonData* d = reinterpret_cast<ButtonData*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_NCCREATE) {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(l);
        d = reinterpret_cast<ButtonData*>(cs->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(d));
        return DefWindowProcW(h, m, w, l);
    }
    if (!d) return DefWindowProcW(h, m, w, l);

    switch (m) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        if (rc.right <= 0 || rc.bottom <= 0) {
            EndPaint(h, &ps);
            return 0;
        }
        DoubleBuffer db;
        if (!db.Begin(dc, rc.right, rc.bottom)) {
            EndPaint(h, &ps);
            return 0;
        }
        HDC mem = db.mem;
        const Palette& p = g_app->pal;
        COLORREF surface = ButtonSurfaceColor(h);
        FillSolid(mem, rc, surface);

        bool enabled = IsWindowEnabled(h) != FALSE;
        bool hot = enabled && d->hover;
        bool down = enabled && d->pressed;
        int r = g_app->Px(6);

        COLORREF fill = surface;
        COLORREF border = p.border;
        COLORREF textColor = p.text;
        bool drawBorder = true;

        switch (d->style) {
        case BTN_PRIMARY:
            fill = down ? p.primaryPressed : (hot ? p.primaryHover : p.primary);
            border = fill;
            textColor = RGB(255, 255, 255);
            break;
        case BTN_GHOST:
            fill = down ? p.btnPressed : (hot ? p.btnHover : surface);
            border = hot ? p.border : surface;
            textColor = hot ? p.text : p.muted;
            break;
        case BTN_TOGGLE:
            if (d->checked) {
                fill = p.accentSoft;
                border = p.accent;
                textColor = p.dark ? p.accent : p.primaryPressed;
            } else {
                fill = down ? p.btnPressed : (hot ? p.btnHover : p.btnBg);
                border = p.border;
                textColor = p.text;
            }
            break;
        case BTN_DANGER:
            fill = down ? p.dangerHover : (hot ? p.danger : surface);
            border = hot ? p.danger : surface;
            textColor = hot ? RGB(255, 255, 255) : p.muted;
            break;
        case BTN_COLOR:
            fill = down ? p.btnPressed : (hot ? p.btnHover : p.input);
            border = d->focused ? p.accent : p.border;
            textColor = p.text;
            break;
        case BTN_SELECT:
            fill = down ? p.btnPressed : (hot ? p.btnHover : p.input);
            border = (d->focused || hot) ? p.accent : p.border;
            textColor = p.text;
            break;
        case BTN_SMALL:
        case BTN_NORMAL:
        default:
            fill = down ? p.btnPressed : (hot ? p.btnHover : p.btnBg);
            border = (d->focused || hot) ? p.accent : p.border;
            textColor = p.text;
            break;
        }
        if (!enabled) {
            fill = surface;
            border = p.border;
            textColor = p.muted;
            drawBorder = false;
        }

        RECT body = rc;
        if (d->style != BTN_GHOST) {
            body.left += g_app->Px(1);
            body.top += g_app->Px(1);
            body.right -= g_app->Px(1);
            body.bottom -= g_app->Px(1);
            RoundRectPaint(mem, body, r, fill, drawBorder ? border : fill, 1);
        } else if (hot || down) {
            InflateRect(&body, -g_app->Px(1), -g_app->Px(1));
            RoundRectPaint(mem, body, r, fill, border, 1);
        }

        HFONT font = (d->style == BTN_SMALL) ? g_app->fontSmall :
                     ((d->style == BTN_PRIMARY || d->style == BTN_DANGER)
                          ? g_app->fontBold : g_app->fontUi);
        RECT textRect = rc;
        int textPad = (d->style == BTN_DANGER) ? g_app->Px(1)
                    : (d->style == BTN_SMALL ? g_app->Px(4) : g_app->Px(8));
        textRect.left += textPad;
        textRect.right -= textPad;

        if (d->style == BTN_COLOR) {
            int pad = g_app->Px(9);
            int sw = std::max(g_app->Px(14), (int)(rc.bottom - pad * 2));
            RECT sr{ rc.left + pad, (rc.top + rc.bottom - sw) / 2,
                     rc.left + pad + sw, (rc.top + rc.bottom + sw) / 2 };
            FillSolid(mem, sr, d->swatch);
            HBRUSH eb = CreateSolidBrush(p.border);
            FrameRect(mem, &sr, eb);
            DeleteObject(eb);
            textRect.left = sr.right + g_app->Px(7);
        }

        if (d->style == BTN_SELECT) {
            textRect.right -= g_app->Px(20);
            DrawTextRect(mem, d->text, textRect, textColor, font,
                         DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX |
                         DT_END_ELLIPSIS);
            DrawChevron(mem, rc.right - g_app->Px(12), (rc.top + rc.bottom) / 2,
                        g_app->Px(4), hot ? p.accent : p.muted, true);
        } else {
            DrawTextRect(mem, d->text, textRect, textColor, font,
                         DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX |
                         DT_END_ELLIPSIS);
        }

        if (d->focused) {
            RECT fr = rc;
            InflateRect(&fr, -g_app->Px(3), -g_app->Px(3));
            HBRUSH fb = CreateSolidBrush(p.accent);
            FrameRect(mem, &fr, fb);
            DeleteObject(fb);
        }

        db.Blit(dc);
        db.End(dc);
        EndPaint(h, &ps);
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!d->hover) {
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = h;
            TrackMouseEvent(&tme);
            d->hover = true;
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        d->hover = false;
        if (!d->pressed) InvalidateRect(h, nullptr, FALSE);
        return 0;

    case WM_LBUTTONDOWN:
        if (!IsWindowEnabled(h)) return 0;
        SetFocus(h);
        d->pressed = true;
        d->hover = true;
        SetCapture(h);
        InvalidateRect(h, nullptr, FALSE);
        return 0;

    case WM_LBUTTONUP: {
        if (!d->pressed) return 0;
        d->pressed = false;
        if (GetCapture() == h) ReleaseCapture();
        POINT pt{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        RECT rc;
        GetClientRect(h, &rc);
        bool inside = PtInRect(&rc, pt) != FALSE;
        InvalidateRect(h, nullptr, FALSE);
        if (inside && IsWindowEnabled(h)) {
            SendMessageW(GetParent(h), WM_COMMAND,
                         MAKEWPARAM(GetDlgCtrlID(h), BN_CLICKED), (LPARAM)h);
        }
        return 0;
    }

    case WM_CAPTURECHANGED:
        if (d->pressed) {
            d->pressed = false;
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;

    case WM_SETFOCUS:
        d->focused = true;
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_KILLFOCUS:
        d->focused = false;
        InvalidateRect(h, nullptr, FALSE);
        return 0;

    case WM_KEYDOWN:
        if (w == VK_ESCAPE && g_app && g_app->aboutWindow &&
            GetParent(h) == g_app->aboutWindow) {
            SendMessageW(GetParent(h), WM_CLOSE, 0, 0);
            return 0;
        }
        if (w == VK_TAB) {
            FocusStep(h, (GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1);
            return 0;
        }
        if (w == VK_SPACE || w == VK_RETURN) {
            if (IsWindowEnabled(h)) {
                SendMessageW(GetParent(h), WM_COMMAND,
                             MAKEWPARAM(GetDlgCtrlID(h), BN_CLICKED), (LPARAM)h);
            }
            return 0;
        }
        break;

    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT && IsWindowEnabled(h)) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        break;

    case WM_ENABLE:
        InvalidateRect(h, nullptr, TRUE);
        return 0;

    case WM_NCDESTROY:
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        delete d;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static HWND CreateUIButton(HWND parent, int id, const wchar_t* text, int style,
                           COLORREF swatch = RGB(0, 0, 0)) {
    ButtonData* d = new (std::nothrow) ButtonData;
    if (!d) return nullptr;
    d->text = text ? text : L"";
    d->style = style;
    d->checked = false;
    d->swatch = swatch;
    HWND h = CreateWindowExW(0, kButtonClass, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                             0, 0, 10, 10, parent, (HMENU)(INT_PTR)id, g_hInst, d);
    if (!h) delete d;
    return h;
}

static void BtnSetText(HWND h, const std::wstring& text) {
    if (!h) return;
    ButtonData* d = reinterpret_cast<ButtonData*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!d) return;
    d->text = text;
    InvalidateRect(h, nullptr, FALSE);
}

static void BtnSetSwatch(HWND h, COLORREF color) {
    if (!h) return;
    ButtonData* d = reinterpret_cast<ButtonData*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!d) return;
    d->swatch = color;
    InvalidateRect(h, nullptr, FALSE);
}

static void BtnSetChecked(HWND h, bool checked) {
    if (!h) return;
    ButtonData* d = reinterpret_cast<ButtonData*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!d || d->checked == checked) return;
    d->checked = checked;
    InvalidateRect(h, nullptr, FALSE);
}

// ---------------------------------------------------------------------------
//  Custom numeric spinner (edit + two arrow buttons, drawn as one control)
// ---------------------------------------------------------------------------
struct NumberData {
    HWND edit = nullptr;
    int value = 0;
    int minVal = 0;
    int maxVal = 100;
    int step = 1;
    bool hover = false;
    bool focused = false;
    bool suppress = false;
    int hotArea = 0; // 0 none, 1 up, 2 down
};

static void NumberFormat(NumberData* d);
static void NumberNotify(HWND h);
static void NumberSetValue(HWND h, int value, bool notify);
static void NumberStep(HWND h, int deltaSteps);

static LRESULT CALLBACK NumberEditProc(HWND h, UINT m, WPARAM w, LPARAM l,
                                       UINT_PTR, DWORD_PTR ref) {
    HWND box = reinterpret_cast<HWND>(ref);
    switch (m) {
    case WM_KEYDOWN:
        if (w == VK_UP) {
            SendMessageW(box, NB_STEP, 1, 0);
            return 0;
        }
        if (w == VK_DOWN) {
            SendMessageW(box, NB_STEP, (WPARAM)-1, 0);
            return 0;
        }
        if (w == VK_TAB) {
            FocusStep(h, (GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1);
            return 0;
        }
        if (w == VK_RETURN) {
            FocusStep(h, 1);
            return 0;
        }
        break;
    case WM_MOUSEWHEEL:
        SendMessageW(box, WM_MOUSEWHEEL, w, l);
        return 0;
    case WM_SETFOCUS:
    case WM_KILLFOCUS: {
        LRESULT r = DefSubclassProc(h, m, w, l);
        SendMessageW(box, NB_SETFOCUS_NOTIFY, 0, 0);
        return r;
    }
    }
    return DefSubclassProc(h, m, w, l);
}

static void NumberFormat(NumberData* d) {
    if (!d || !d->edit) return;
    std::wstring valueText = std::to_wstring(d->value);
    d->suppress = true;
    SendMessageW(d->edit, WM_SETTEXT, 0, (LPARAM)valueText.c_str());
    d->suppress = false;
}

static void NumberNotify(HWND h) {
    SendMessageW(GetParent(h), WM_APP_NUMCHANGE,
                 (WPARAM)GetDlgCtrlID(h), (LPARAM)h);
}

static void NumberSetValue(HWND h, int value, bool notify) {
    NumberData* d = reinterpret_cast<NumberData*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!d) return;
    int v = ClampInt(value, d->minVal, d->maxVal);
    bool changed = v != d->value;
    d->value = v;
    NumberFormat(d);
    InvalidateRect(h, nullptr, FALSE);
    if (notify && changed) NumberNotify(h);
}

static void NumberStep(HWND h, int deltaSteps) {
    NumberData* d = reinterpret_cast<NumberData*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!d) return;
    int v = ClampInt(d->value + deltaSteps * d->step, d->minVal, d->maxVal);
    if (v == d->value) return;
    d->value = v;
    NumberFormat(d);
    InvalidateRect(h, nullptr, FALSE);
    NumberNotify(h);
}

static LRESULT CALLBACK NumberProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    NumberData* d = reinterpret_cast<NumberData*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_NCCREATE || m == WM_CREATE) {
        if (m == WM_NCCREATE) {
            CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(l);
            d = reinterpret_cast<NumberData*>(cs->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(d));
            return DefWindowProcW(h, m, w, l);
        }
        if (!d) return -1;
        d->edit = CreateWindowExW(0, L"EDIT", L"0",
                                  WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_RIGHT |
                                  ES_NUMBER | WS_TABSTOP,
                                  0, 0, 10, 10, h, (HMENU)1, g_hInst, nullptr);
        if (!d->edit) return -1;
        SendMessageW(d->edit, WM_SETFONT, (WPARAM)g_app->fontUi, TRUE);
        SendMessageW(d->edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                     MAKELPARAM(g_app->Px(6), g_app->Px(6)));
        SetWindowSubclass(d->edit, NumberEditProc, 1, (DWORD_PTR)h);
        NumberFormat(d);
        return 0;
    }
    if (!d) return DefWindowProcW(h, m, w, l);

    switch (m) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        DoubleBuffer db;
        if (!db.Begin(dc, rc.right, rc.bottom)) {
            EndPaint(h, &ps);
            return 0;
        }
        HDC mem = db.mem;
        const Palette& p = g_app->pal;
        FillSolid(mem, rc, p.bg);
        RECT body = rc;
        RoundRectPaint(mem, body, g_app->Px(4), p.input,
                       d->focused ? p.accent : p.border, 1);

        int arrowW = g_app->Px(19);
        int sepX = rc.right - arrowW;
        HPEN sep = CreatePen(PS_SOLID, 1, p.border);
        HGDIOBJ oldPen = SelectObject(mem, sep);
        MoveToEx(mem, sepX, rc.top + g_app->Px(4), nullptr);
        LineTo(mem, sepX, rc.bottom - g_app->Px(4));
        SelectObject(mem, oldPen);
        DeleteObject(sep);

        int mid = (rc.top + rc.bottom) / 2;
        bool canUp = d->value < d->maxVal;
        bool canDown = d->value > d->minVal;
        COLORREF upColor = (canUp ? (d->hotArea == 1 ? p.accent : p.muted) : p.border);
        COLORREF downColor = (canDown ? (d->hotArea == 2 ? p.accent : p.muted) : p.border);
        int cx = sepX + arrowW / 2;
        DrawChevron(mem, cx, mid - g_app->Px(6), g_app->Px(4), upColor, false);
        DrawChevron(mem, cx, mid + g_app->Px(6), g_app->Px(4), downColor, true);

        db.Blit(dc);
        db.End(dc);
        EndPaint(h, &ps);
        return 0;
    }

    case WM_SIZE: {
        RECT rc;
        GetClientRect(h, &rc);
        int arrowW = g_app->Px(19);
        int hgt = std::max(1, (int)(rc.bottom - g_app->Px(4)));
        SetWindowPos(d->edit, nullptr, g_app->Px(3), g_app->Px(2),
                     std::max(1, (int)(rc.right - arrowW - g_app->Px(4))), hgt - g_app->Px(2),
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(w);
        int code = HIWORD(w);
        if (id == 1 && code == EN_CHANGE) {
            if (d->suppress) return 0;
            wchar_t buf[64] = {};
            GetWindowTextW(d->edit, buf, 63);
            std::wstring txt = TrimW(buf);
            if (txt.empty()) return 0;
            wchar_t* end = nullptr;
            long v = wcstol(txt.c_str(), &end, 10);
            if (end == txt.c_str()) return 0;
            int nv = ClampInt((int)v, d->minVal, d->maxVal);
            bool changed = nv != d->value;
            d->value = nv;
            InvalidateRect(h, nullptr, FALSE);
            if (changed) NumberNotify(h);
            return 0;
        }
        if (id == 1 && code == EN_SETFOCUS) {
            d->focused = true;
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        if (id == 1 && code == EN_KILLFOCUS) {
            d->focused = false;
            NumberFormat(d);
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        break;
    }

    case NB_SETVALUE:
        NumberSetValue(h, (int)w, false);
        return 0;
    case NB_GETVALUE:
        return d->value;
    case NB_SETRANGE: {
        const int* r = reinterpret_cast<const int*>(l);
        if (r) {
            d->minVal = r[0];
            d->maxVal = r[1];
            d->step = std::max(1, r[2]);
        }
        if (d->value < d->minVal) d->value = d->minVal;
        if (d->value > d->maxVal) d->value = d->maxVal;
        NumberFormat(d);
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case NB_SETSTEP:
        d->step = std::max(1, (int)w);
        return 0;
    case NB_STEP:
        NumberStep(h, (int)w);
        return 0;
    case NB_GETEDIT:
        return (LRESULT)d->edit;
    case NB_SETFOCUS_NOTIFY:
        d->focused = (GetFocus() == d->edit);
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_MOUSEMOVE: {
        POINT pt{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        RECT rc;
        GetClientRect(h, &rc);
        int area = 0;
        int arrowW = g_app->Px(19);
        if (pt.x >= rc.right - arrowW) {
            area = (pt.y < (rc.top + rc.bottom) / 2) ? 1 : 2;
        }
        if (!d->hover) {
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = h;
            TrackMouseEvent(&tme);
            d->hover = true;
        }
        if (d->hotArea != area) {
            d->hotArea = area;
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        d->hover = false;
        if (d->hotArea != 0) {
            d->hotArea = 0;
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN: {
        SetFocus(d->edit);
        POINT pt{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        RECT rc;
        GetClientRect(h, &rc);
        int arrowW = g_app->Px(19);
        if (pt.x >= rc.right - arrowW) {
            NumberStep(h, pt.y < (rc.top + rc.bottom) / 2 ? 1 : -1);
        }
        return 0;
    }
    case WM_MOUSEWHEEL:
        NumberStep(h, GET_WHEEL_DELTA_WPARAM(w) > 0 ? 1 : -1);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT && d->hotArea != 0) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        break;
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)w;
        SetTextColor(dc, g_app->pal.text);
        SetBkColor(dc, g_app->pal.input);
        return (LRESULT)g_app->brInput;
    }
    case WM_NCDESTROY:
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        delete d;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static HWND CreateNumberBox(HWND parent, int id, int value, int lo, int hi, int step,
                            HWND* outEdit) {
    NumberData* d = new (std::nothrow) NumberData;
    if (!d) return nullptr;
    d->value = ClampInt(value, lo, hi);
    d->minVal = lo;
    d->maxVal = hi;
    d->step = step;
    HWND h = CreateWindowExW(0, kNumberClass, L"", WS_CHILD | WS_VISIBLE,
                             0, 0, 10, 10, parent, (HMENU)(INT_PTR)id, g_hInst, d);
    if (!h) {
        delete d;
        return nullptr;
    }
    int range[3] = { lo, hi, step };
    SendMessageW(h, NB_SETRANGE, 0, (LPARAM)range);
    SendMessageW(h, NB_SETVALUE, (WPARAM)value, 0);
    SendMessageW(h, NB_SETSTEP, (WPARAM)step, 0);
    if (outEdit) *outEdit = (HWND)SendMessageW(h, NB_GETEDIT, 0, 0);
    return h;
}

// ---------------------------------------------------------------------------
//  Word-row edit subclass (Tab / Enter / wheel / themed border)
// ---------------------------------------------------------------------------
static void PaintEditFrame(HWND h, bool accent) {
    if (!g_app) return;
    HDC dc = GetWindowDC(h);
    if (!dc) return;
    RECT wr;
    GetWindowRect(h, &wr);
    HBRUSH br = CreateSolidBrush(accent ? g_app->pal.accent : g_app->pal.border);
    RECT frame{ 0, 0, wr.right - wr.left, wr.bottom - wr.top };
    FrameRect(dc, &frame, br);
    DeleteObject(br);
    ReleaseDC(h, dc);
}

static LRESULT CALLBACK RowEditProc(HWND h, UINT m, WPARAM w, LPARAM l,
                                    UINT_PTR, DWORD_PTR) {
    switch (m) {
    case WM_KEYDOWN:
        if (w == VK_TAB) {
            FocusStep(h, (GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1);
            return 0;
        }
        if (w == VK_RETURN) {
            SendMessageW(GetParent(h), WM_APP_ROWENTER, 0, (LPARAM)h);
            return 0;
        }
        break;
    case WM_MOUSEWHEEL:
        SendMessageW(GetParent(h), WM_APP_PANELWHEEL, w, l);
        return 0;
    case WM_NCPAINT: {
        LRESULT r = DefSubclassProc(h, m, w, l);
        PaintEditFrame(h, GetFocus() == h);
        return r;
    }
    case WM_SETFOCUS:
    case WM_KILLFOCUS: {
        LRESULT r = DefSubclassProc(h, m, w, l);
        RedrawWindow(h, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE);
        return r;
    }
    }
    return DefSubclassProc(h, m, w, l);
}

// ---------------------------------------------------------------------------
//  UI fonts, brushes, tooltips
// ---------------------------------------------------------------------------
static HFONT CreateUiFont(int pointSize, int weight, int dpi) {
    LOGFONTW lf{};
    lf.lfHeight = -MulDiv(pointSize, dpi, 72);
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    lstrcpynW(lf.lfFaceName, g_uiFace.c_str(), LF_FACESIZE);
    return CreateFontIndirectW(&lf);
}

static void CreateAppFonts(App& a) {
    auto del = [](HFONT& f) { if (f) { DeleteObject(f); f = nullptr; } };
    del(a.fontUi);
    del(a.fontBold);
    del(a.fontSmall);
    del(a.fontSection);
    del(a.fontTitle);
    a.fontUi = CreateUiFont(10, FW_NORMAL, a.dpi);
    a.fontBold = CreateUiFont(10, FW_BOLD, a.dpi);
    a.fontSmall = CreateUiFont(9, FW_NORMAL, a.dpi);
    a.fontSection = CreateUiFont(10, FW_BOLD, a.dpi);
    a.fontTitle = CreateUiFont(13, FW_BOLD, a.dpi);
}

static void RebuildThemeBrushes(App& a) {
    auto del = [](HBRUSH& b) { if (b) { DeleteObject(b); b = nullptr; } };
    del(a.brInput);
    del(a.brBg);
    del(a.brPanel);
    del(a.brStatus);
    a.brInput = CreateSolidBrush(a.pal.input);
    a.brBg = CreateSolidBrush(a.pal.bg);
    a.brPanel = CreateSolidBrush(a.pal.panel);
    a.brStatus = CreateSolidBrush(a.pal.statusBg);
}

static void InitTooltip(App& a) {
    a.tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                            0, 0, 0, 0, a.hwnd, nullptr, g_hInst, nullptr);
    if (a.tip) {
        SendMessageW(a.tip, WM_SETFONT, (WPARAM)a.fontSmall, TRUE);
        SetWindowPos(a.tip, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

static void AddTip(App& a, HWND ctl, const wchar_t* text) {
    if (!a.tip || !ctl) return;
    TOOLINFOW ti{};
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = a.hwnd;
    ti.uId = (UINT_PTR)ctl;
    ti.lpszText = const_cast<LPWSTR>(text);
    SendMessageW(a.tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
}

// ---------------------------------------------------------------------------
//  Word list panel
// ---------------------------------------------------------------------------
static int PanelTopPad(App& a) { return a.Px(7); }
static int PanelRowH(App& a)   { return a.Px(34); }
static int PanelContentH(App& a) {
    return PanelTopPad(a) + (int)a.rows.size() * PanelRowH(a) + a.Px(7);
}

struct ScrollGeom {
    bool visible = false;
    RECT track{};
    RECT thumb{};
    int maxScroll = 0;
};

static ScrollGeom PanelScrollGeom(App& a) {
    ScrollGeom g;
    if (!a.panel) return g;
    RECT rc;
    GetClientRect(a.panel, &rc);
    int contentH = PanelContentH(a);
    g.maxScroll = std::max(0, (int)(contentH - rc.bottom));
    g.visible = g.maxScroll > 0;
    if (!g.visible) return g;
    int sbW = a.Px(9);
    int pad = a.Px(3);
    g.track = { rc.right - sbW - pad, a.Px(4), rc.right - pad, rc.bottom - a.Px(4) };
    int trackH = std::max(1, (int)(g.track.bottom - g.track.top));
    int thumbH = std::min(trackH,
        std::max(a.Px(26), (int)((long long)trackH * rc.bottom / contentH)));
    int maxThumbTop = std::max(0, trackH - thumbH);
    int thumbTop = g.track.top;
    if (g.maxScroll > 0) {
        thumbTop += (int)((long long)maxThumbTop * ClampInt(a.scrollY, 0, g.maxScroll) /
                          g.maxScroll);
    }
    g.thumb = { g.track.left, thumbTop, g.track.right, thumbTop + thumbH };
    return g;
}

static std::wstring GetControlText(HWND h) {
    int len = GetWindowTextLengthW(h);
    if (len <= 0) return L"";
    std::wstring s((size_t)len + 1, L'\0');
    int got = GetWindowTextW(h, &s[0], len + 1);
    s.resize((size_t)std::max(0, got));
    return s;
}

static void AssignRowIds(App& a) {
    for (size_t i = 0; i < a.rows.size(); ++i) {
        int base = 3000 + (int)i * 3;
        Row& r = a.rows[i];
        if (r.hWord) SetWindowLongPtrW(r.hWord, GWLP_ID, base);
        if (r.hMeaning) SetWindowLongPtrW(r.hMeaning, GWLP_ID, base + 1);
        if (r.hDelete) SetWindowLongPtrW(r.hDelete, GWLP_ID, base + 2);
    }
}

static void CreateRowControls(App& a, size_t index) {
    Row& r = a.rows[index];
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL;
    r.hWord = CreateWindowExW(0, L"EDIT", L"", style, 0, 0, 10, 10, a.panel,
                              (HMENU)(INT_PTR)(3000 + (int)index * 3), g_hInst, nullptr);
    r.hMeaning = CreateWindowExW(0, L"EDIT", L"", style, 0, 0, 10, 10, a.panel,
                                 (HMENU)(INT_PTR)(3000 + (int)index * 3 + 1),
                                 g_hInst, nullptr);
    if (r.hWord) {
        SendMessageW(r.hWord, WM_SETFONT, (WPARAM)a.fontUi, TRUE);
        SendMessageW(r.hWord, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                     MAKELPARAM(a.Px(6), a.Px(6)));
        SendMessageW(r.hWord, EM_SETCUEBANNER, TRUE, (LPARAM)L"外文单词");
        SetWindowSubclass(r.hWord, RowEditProc, 1, 0);
    }
    if (r.hMeaning) {
        SendMessageW(r.hMeaning, WM_SETFONT, (WPARAM)a.fontUi, TRUE);
        SendMessageW(r.hMeaning, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                     MAKELPARAM(a.Px(6), a.Px(6)));
        SendMessageW(r.hMeaning, EM_SETCUEBANNER, TRUE, (LPARAM)L"中文释义");
        SetWindowSubclass(r.hMeaning, RowEditProc, 2, 0);
    }
    r.hDelete = CreateUIButton(a.panel, 3000 + (int)index * 3 + 2, L"×", BTN_DANGER);
    if (r.hDelete) SendMessageW(r.hDelete, WM_SETFONT, (WPARAM)a.fontSmall, TRUE);
}

static void PanelLayout(App& a) {
    if (!a.panel) return;
    RECT rc;
    GetClientRect(a.panel, &rc);
    ScrollGeom sg = PanelScrollGeom(a);
    a.needScrollbar = sg.visible;
    a.scrollY = ClampInt(a.scrollY, 0, sg.maxScroll);

    int pad = a.Px(7);
    int gap = a.Px(6);
    int sbW = sg.visible ? a.Px(12) : 0;
    int innerW = std::max(a.Px(120), (int)rc.right - pad * 2 - sbW);
    int delW = a.Px(20);
    int wordW = std::max(a.Px(70), (innerW - delW - gap * 2) * 42 / 100);
    int meanW = std::max(a.Px(70), innerW - wordW - delW - gap * 2);
    int editH = PanelRowH(a) - a.Px(8);
    int delH = std::min(a.Px(22), editH);
    int top = PanelTopPad(a) - a.scrollY;

    for (size_t i = 0; i < a.rows.size(); ++i) {
        Row& r = a.rows[i];
        int y = top + (int)i * PanelRowH(a) + (PanelRowH(a) - editH) / 2;
        int x = pad;
        if (r.hWord) {
            SetWindowPos(r.hWord, nullptr, x, y, wordW, editH,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        x += wordW + gap;
        if (r.hMeaning) {
            SetWindowPos(r.hMeaning, nullptr, x, y, meanW, editH,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        x += meanW + gap;
        if (r.hDelete) {
            SetWindowPos(r.hDelete, nullptr, x, y + (editH - delH) / 2, delW, delH,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    InvalidateRect(a.panel, nullptr, FALSE);
}

static void PanelEnsureVisible(App& a, int row) {
    if (!a.panel || row < 0 || row >= (int)a.rows.size()) return;
    RECT rc;
    GetClientRect(a.panel, &rc);
    int top = PanelTopPad(a) + row * PanelRowH(a);
    int bottom = top + PanelRowH(a);
    int newScroll = a.scrollY;
    if (top - a.scrollY < PanelTopPad(a)) {
        newScroll = top - PanelTopPad(a);
    } else if (bottom - a.scrollY > rc.bottom - a.Px(4)) {
        newScroll = bottom - (rc.bottom - a.Px(4));
    }
    ScrollGeom sg = PanelScrollGeom(a);
    newScroll = ClampInt(newScroll, 0, sg.maxScroll);
    if (newScroll != a.scrollY) {
        a.scrollY = newScroll;
        PanelLayout(a);
    }
}

static void RebuildTabStops(App& a) {
    a.tabStops.clear();
    for (auto& r : a.rows) {
        if (r.hWord) a.tabStops.push_back(r.hWord);
        if (r.hMeaning) a.tabStops.push_back(r.hMeaning);
    }
    HWND extra[] = {
        a.fontEdit, a.posXEdit, a.posYEdit, a.btnFontColor, a.btnBgColor,
        a.presetBtn, a.resWEdit, a.resHEdit, a.themeDarkBtn, a.themeLightBtn,
        a.browseBtn, a.importBtn, a.openDirBtn, a.saveOnlyBtn, a.applyBtn
    };
    for (HWND h : extra) if (h) a.tabStops.push_back(h);
}

static void FocusStep(HWND current, int dir) {
    if (!g_app) return;
    std::vector<HWND>* tabList = &g_app->tabStops;
    if (g_app->aboutWindow && GetParent(current) == g_app->aboutWindow) {
        tabList = &g_app->aboutTabStops;
    }
    auto& tabs = *tabList;
    if (tabs.empty()) return;
    auto it = std::find(tabs.begin(), tabs.end(), current);
    int n = (int)tabs.size();
    int i = (it == tabs.end()) ? (dir > 0 ? -1 : 0) : (int)(it - tabs.begin());
    i = (i + dir + n) % n;
    SetFocus(tabs[(size_t)i]);
}

static bool RowEmpty(const Row& r);

static void FillDemoRows(App& a) {
    const wchar_t* data[][2] = {
        { L"personnel", L"员工" },
        { L"particular", L"特定的" },
        { L"put on", L"穿上" },
        { L"ordering", L"订购；排序" },
        { L"opportunity", L"机会" },
        { L"direction", L"方向" },
        { L"decoration", L"装饰" },
        { L"persuade", L"说服" },
        { L"moderate", L"中等的" },
        { L"essential", L"必不可少的" },
        { L"suspicion", L"怀疑" },
        { L"invention", L"发明" }
    };
    int count = (int)(sizeof(data) / sizeof(data[0]));
    for (int i = 0; i < count; ++i) {
        if (i >= (int)a.rows.size()) AddRow(a, false);
        Row& r = a.rows[(size_t)i];
        r.word = data[i][0];
        r.meaning = data[i][1];
        if (r.hWord) SendMessageW(r.hWord, WM_SETTEXT, 0, (LPARAM)r.word.c_str());
        if (r.hMeaning) SendMessageW(r.hMeaning, WM_SETTEXT, 0, (LPARAM)r.meaning.c_str());
    }
    if (a.rows.empty() || !RowEmpty(a.rows.back())) AddRow(a, false);
    a.activeRow = 0;
    RebuildTabStops(a);
    PanelLayout(a);
    SchedulePreview(a, 80);
}

static void AddRowAt(App& a, int index, bool focusNew) {
    index = ClampInt(index, 0, (int)a.rows.size());
    a.rows.insert(a.rows.begin() + index, Row{});
    CreateRowControls(a, (size_t)index);
    AssignRowIds(a);
    RebuildTabStops(a);
    PanelLayout(a);
    InvalidateRect(a.panel, nullptr, FALSE);
    SchedulePreview(a);
    if (focusNew) {
        a.activeRow = index;
        if (a.rows[(size_t)index].hWord) {
            SetFocus(a.rows[(size_t)index].hWord);
            PanelEnsureVisible(a, index);
        }
    }
}

static void AddRow(App& a, bool focusNew) {
    AddRowAt(a, (int)a.rows.size(), focusNew);
}

static bool RowEmpty(const Row& r) {
    return TrimW(r.word).empty() && TrimW(r.meaning).empty();
}

static void DeleteRow(App& a, int index) {
    if (index < 0 || index >= (int)a.rows.size()) return;
    HWND focused = GetFocus();
    Row& r = a.rows[index];
    bool hadFocus = (focused == r.hWord || focused == r.hMeaning);
    if (r.hWord) { RemoveWindowSubclass(r.hWord, RowEditProc, 1); DestroyWindow(r.hWord); }
    if (r.hMeaning) { RemoveWindowSubclass(r.hMeaning, RowEditProc, 2); DestroyWindow(r.hMeaning); }
    if (r.hDelete) DestroyWindow(r.hDelete);
    a.rows.erase(a.rows.begin() + index);

    if (a.rows.empty() || !RowEmpty(a.rows.back())) {
        a.suppressAutoAdd = true;
        AddRow(a, false);
        a.suppressAutoAdd = false;
    }

    AssignRowIds(a);
    RebuildTabStops(a);
    if (a.activeRow > index) --a.activeRow;
    else if (a.activeRow == index) a.activeRow = index;
    if (a.activeRow >= (int)a.rows.size()) a.activeRow = (int)a.rows.size() - 1;
    if (a.activeRow < 0) a.activeRow = 0;
    PanelLayout(a);
    PanelEnsureVisible(a, a.activeRow);
    if (hadFocus) {
        int target = ClampInt(index, 0, (int)a.rows.size() - 1);
        if (target >= 0 && a.rows[target].hWord) SetFocus(a.rows[target].hWord);
    }
    InvalidateRect(a.panel, nullptr, FALSE);
    SchedulePreview(a);
}

static void ScrollPanelBy(App& a, int delta) {
    ScrollGeom sg = PanelScrollGeom(a);
    int before = a.scrollY;
    a.scrollY = ClampInt(a.scrollY + delta, 0, sg.maxScroll);
    if (a.scrollY != before) PanelLayout(a);
}

static void PanelPaint(App& a, HDC dc) {
    RECT rc;
    GetClientRect(a.panel, &rc);
    DoubleBuffer db;
    if (!db.Begin(dc, rc.right, rc.bottom)) return;
    HDC mem = db.mem;
    const Palette& p = a.pal;
    FillSolid(mem, rc, p.panel);

    int top = PanelTopPad(a) - a.scrollY;
    int rowH = PanelRowH(a);
    int right = rc.right - (a.needScrollbar ? a.Px(12) : a.Px(4));

    if (a.activeRow >= 0 && a.activeRow < (int)a.rows.size()) {
        int y = top + a.activeRow * rowH;
        RECT ar{ a.Px(4), y + a.Px(2), right, y + rowH - a.Px(2) };
        if (ar.bottom > 0 && ar.top < rc.bottom) {
            FillSolid(mem, ar, p.rowActive);
            RECT bar{ a.Px(1), y + a.Px(5), a.Px(4), y + rowH - a.Px(5) };
            if (bar.bottom > 0 && bar.top < rc.bottom) FillSolid(mem, bar, p.accent);
        }
    }

    HPEN sep = CreatePen(PS_SOLID, 1, Blend(p.panel, p.border, 45));
    HGDIOBJ oldPen = SelectObject(mem, sep);
    for (size_t i = 1; i <= a.rows.size(); ++i) {
        int y = top + (int)i * rowH - 1;
        if (y < 0 || y >= rc.bottom) continue;
        MoveToEx(mem, a.Px(7), y, nullptr);
        LineTo(mem, right, y);
    }
    SelectObject(mem, oldPen);
    DeleteObject(sep);

    ScrollGeom sg = PanelScrollGeom(a);
    if (sg.visible) {
        RoundRectPaint(mem, sg.track, a.Px(4), Blend(p.panel, p.border, 30),
                       Blend(p.panel, p.border, 30), 1);
        RoundRectPaint(mem, sg.thumb, a.Px(4), p.scrollThumb, p.scrollThumb, 1);
    }

    HBRUSH border = CreateSolidBrush(p.border);
    FrameRect(mem, &rc, border);
    DeleteObject(border);
    db.Blit(dc);
    db.End(dc);
}

static LRESULT CALLBACK WordPanelProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    App* a = reinterpret_cast<App*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_NCCREATE) {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(l);
        a = reinterpret_cast<App*>(cs->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(a));
        return DefWindowProcW(h, m, w, l);
    }
    if (!a) return DefWindowProcW(h, m, w, l);

    switch (m) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        PanelPaint(*a, dc);
        EndPaint(h, &ps);
        return 0;
    }

    case WM_SIZE:
        PanelLayout(*a);
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(w);
        int code = HIWORD(w);
        if (id >= 3000) {
            int rel = id - 3000;
            int index = rel / 3;
            int field = rel % 3;
            if (index < 0 || index >= (int)a->rows.size()) return 0;
            Row& row = a->rows[index];
            if (field == 2) {
                if (code == BN_CLICKED) DeleteRow(*a, index);
                return 0;
            }
            if (code == EN_CHANGE) {
                std::wstring txt = GetControlText(field == 0 ? row.hWord : row.hMeaning);
                if (field == 0) row.word = txt; else row.meaning = txt;
                if (!a->suppressAutoAdd && index == (int)a->rows.size() - 1 &&
                    (!TrimW(row.word).empty() || !TrimW(row.meaning).empty())) {
                    a->suppressAutoAdd = true;
                    AddRow(*a, false);
                    a->suppressAutoAdd = false;
                    PanelEnsureVisible(*a, index);
                }
                SchedulePreview(*a);
                return 0;
            }
            if (code == EN_SETFOCUS) {
                a->activeRow = index;
                PanelEnsureVisible(*a, index);
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
            if (code == EN_KILLFOCUS) {
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
        }
        break;
    }

    case WM_APP_ROWENTER: {
        HWND edit = (HWND)l;
        for (size_t i = 0; i < a->rows.size(); ++i) {
            Row& r = a->rows[i];
            if (edit == r.hWord) {
                if (r.hMeaning) SetFocus(r.hMeaning);
                return 0;
            }
            if (edit == r.hMeaning) {
                if (i + 1 < a->rows.size()) {
                    if (a->rows[i + 1].hWord) {
                        SetFocus(a->rows[i + 1].hWord);
                        PanelEnsureVisible(*a, (int)i + 1);
                    }
                } else {
                    AddRow(*a, true);
                }
                return 0;
            }
        }
        return 0;
    }

    case WM_MOUSEWHEEL:
    case WM_APP_PANELWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(w);
        ScrollPanelBy(*a, -delta / WHEEL_DELTA * PanelRowH(*a) * 3);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        POINT pt{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        ScrollGeom sg = PanelScrollGeom(*a);
        if (sg.visible && PtInRect(&sg.track, pt)) {
            if (PtInRect(&sg.thumb, pt)) {
                a->dragScrollbar = true;
                a->dragScrollbarOffset = pt.y - sg.thumb.top;
                SetCapture(h);
            } else {
                RECT rc;
                GetClientRect(h, &rc);
                ScrollPanelBy(*a, (pt.y < sg.thumb.top ? -1 : 1) * (rc.bottom - a->Px(40)));
            }
        }
        return 0;
    }

    case WM_MOUSEMOVE:
        if (a->dragScrollbar && (w & MK_LBUTTON)) {
            ScrollGeom sg = PanelScrollGeom(*a);
            int trackH = std::max(1, (int)(sg.track.bottom - sg.track.top));
            int thumbH = std::max(1, (int)(sg.thumb.bottom - sg.thumb.top));
            int maxThumbTop = std::max(0, trackH - thumbH);
            int newTop = GET_Y_LPARAM(l) - a->dragScrollbarOffset;
            int rel = ClampInt(newTop - sg.track.top, 0, maxThumbTop);
            int newScroll = maxThumbTop > 0 ?
                (int)((long long)rel * sg.maxScroll / maxThumbTop) : 0;
            if (newScroll != a->scrollY) {
                a->scrollY = newScroll;
                PanelLayout(*a);
            }
        }
        return 0;

    case WM_LBUTTONUP:
        if (a->dragScrollbar) {
            a->dragScrollbar = false;
            if (GetCapture() == h) ReleaseCapture();
        }
        return 0;

    case WM_CAPTURECHANGED:
        a->dragScrollbar = false;
        return 0;

    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)w;
        SetTextColor(dc, a->pal.text);
        SetBkColor(dc, a->pal.input);
        return (LRESULT)a->brInput;
    }

    case WM_NCDESTROY:
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// ---------------------------------------------------------------------------
//  Live preview (draggable text origin + crosshair)
// ---------------------------------------------------------------------------
static Color GdipColor(COLORREF c, BYTE a = 255) {
    return Color(a, GetRValue(c), GetGValue(c), GetBValue(c));
}

static void UpdatePreviewInfo(App& a) {
    int x = a.draggingPos ? a.dragX : a.s.posX;
    int y = a.draggingPos ? a.dragY : a.s.posY;
    a.previewInfo = Fmt(L"X %d  ·  Y %d  ·  %d × %d", x, y, a.s.resW, a.s.resH);
    if (a.hwnd) InvalidateRect(a.hwnd, &a.L.previewHeader, FALSE);
}

static bool PreviewMapPoint(App& a, int px, int py, int* ix, int* iy) {
    RECT ir = a.imageRect;
    int w = ir.right - ir.left;
    int h = ir.bottom - ir.top;
    if (w <= 0 || h <= 0) return false;
    if (px < ir.left || px >= ir.right || py < ir.top || py >= ir.bottom) return false;
    int x = (int)std::lround((double)(px - ir.left) * a.s.resW / w);
    int y = (int)std::lround((double)(py - ir.top) * a.s.resH / h);
    if (ix) *ix = ClampInt(x, 0, a.s.resW - 1);
    if (iy) *iy = ClampInt(y, 0, a.s.resH - 1);
    return true;
}

static void RenderPreviewNow(App& a) {
    if (!a.preview || !a.hwnd) return;
    if (IsIconic(a.hwnd)) return;
    RECT rc;
    GetClientRect(a.preview, &rc);
    if (rc.right < 20 || rc.bottom < 20) return;

    int pad = a.Px(10);
    int availW = std::max(1, (int)rc.right - pad * 2);
    int availH = std::max(1, (int)rc.bottom - pad * 2);
    double scale = std::min((double)availW / a.s.resW, (double)availH / a.s.resH);
    scale = std::min(scale, 1.0);
    if (scale <= 0) return;
    int w = std::max(1, (int)std::lround(a.s.resW * scale));
    int h = std::max(1, (int)std::lround(a.s.resH * scale));
    if (w > availW) w = availW;
    if (h > availH) h = availH;

    Settings s2 = a.s;
    if (a.draggingPos) {
        s2.posX = a.dragX;
        s2.posY = a.dragY;
    }
    Bitmap* bmp = RenderImage(s2, a.rows, w, h, scale);
    if (!bmp) return;
    delete a.previewBmp;
    a.previewBmp = bmp;
    a.previewW = w;
    a.previewH = h;
    a.imageRect.left = (rc.right - w) / 2;
    a.imageRect.top = (rc.bottom - h) / 2;
    a.imageRect.right = a.imageRect.left + w;
    a.imageRect.bottom = a.imageRect.top + h;
    UpdatePreviewInfo(a);
    InvalidateRect(a.preview, nullptr, FALSE);
}

static void DrawPreviewCrosshair(Graphics& g, App& a, const RECT& ir,
                                 bool guides, int guideX, int guideY) {
    const Palette& p = a.pal;
    int x = a.draggingPos ? a.dragX : a.s.posX;
    int y = a.draggingPos ? a.dragY : a.s.posY;
    int iw = ir.right - ir.left;
    int ih = ir.bottom - ir.top;
    if (iw <= 0 || ih <= 0) return;
    int ox = ir.left + (int)std::lround((double)x * iw / a.s.resW);
    int oy = ir.top + (int)std::lround((double)y * ih / a.s.resH);

    if (guides) {
        Pen guide(GdipColor(p.accent, 210), 1.0f);
        guide.SetDashStyle(DashStyleDash);
        g.DrawLine(&guide, guideX, (int)ir.top, guideX, (int)ir.bottom);
        g.DrawLine(&guide, (int)ir.left, guideY, (int)ir.right, guideY);
    }

    Pen origin(GdipColor(p.accent, 255), 2.0f);
    int r = a.Px(6);
    g.DrawEllipse(&origin, ox - r, oy - r, r * 2, r * 2);
    int t = a.Px(10);
    g.DrawLine(&origin, ox - t, oy, ox + t, oy);
    g.DrawLine(&origin, ox, oy - t, ox, oy + t);
    SolidBrush dot(GdipColor(p.accent, 255));
    g.FillEllipse(&dot, ox - 2, oy - 2, 4, 4);
}

static void PreviewPaint(App& a, HDC dc) {
    RECT rc;
    GetClientRect(a.preview, &rc);
    DoubleBuffer db;
    if (!db.Begin(dc, rc.right, rc.bottom)) return;
    HDC mem = db.mem;
    FillSolid(mem, rc, a.pal.previewBg);

    if (a.previewBmp) {
        RECT ir = a.imageRect;
        {
            Graphics g(mem);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetInterpolationMode(InterpolationModeNearestNeighbor);
            g.SetPixelOffsetMode(PixelOffsetModeHalf);
            g.DrawImage(a.previewBmp, RectF((REAL)ir.left, (REAL)ir.top,
                                            (REAL)(ir.right - ir.left),
                                            (REAL)(ir.bottom - ir.top)));
            Pen border(GdipColor(a.pal.border, 255), 1.0f);
            g.DrawRectangle(&border, (INT)ir.left, (INT)ir.top,
                            (INT)(ir.right - ir.left - 1), (INT)(ir.bottom - ir.top - 1));

            bool dragging = a.draggingPos;
            bool hover = a.hoveringPreview;
            int gx = 0, gy = 0;
            if (dragging) {
                gx = ir.left + (int)std::lround(
                    (double)a.dragX * (ir.right - ir.left) / a.s.resW);
                gy = ir.top + (int)std::lround(
                    (double)a.dragY * (ir.bottom - ir.top) / a.s.resH);
            } else if (hover) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(a.preview, &pt);
                gx = pt.x;
                gy = pt.y;
            }
            DrawPreviewCrosshair(g, a, ir, dragging || hover, gx, gy);

            if (dragging) {
                int left = ClampInt(gx + a.Px(14), ir.left, ir.right - a.Px(150));
                int top = ClampInt(gy + a.Px(14), ir.top, ir.bottom - a.Px(30));
                RECT label{ left, top, left + a.Px(150), top + a.Px(24) };
                SolidBrush box(GdipColor(a.pal.statusBg, 235));
                g.FillRectangle(&box, (INT)label.left, (INT)label.top,
                                (INT)(label.right - label.left), (INT)(label.bottom - label.top));
            }
        }
        if (a.draggingPos) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(a.preview, &pt);
            int left = ClampInt(pt.x + a.Px(14), a.imageRect.left,
                                a.imageRect.right - a.Px(160));
            int top = ClampInt(pt.y + a.Px(14), a.imageRect.top,
                               a.imageRect.bottom - a.Px(28));
            std::wstring label = Fmt(L"X %d   Y %d", a.dragX, a.dragY);
            RECT lr{ left + a.Px(6), top, left + a.Px(160), top + a.Px(24) };
            DrawTextRect(mem, label, lr, a.pal.text, a.fontSmall,
                         DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
        }
    } else {
        RECT tr = rc;
        DrawTextRect(mem, L"预览准备中…", tr, a.pal.muted, a.fontSmall,
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    db.Blit(dc);
    db.End(dc);
}

static LRESULT CALLBACK PreviewProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    App* a = reinterpret_cast<App*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_NCCREATE) {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(l);
        a = reinterpret_cast<App*>(cs->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(a));
        return DefWindowProcW(h, m, w, l);
    }
    if (!a) return DefWindowProcW(h, m, w, l);

    switch (m) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        PreviewPaint(*a, dc);
        EndPaint(h, &ps);
        return 0;
    }

    case WM_SIZE:
        SchedulePreview(*a, 30);
        return 0;

    case WM_MOUSEMOVE: {
        if (a->draggingPos) {
            if (w & MK_LBUTTON) {
                int ix, iy;
                if (PreviewMapPoint(*a, GET_X_LPARAM(l), GET_Y_LPARAM(l), &ix, &iy)) {
                    if (ix != a->dragX || iy != a->dragY) {
                        a->dragX = ix;
                        a->dragY = iy;
                        a->dragMoved = true;
                        a->hoveringPreview = true;
                        RenderPreviewNow(*a);
                        UpdatePreviewInfo(*a);
                        InvalidateRect(h, nullptr, FALSE);
                    }
                }
            }
            return 0;
        }
        bool inside = false;
        int ix, iy;
        inside = PreviewMapPoint(*a, GET_X_LPARAM(l), GET_Y_LPARAM(l), &ix, &iy);
        if (inside && !a->hoveringPreview) {
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = h;
            TrackMouseEvent(&tme);
        }
        if (inside != a->hoveringPreview) {
            a->hoveringPreview = inside;
            InvalidateRect(h, nullptr, FALSE);
        } else if (inside) {
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        a->hoveringPreview = false;
        InvalidateRect(h, nullptr, FALSE);
        return 0;

    case WM_LBUTTONDOWN: {
        int ix, iy;
        if (!PreviewMapPoint(*a, GET_X_LPARAM(l), GET_Y_LPARAM(l), &ix, &iy)) return 0;
        SetFocus(h);
        SetCapture(h);
        a->draggingPos = true;
        a->dragMoved = false;
        a->dragX = ix;
        a->dragY = iy;
        a->hoveringPreview = true;
        RenderPreviewNow(*a);
        UpdatePreviewInfo(*a);
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }

    case WM_LBUTTONUP:
        if (a->draggingPos) {
            int ix = a->dragX, iy = a->dragY;
            PreviewMapPoint(*a, GET_X_LPARAM(l), GET_Y_LPARAM(l), &ix, &iy);
            a->s.posX = ix;
            a->s.posY = iy;
            a->draggingPos = false;
            if (GetCapture() == h) ReleaseCapture();
            if (a->posXBox) SendMessageW(a->posXBox, NB_SETVALUE, (WPARAM)ix, 0);
            if (a->posYBox) SendMessageW(a->posYBox, NB_SETVALUE, (WPARAM)iy, 0);
            SetStatus(*a, Fmt(L"文字起始位置：X=%d，Y=%d", ix, iy));
            SetTimer(a->hwnd, TIMER_SAVE, 700, nullptr);
            RenderPreviewNow(*a);
            UpdatePreviewInfo(*a);
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;

    case WM_CAPTURECHANGED:
        if (a->draggingPos) {
            a->draggingPos = false;
            RenderPreviewNow(*a);
            UpdatePreviewInfo(*a);
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;

    case WM_KEYDOWN:
        if (w == VK_ESCAPE && a->draggingPos) {
            a->draggingPos = false;
            if (GetCapture() == h) ReleaseCapture();
            RenderPreviewNow(*a);
            UpdatePreviewInfo(*a);
            InvalidateRect(h, nullptr, FALSE);
            SetStatus(*a, L"已取消拖动");
            return 0;
        }
        break;

    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT) {
            SetCursor(LoadCursorW(nullptr, IDC_CROSS));
            return TRUE;
        }
        break;

    case WM_NCDESTROY:
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// ---------------------------------------------------------------------------
//  Layout
// ---------------------------------------------------------------------------
static void Relayout(App& a) {
    if (!a.hwnd) return;
    RECT rc;
    GetClientRect(a.hwnd, &rc);
    if (rc.right < 100 || rc.bottom < 100) return;
    Layout& L = a.L;
    L.client = rc;
    L.dpi = a.dpi;

    int m = a.Px(16);
    int statusH = a.Px(28);
    int actionH = a.Px(58);
    int contentBottom = rc.bottom - statusH - actionH;
    int leftW = ClampInt((int)((rc.right - 2 * m) * 0.36), a.Px(340), a.Px(470));
    int maxLeft = std::max(a.Px(300), (int)(rc.right - 2 * m) - a.Px(380));
    if (leftW > maxLeft) leftW = maxLeft;

    int hdrH = a.Px(20);
    int rowH = a.Px(30);
    int gap = a.Px(6);
    int addRowH = a.Px(30);
    int hintH = a.Px(18);

    L.wordHeader = { m, m, m + leftW, m + hdrH };
    int y = m + a.Px(26);

    int fixedBelow = a.Px(8) + addRowH + a.Px(4) + hintH + a.Px(14) + hdrH + a.Px(6)
                   + (rowH + gap) * 4
                   + rowH + a.Px(8) + rowH + a.Px(16) + hdrH + a.Px(6) + rowH;
    int panelBottom = contentBottom - a.Px(4);
    int panelH = std::max(a.Px(96), panelBottom - y - fixedBelow);
    L.panel = { m, y, m + leftW, y + panelH };

    y = L.panel.bottom + a.Px(8);
    L.addRowBtn = { m, y, m + a.Px(112), y + addRowH };
    y += addRowH + a.Px(4);
    L.wordHint = { m, y, m + leftW, y + hintH };
    y += hintH + a.Px(14);

    L.styleHeader = { m, y, m + leftW, y + hdrH };
    y += hdrH + a.Px(6);

    L.fontLabel = { m, y, m + a.Px(80), y + rowH };
    L.fontBox = { m + a.Px(84), y + a.Px(1), m + a.Px(180), y + rowH - a.Px(1) };
    y += rowH + gap;

    L.posLabel = { m, y, m + a.Px(80), y + rowH };
    L.posXLabel = { m + a.Px(84), y, m + a.Px(96), y + rowH };
    L.posXBox = { m + a.Px(98), y + a.Px(1), m + a.Px(174), y + rowH - a.Px(1) };
    L.posYLabel = { m + a.Px(184), y, m + a.Px(196), y + rowH };
    L.posYBox = { m + a.Px(198), y + a.Px(1), m + a.Px(274), y + rowH - a.Px(1) };
    y += rowH + gap;

    L.colorLabel = { m, y, m + a.Px(80), y + rowH };
    int colorX = m + a.Px(84);
    int colorW = std::max(a.Px(100), (leftW - a.Px(84) - gap) / 2);
    L.fontColorBtn = { colorX, y, colorX + colorW, y + rowH };
    L.bgColorBtn = { colorX + colorW + gap, y, colorX + colorW * 2 + gap, y + rowH };
    y += rowH + gap;

    L.presetLabel = { m, y, m + a.Px(80), y + rowH };
    L.presetBtn = { m + a.Px(84), y, m + leftW, y + rowH };
    y += rowH + gap;

    L.resLabel = { m, y, m + a.Px(80), y + rowH };
    L.resWLabel = { m + a.Px(84), y, m + a.Px(96), y + rowH };
    L.resWBox = { m + a.Px(98), y + a.Px(1), m + a.Px(174), y + rowH - a.Px(1) };
    L.resHLabel = { m + a.Px(184), y, m + a.Px(196), y + rowH };
    L.resHBox = { m + a.Px(198), y + a.Px(1), m + a.Px(274), y + rowH - a.Px(1) };
    y += rowH + a.Px(8);

    L.themeLabel = { m, y, m + a.Px(80), y + rowH };
    L.themeDarkBtn = { m + a.Px(84), y, m + a.Px(84) + a.Px(84), y + rowH };
    L.themeLightBtn = { m + a.Px(84) + a.Px(90), y,
                        m + a.Px(84) + a.Px(174), y + rowH };
    L.aboutBtn = { m + a.Px(270), y, m + a.Px(336), y + rowH };
    y += rowH + a.Px(16);

    L.saveHeader = { m, y, m + leftW, y + hdrH };
    y += hdrH + a.Px(6);
    int browseW = a.Px(64);
    L.pathField = { m, y, m + leftW - browseW - a.Px(8), y + rowH };
    L.browseBtn = { m + leftW - browseW, y, m + leftW, y + rowH };

    int rx0 = m + leftW + a.Px(18);
    int rx1 = rc.right - m;
    L.previewHeader = { rx0, m, rx1, m + hdrH };
    L.previewHint = { rx0, contentBottom - a.Px(22), rx1, contentBottom };
    L.preview = { rx0, m + a.Px(26), rx1, contentBottom - a.Px(28) };

    L.actionBar = { 0, rc.bottom - statusH - actionH, rc.right, rc.bottom - statusH };
    L.statusBar = { 0, rc.bottom - statusH, rc.right, rc.bottom };

    int bh = a.Px(38);
    int by = L.actionBar.top + (actionH - bh) / 2;
    int applyW = a.Px(190);
    int saveW = a.Px(132);
    int openW = a.Px(138);
    int bgap = a.Px(10);
    L.applyBtn = { rc.right - m - applyW, by, rc.right - m, by + bh };
    L.saveOnlyBtn = { L.applyBtn.left - bgap - saveW, by,
                      L.applyBtn.left - bgap, by + bh };
    L.openDirBtn = { L.saveOnlyBtn.left - bgap - openW, by,
                     L.saveOnlyBtn.left - bgap, by + bh };
    int importW = a.Px(112);
    L.importBtn = { L.openDirBtn.left - bgap - importW, by,
                    L.openDirBtn.left - bgap, by + bh };

    auto move = [](HWND h, const RECT& r) {
        if (!h) return;
        SetWindowPos(h, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    };
    move(a.panel, L.panel);
    move(a.preview, L.preview);
    move(a.addBtn, L.addRowBtn);
    move(a.fontBox, L.fontBox);
    move(a.posXBox, L.posXBox);
    move(a.posYBox, L.posYBox);
    move(a.btnFontColor, L.fontColorBtn);
    move(a.btnBgColor, L.bgColorBtn);
    move(a.presetBtn, L.presetBtn);
    move(a.resWBox, L.resWBox);
    move(a.resHBox, L.resHBox);
    move(a.themeDarkBtn, L.themeDarkBtn);
    move(a.themeLightBtn, L.themeLightBtn);
    move(a.aboutBtn, L.aboutBtn);
    move(a.browseBtn, L.browseBtn);
    move(a.importBtn, L.importBtn);
    move(a.openDirBtn, L.openDirBtn);
    move(a.saveOnlyBtn, L.saveOnlyBtn);
    move(a.applyBtn, L.applyBtn);

    InvalidateRect(a.hwnd, nullptr, FALSE);
}

// ---------------------------------------------------------------------------
//  Painting helpers for the main window
// ---------------------------------------------------------------------------
static void DrawSectionHeader(App& a, HDC dc, const RECT& r, const std::wstring& text) {
    if (r.right <= r.left || r.bottom <= r.top) return;
    int h = r.bottom - r.top;
    int barW = std::max(2, a.Px(3));
    int barH = std::max(8, (int)(h * 0.55));
    RECT bar{ r.left, r.top + (h - barH) / 2, r.left + barW, r.top + (h + barH) / 2 };
    FillSolid(dc, bar, a.pal.accent);
    RECT tr = r;
    tr.left += a.Px(9);
    DrawTextRect(dc, text, tr, a.pal.text, a.fontSection,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

static void DrawLabel(App& a, HDC dc, const RECT& r, const std::wstring& text,
                      COLORREF color, HFONT font, UINT flags) {
    (void)a;
    DrawTextRect(dc, text, r, color, font, flags);
}

static std::wstring ColorButtonText(const wchar_t* prefix, COLORREF color) {
    return std::wstring(prefix) + L"  " + ColorToHex(color);
}

static void UpdateColorButtons(App& a) {
    if (a.btnFontColor) {
        BtnSetText(a.btnFontColor, ColorButtonText(L"字体", a.s.fontColor));
        BtnSetSwatch(a.btnFontColor, a.s.fontColor);
    }
    if (a.btnBgColor) {
        BtnSetText(a.btnBgColor, ColorButtonText(L"背景", a.s.bgColor));
        BtnSetSwatch(a.btnBgColor, a.s.bgColor);
    }
}

static void MainPaint(App& a, HDC dc) {
    RECT rc;
    GetClientRect(a.hwnd, &rc);
    DoubleBuffer db;
    if (!db.Begin(dc, rc.right, rc.bottom)) return;
    HDC mem = db.mem;
    const Palette& p = a.pal;
    const Layout& L = a.L;

    FillSolid(mem, rc, p.bg);

    DrawSectionHeader(a, mem, L.wordHeader, L"单词");
    DrawSectionHeader(a, mem, L.styleHeader, L"样式");
    DrawSectionHeader(a, mem, L.saveHeader, L"保存位置");
    DrawSectionHeader(a, mem, L.previewHeader, L"预览");

    if (!a.previewInfo.empty()) {
        RECT ir = L.previewHeader;
        ir.left = ir.right - a.Px(260);
        DrawLabel(a, mem, ir, a.previewInfo, p.muted, a.fontSmall,
                  DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    DrawLabel(a, mem, L.wordHint, L"在最后一行输入会自动新增一行", p.muted, a.fontSmall,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    DrawLabel(a, mem, L.previewHint, L"拖动预览画面调整文字起始位置，松开后生效",
              p.muted, a.fontSmall, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    UINT lf = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
    DrawLabel(a, mem, L.fontLabel, L"字体大小", p.text, a.fontUi, lf);
    RECT unit{ L.fontBox.right + a.Px(8), L.fontBox.top, L.fontBox.right + a.Px(48),
               L.fontBox.bottom };
    DrawLabel(a, mem, unit, L"像素", p.muted, a.fontSmall, lf);

    DrawLabel(a, mem, L.posLabel, L"起始位置", p.text, a.fontUi, lf);
    DrawLabel(a, mem, L.posXLabel, L"X", p.muted, a.fontSmall,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawLabel(a, mem, L.posYLabel, L"Y", p.muted, a.fontSmall,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawLabel(a, mem, L.colorLabel, L"颜色", p.text, a.fontUi, lf);
    DrawLabel(a, mem, L.presetLabel, L"比例预设", p.text, a.fontUi, lf);
    DrawLabel(a, mem, L.resLabel, L"分辨率", p.text, a.fontUi, lf);
    DrawLabel(a, mem, L.resWLabel, L"W", p.muted, a.fontSmall,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawLabel(a, mem, L.resHLabel, L"H", p.muted, a.fontSmall,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawLabel(a, mem, L.themeLabel, L"界面主题", p.text, a.fontUi, lf);

    RECT pf = L.pathField;
    RoundRectPaint(mem, pf, a.Px(4), p.input, p.border, 1);
    RECT tr = pf;
    tr.left += a.Px(8);
    tr.right -= a.Px(8);
    if (a.s.addr.empty()) {
        DrawLabel(a, mem, tr, L"尚未设置 · 生成壁纸前会提示选择文件夹", p.accent,
                  a.fontSmall, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    } else {
        DrawLabel(a, mem, tr, a.s.addr, p.text, a.fontSmall,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_PATH_ELLIPSIS);
    }

    int actionTop = L.actionBar.top;
    HPEN apen = CreatePen(PS_SOLID, 1, p.border);
    HGDIOBJ oldPen = SelectObject(mem, apen);
    MoveToEx(mem, 0, actionTop, nullptr);
    LineTo(mem, rc.right, actionTop);
    SelectObject(mem, oldPen);
    DeleteObject(apen);
    RECT actionHint{ a.Px(16), actionTop, L.importBtn.left - a.Px(12), L.actionBar.bottom };
    DrawLabel(a, mem, actionHint, L"JPEG 质量 50% · 以单词命名保存", p.muted, a.fontSmall,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    FillSolid(mem, L.statusBar, p.statusBg);
    HPEN spen = CreatePen(PS_SOLID, 1, p.border);
    oldPen = SelectObject(mem, spen);
    MoveToEx(mem, 0, L.statusBar.top, nullptr);
    LineTo(mem, rc.right, L.statusBar.top);
    SelectObject(mem, oldPen);
    DeleteObject(spen);
    RECT st = L.statusBar;
    st.left += a.Px(16);
    st.right -= a.Px(220);
    DrawLabel(a, mem, st, a.status, a.statusError ? (p.dark ? RGB(240, 140, 140) : RGB(160, 50, 40))
                                                  : p.text,
              a.fontSmall, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    RECT rightInfo = L.statusBar;
    rightInfo.right -= a.Px(16);
    rightInfo.left = rightInfo.right - a.Px(200);
    DrawLabel(a, mem, rightInfo,
              Fmt(L"JPEG %d%%  ·  %d × %d", kJpegQuality, a.s.resW, a.s.resH),
              p.muted, a.fontSmall, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    db.Blit(dc);
    db.End(dc);
}

// ---------------------------------------------------------------------------
//  Status / settings / preview scheduling
// ---------------------------------------------------------------------------
static void SetStatus(App& a, const std::wstring& text, bool isError) {
    a.status = text;
    a.statusError = isError;
    if (a.hwnd) InvalidateRect(a.hwnd, &a.L.statusBar, FALSE);
}

static void SchedulePreview(App& a, int delayMs) {
    if (!a.hwnd) return;
    if (a.previewTimer) KillTimer(a.hwnd, TIMER_PREVIEW);
    a.previewTimer = SetTimer(a.hwnd, TIMER_PREVIEW, (UINT)std::max(1, delayMs), nullptr);
}

static void ScheduleSave(App& a) {
    if (a.hwnd) SetTimer(a.hwnd, TIMER_SAVE, 700, nullptr);
}

static void SaveSettingsNow(App& a) {
    if (a.configPath.empty()) a.configPath = ConfigPathForApp();
    if (a.hwnd && !IsIconic(a.hwnd) && !IsZoomed(a.hwnd)) {
        RECT cr;
        GetClientRect(a.hwnd, &cr);
        a.s.winW = ClampInt(MulDiv(cr.right, 96, a.dpi), kMinW, 10000);
        a.s.winH = ClampInt(MulDiv(cr.bottom, 96, a.dpi), kMinH, 10000);
    }
    if (a.hwnd) a.s.maximized = IsZoomed(a.hwnd) != FALSE;
    if (SaveSettings(a.s, a.configPath)) {
        a.s.hasConfig = true;
        a.firstRun = false;
    } else {
        SetStatus(a, L"无法写入配置文件（程序目录可能不可写）", true);
    }
}

static void SetTheme(App& a, bool dark) {
    BtnSetChecked(a.themeDarkBtn, dark);
    BtnSetChecked(a.themeLightBtn, !dark);
    if (a.s.dark == dark) return;
    a.s.dark = dark;
    a.pal = MakePalette(dark);
    RebuildThemeBrushes(a);
    UpdateTitleBarTheme(a);
    RedrawWindow(a.hwnd, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
    SetStatus(a, dark ? L"已切换到深色主题" : L"已切换到暖白护眼主题");
    ScheduleSave(a);
}

// ---------------------------------------------------------------------------
//  Folder / colour dialogs
// ---------------------------------------------------------------------------
static bool BrowseFolder(HWND owner, const std::wstring& title, std::wstring& out) {
    wchar_t buf[MAX_PATH * 2] = {};
    BROWSEINFOW bi{};
    bi.hwndOwner = owner;
    bi.lpszTitle = title.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_EDITBOX | BIF_USENEWUI;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return false;
    bool ok = SHGetPathFromIDListW(pidl, buf) != FALSE;
    CoTaskMemFree(pidl);
    if (!ok) return false;
    out = buf;
    return !out.empty();
}

static void BrowseForSaveDir(App& a) {
    std::wstring dir;
    if (!BrowseFolder(a.hwnd, L"选择壁纸图片的保存位置", dir)) {
        SetStatus(a, L"已取消选择保存位置");
        return;
    }
    a.s.addr = dir;
    a.s.hasConfig = true;
    SaveSettingsNow(a);
    SetStatus(a, L"保存位置已设置：" + a.s.addr);
    InvalidateRect(a.hwnd, &a.L.pathField, FALSE);
}

static bool EnsureSaveAddress(App& a, bool firstUse) {
    if (DirExists(a.s.addr)) return true;
    if (!a.s.addr.empty()) a.s.addr.clear();
    std::wstring dir;
    if (!BrowseFolder(a.hwnd, firstUse ? L"首次使用：请选择壁纸图片的保存位置"
                                       : L"请选择壁纸图片的保存位置", dir)) {
        SetStatus(a, L"已取消：尚未设置保存位置", true);
        return false;
    }
    a.s.addr = dir;
    a.s.hasConfig = true;
    SaveSettingsNow(a);
    InvalidateRect(a.hwnd, &a.L.pathField, FALSE);
    return true;
}

static void SetColorAndPreview(App& a, bool fontColor) {
    static COLORREF custom[16] = {};
    CHOOSECOLORW cc{};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = a.hwnd;
    cc.rgbResult = fontColor ? a.s.fontColor : a.s.bgColor;
    cc.lpCustColors = custom;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (!ChooseColorW(&cc)) return;
    if (fontColor) a.s.fontColor = cc.rgbResult;
    else a.s.bgColor = cc.rgbResult;
    UpdateColorButtons(a);
    SchedulePreview(a, 10);
    SetStatus(a, std::wstring(fontColor ? L"字体颜色：" : L"背景颜色：") +
                 ColorToHex(cc.rgbResult));
    ScheduleSave(a);
}

static void ShowOpenFolder(App& a) {
    if (!DirExists(a.s.addr)) {
        BrowseForSaveDir(a);
        return;
    }
    ShellExecuteW(a.hwnd, L"open", a.s.addr.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// ---------------------------------------------------------------------------
//  Presets and number changes
// ---------------------------------------------------------------------------
static void SyncPresetFromSize(App& a) {
    if (!a.presetBtn) return;
    int idx = FindMatchingPreset(a.s.resW, a.s.resH);
    BtnSetText(a.presetBtn, PresetDisplay(idx) + L"    ▾");
}

static void ApplyResPreset(App& a, int presetIndex) {
    if (presetIndex < 0 || presetIndex >= kPresetCount) return;
    const ResolutionPreset& pr = kPresets[presetIndex];
    a.s.resW = pr.w;
    a.s.resH = pr.h;
    if (a.resWBox) SendMessageW(a.resWBox, NB_SETVALUE, (WPARAM)pr.w, 0);
    if (a.resHBox) SendMessageW(a.resHBox, NB_SETVALUE, (WPARAM)pr.h, 0);
    SchedulePreview(a, 10);
    ScheduleSave(a);
    SetStatus(a, Fmt(L"分辨率：%s  %d × %d", pr.ratio, pr.w, pr.h));
    InvalidateRect(a.hwnd, &a.L.statusBar, FALSE);
    UpdatePreviewInfo(a);
}

static void ShowPresetMenu(App& a) {
    if (!a.presetBtn) return;
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, 2001, L"自定义（保留当前数值）");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    for (int i = 0; i < kPresetCount; ++i) {
        AppendMenuW(menu, MF_STRING, 2100 + i, PresetDisplay(i).c_str());
    }
    int current = FindMatchingPreset(a.s.resW, a.s.resH);
    if (current >= 0) {
        CheckMenuRadioItem(menu, 2100, 2100 + kPresetCount - 1, 2100 + current,
                           MF_BYCOMMAND);
    }
    RECT rc;
    GetWindowRect(a.presetBtn, &rc);
    int cmd = (int)TrackPopupMenu(menu,
        TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
        rc.left, rc.bottom + 2, 0, a.hwnd, nullptr);
    DestroyMenu(menu);
    if (cmd >= 2100 && cmd < 2100 + kPresetCount) {
        ApplyResPreset(a, cmd - 2100);
    }
}

static void OnNumberChange(App& a, int id, int value) {
    switch (id) {
    case IDC_FONT_SIZE:
        a.s.fontSize = ClampInt(value, kFontMin, kFontMax);
        break;
    case IDC_POS_X:
        a.s.posX = ClampInt(value, kPosMin, kPosMax);
        break;
    case IDC_POS_Y:
        a.s.posY = ClampInt(value, kPosMin, kPosMax);
        break;
    case IDC_RES_W:
        a.s.resW = ClampInt(value, kResMin, kResMax);
        SyncPresetFromSize(a);
        break;
    case IDC_RES_H:
        a.s.resH = ClampInt(value, kResMin, kResMax);
        SyncPresetFromSize(a);
        break;
    default:
        return;
    }
    SchedulePreview(a);
    ScheduleSave(a);
    if (id == IDC_RES_W || id == IDC_RES_H) UpdatePreviewInfo(a);
}

// ---------------------------------------------------------------------------
//  Save + apply
// ---------------------------------------------------------------------------
static bool DoSave(App& a, bool apply) {
    if ((long long)a.s.resW * a.s.resH > kMaxPixels) {
        MessageBoxW(a.hwnd,
                    L"当前分辨率过大，可能占用过多内存。\n请减小分辨率后再试。",
                    L"单词壁纸生成器", MB_OK | MB_ICONWARNING);
        SetStatus(a, L"分辨率过大，已取消", true);
        return false;
    }
    bool hasWord = false;
    for (const auto& r : a.rows) {
        if (!TrimW(r.word).empty()) { hasWord = true; break; }
    }
    if (!hasWord) {
        MessageBoxW(a.hwnd, L"请先在“单词”栏输入至少一个单词。",
                    L"单词壁纸生成器", MB_OK | MB_ICONINFORMATION);
        SetStatus(a, L"还没有输入单词", true);
        return false;
    }

    if (!EnsureSaveAddress(a, a.firstRun || !a.s.hasConfig)) return false;

    std::wstring name = BuildFileName(a.rows);
    std::wstring path = JoinPath(a.s.addr, name);

    Bitmap* bmp = RenderImage(a.s, a.rows, a.s.resW, a.s.resH, 1.0);
    if (!bmp) {
        MessageBoxW(a.hwnd, L"生成图片失败（内存不足或图形接口异常）。",
                    L"单词壁纸生成器", MB_OK | MB_ICONERROR);
        SetStatus(a, L"生成图片失败", true);
        return false;
    }
    unsigned long long bytes = 0;
    bool saved = SaveJpeg(bmp, path, kJpegQuality, &bytes);
    delete bmp;
    if (!saved) {
        MessageBoxW(a.hwnd,
                    (L"无法写入图片文件：\n" + path +
                     L"\n\n请检查文件夹是否存在、是否有写入权限。").c_str(),
                    L"单词壁纸生成器", MB_OK | MB_ICONERROR);
        SetStatus(a, L"保存失败：" + name, true);
        return false;
    }

    // Insert EXIF UserComment (Windows "Comments") and the JPEG COM copy
    // that this program reads when importing.
    std::wstring projectText = BuildProjectComment(a.s, a.rows);
    bool metaBuilt = !projectText.empty();
    bool comOk = false, exifOk = false, writeOk = false;
    bool metaOk = false;
    if (metaBuilt) {
        std::string comment = WideToUtf8(projectText);
        writeOk = InjectProjectMetadata(path, projectText, comment, comOk, exifOk);
        metaOk = writeOk && comOk;
    }
    (void)exifOk;
    unsigned long long finalBytes = FileSizeBytes(path);
    if (finalBytes) bytes = finalBytes;
    if (!metaOk) {
        MessageBoxW(a.hwnd,
                    metaBuilt
                        ? L"图片已保存，但工程备注写入失败，之后可能无法从这张图片恢复单词和样式。\n\n"
                          L"图片本身仍可正常使用。"
                        : L"单词内容过多，工程备注未能写入（图片仍已正常保存）。\n\n"
                          L"之后可能无法从这张图片恢复完整的单词列表。",
                    L"单词壁纸生成器", MB_OK | MB_ICONWARNING);
    }

    std::wstring msg = Fmt(L"已保存 %s（%d × %d，%s）",
                           name.c_str(), a.s.resW, a.s.resH,
                           FormatBytes(bytes).c_str());
    msg += metaOk ? L" · 工程备注已写入" : L" · 备注未写入";
    if (apply) {
        if (SetDesktopWallpaper(path)) {
            msg += L" · 已设为壁纸";
        } else {
            DWORD err = GetLastError();
            SetStatus(a, msg + L" · 设置壁纸失败", true);
            MessageBoxW(a.hwnd,
                        (L"图片已保存，但设置桌面壁纸失败。\n错误代码：" +
                         std::to_wstring(err)).c_str(),
                        L"单词壁纸生成器", MB_OK | MB_ICONWARNING);
            return false;
        }
    }
    SetStatus(a, msg, !metaOk);
    return true;
}

// ---------------------------------------------------------------------------
//  Control creation
// ---------------------------------------------------------------------------
static void ApplyFontsToControls(App& a) {
    for (auto& r : a.rows) {
        if (r.hWord) SendMessageW(r.hWord, WM_SETFONT, (WPARAM)a.fontUi, TRUE);
        if (r.hMeaning) SendMessageW(r.hMeaning, WM_SETFONT, (WPARAM)a.fontUi, TRUE);
        if (r.hDelete) SendMessageW(r.hDelete, WM_SETFONT, (WPARAM)a.fontSmall, TRUE);
    }
    HWND edits[] = { a.fontEdit, a.posXEdit, a.posYEdit, a.resWEdit, a.resHEdit };
    for (HWND h : edits) {
        if (h) SendMessageW(h, WM_SETFONT, (WPARAM)a.fontUi, TRUE);
    }
    if (a.tip) SendMessageW(a.tip, WM_SETFONT, (WPARAM)a.fontSmall, TRUE);
}

static void AddTooltips(App& a) {
    AddTip(a, a.fontBox, L"字体大小（像素），与微软雅黑粗体一致");
    AddTip(a, a.posXBox, L"文字起始位置 X（像素），也可直接在预览中拖动");
    AddTip(a, a.posYBox, L"文字起始位置 Y（像素），也可直接在预览中拖动");
    AddTip(a, a.btnFontColor, L"选择文字颜色");
    AddTip(a, a.btnBgColor, L"选择背景颜色");
    AddTip(a, a.presetBtn, L"常见屏幕比例 / 分辨率预设");
    AddTip(a, a.resWBox, L"输出图片宽度（像素）");
    AddTip(a, a.resHEdit, L"输出图片高度（像素）");
    AddTip(a, a.themeDarkBtn, L"深色主题（默认）");
    AddTip(a, a.themeLightBtn, L"暖白护眼主题");
    AddTip(a, a.aboutBtn, L"关于 WordsWallpaper · 版本、作者与链接");
    AddTip(a, a.browseBtn, L"设置图片保存位置");
    AddTip(a, a.addBtn, L"在所选行下方插入一行；在最后一行输入也会自动新增一行");
    AddTip(a, a.importBtn, L"读取图片备注中的单词和样式（Ctrl+O）");
    AddTip(a, a.openDirBtn, L"在资源管理器中打开保存文件夹");
    AddTip(a, a.saveOnlyBtn, L"生成 JPEG 图片但不修改桌面壁纸（Ctrl+S）");
    AddTip(a, a.applyBtn, L"生成 JPEG（质量 50%）并设为桌面壁纸（Ctrl+Enter）");
}

static void CreateControls(App& a) {
    a.panel = CreateWindowExW(0, kPanelClass, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, 10, 10, a.hwnd, (HMENU)IDC_WORD_PANEL, g_hInst, &a);
    a.preview = CreateWindowExW(0, kPreviewClass, L"",
        WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, a.hwnd,
        (HMENU)IDC_PREVIEW, g_hInst, &a);

    a.fontBox = CreateNumberBox(a.hwnd, IDC_FONT_SIZE, a.s.fontSize,
                                kFontMin, kFontMax, 5, &a.fontEdit);
    a.posXBox = CreateNumberBox(a.hwnd, IDC_POS_X, a.s.posX,
                                kPosMin, kPosMax, 5, &a.posXEdit);
    a.posYBox = CreateNumberBox(a.hwnd, IDC_POS_Y, a.s.posY,
                                kPosMin, kPosMax, 5, &a.posYEdit);
    a.resWBox = CreateNumberBox(a.hwnd, IDC_RES_W, a.s.resW,
                                kResMin, kResMax, 10, &a.resWEdit);
    a.resHBox = CreateNumberBox(a.hwnd, IDC_RES_H, a.s.resH,
                                kResMin, kResMax, 10, &a.resHEdit);

    a.btnFontColor = CreateUIButton(a.hwnd, IDC_FONT_COLOR, L"", BTN_COLOR, a.s.fontColor);
    a.btnBgColor = CreateUIButton(a.hwnd, IDC_BG_COLOR, L"", BTN_COLOR, a.s.bgColor);
    UpdateColorButtons(a);

    a.presetBtn = CreateUIButton(a.hwnd, IDC_PRESET, L"", BTN_SELECT);
    SyncPresetFromSize(a);

    a.themeDarkBtn = CreateUIButton(a.hwnd, IDC_THEME_DARK, L"深色", BTN_TOGGLE);
    a.themeLightBtn = CreateUIButton(a.hwnd, IDC_THEME_LIGHT, L"暖白", BTN_TOGGLE);
    BtnSetChecked(a.themeDarkBtn, a.s.dark);
    BtnSetChecked(a.themeLightBtn, !a.s.dark);
    a.aboutBtn = CreateUIButton(a.hwnd, IDC_ABOUT, L"关于", BTN_GHOST);

    a.addBtn = CreateUIButton(a.hwnd, IDC_ADD_ROW, L"＋ 添加一行", BTN_NORMAL);
    a.browseBtn = CreateUIButton(a.hwnd, IDC_BROWSE, L"浏览…", BTN_NORMAL);
    a.importBtn = CreateUIButton(a.hwnd, IDC_IMPORT, L"导入图片…", BTN_NORMAL);
    a.openDirBtn = CreateUIButton(a.hwnd, IDC_OPEN_DIR, L"打开文件夹", BTN_GHOST);
    a.saveOnlyBtn = CreateUIButton(a.hwnd, IDC_SAVE_ONLY, L"仅保存图片", BTN_NORMAL);
    a.applyBtn = CreateUIButton(a.hwnd, IDC_APPLY, L"生成并应用壁纸", BTN_PRIMARY);

    InitTooltip(a);
    AddTooltips(a);

    ACCEL acc[3] = {
        { FVIRTKEY | FCONTROL, 'S', IDC_SAVE_ONLY },
        { FVIRTKEY | FCONTROL, 'O', IDC_IMPORT },
        { FVIRTKEY | FCONTROL, VK_RETURN, IDC_APPLY }
    };
    a.accel = CreateAcceleratorTableW(acc, 3);

    AddRow(a, false);
    a.activeRow = 0;
    RebuildTabStops(a);
}

// ---------------------------------------------------------------------------
//  Import a saved wallpaper (reads the project notes from the JPEG comment)
// ---------------------------------------------------------------------------
static void ApplyImportedProject(App& a, const ProjectData& p, const std::wstring& path) {
    a.s.fontSize = ClampInt(p.fontSize, kFontMin, kFontMax);
    a.s.posX = ClampInt(p.posX, kPosMin, kPosMax);
    a.s.posY = ClampInt(p.posY, kPosMin, kPosMax);
    a.s.resW = ClampInt(p.resW, kResMin, kResMax);
    a.s.resH = ClampInt(p.resH, kResMin, kResMax);
    a.s.fontColor = p.fontColor;
    a.s.bgColor = p.bgColor;

    if (a.fontBox) SendMessageW(a.fontBox, NB_SETVALUE, (WPARAM)a.s.fontSize, 0);
    if (a.posXBox) SendMessageW(a.posXBox, NB_SETVALUE, (WPARAM)a.s.posX, 0);
    if (a.posYBox) SendMessageW(a.posYBox, NB_SETVALUE, (WPARAM)a.s.posY, 0);
    if (a.resWBox) SendMessageW(a.resWBox, NB_SETVALUE, (WPARAM)a.s.resW, 0);
    if (a.resHBox) SendMessageW(a.resHBox, NB_SETVALUE, (WPARAM)a.s.resH, 0);
    UpdateColorButtons(a);
    SyncPresetFromSize(a);

    a.suppressAutoAdd = true;
    a.tabStops.clear();
    for (auto& r : a.rows) {
        if (r.hWord) {
            RemoveWindowSubclass(r.hWord, RowEditProc, 1);
            DestroyWindow(r.hWord);
        }
        if (r.hMeaning) {
            RemoveWindowSubclass(r.hMeaning, RowEditProc, 2);
            DestroyWindow(r.hMeaning);
        }
        if (r.hDelete) DestroyWindow(r.hDelete);
    }
    a.rows.clear();
    a.scrollY = 0;
    a.activeRow = 0;

    for (const auto& e : p.entries) {
        AddRow(a, false);
        Row& r = a.rows.back();
        r.word = e.word;
        r.meaning = e.meaning;
        if (r.hWord) {
            SendMessageW(r.hWord, WM_SETTEXT, 0, (LPARAM)r.word.c_str());
        }
        if (r.hMeaning) {
            SendMessageW(r.hMeaning, WM_SETTEXT, 0, (LPARAM)r.meaning.c_str());
        }
    }
    if (a.rows.empty() || !RowEmpty(a.rows.back())) AddRow(a, false);
    a.suppressAutoAdd = false;

    AssignRowIds(a);
    RebuildTabStops(a);
    a.scrollY = 0;
    PanelLayout(a);
    a.activeRow = 0;
    PanelEnsureVisible(a, 0);
    SchedulePreview(a, 10);
    RenderPreviewNow(a);
    if (!a.rows.empty() && a.rows[0].hWord) SetFocus(a.rows[0].hWord);

    SetStatus(a, Fmt(L"已导入工程：%s（%d 个词条）",
                     GetFileNamePart(path).c_str(), (int)p.entries.size()));
    ScheduleSave(a);
    RedrawWindow(a.hwnd, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

static bool ImportProjectFile(App& a, const std::wstring& path, bool showErrors) {
    if (!FileExists(path)) {
        if (showErrors) {
            MessageBoxW(a.hwnd, (L"找不到图片文件：\n" + path).c_str(),
                        kAppTitle, MB_OK | MB_ICONERROR);
        }
        SetStatus(a, L"找不到要导入的图片", true);
        return false;
    }
    std::string comment;
    if (!ExtractProjectComment(path, comment)) {
        if (showErrors) {
            MessageBoxW(a.hwnd,
                L"这张图片没有本程序写入的工程备注，无法恢复单词和样式。\n\n"
                L"只有用本程序“仅保存图片”或“生成并应用壁纸”生成的 JPEG 才带工程备注。",
                kAppTitle, MB_OK | MB_ICONINFORMATION);
        }
        SetStatus(a, L"该图片没有工程备注", true);
        return false;
    }
    ProjectData project;
    if (!ParseProjectComment(comment, project)) {
        if (showErrors) {
            MessageBoxW(a.hwnd,
                L"图片中的工程备注无法解析，可能已损坏或被其他工具修改。",
                kAppTitle, MB_OK | MB_ICONWARNING);
        }
        SetStatus(a, L"工程备注解析失败", true);
        return false;
    }
    ApplyImportedProject(a, project, path);
    return true;
}

static void ImportProjectDialog(App& a) {
    wchar_t file[MAX_PATH * 4] = {};
    std::wstring initial = DirExists(a.s.addr) ? a.s.addr : ExeDir();
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = a.hwnd;
    ofn.lpstrFilter =
        L"JPEG 图片 (*.jpg;*.jpeg)\0*.jpg;*.jpeg\0所有文件 (*.*)\0*.*\0\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = (DWORD)(sizeof(file) / sizeof(file[0]));
    ofn.lpstrInitialDir = initial.c_str();
    ofn.lpstrTitle = L"导入图片（读取图片中的工程备注）";
    ofn.lpstrDefExt = L"jpg";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;
    ImportProjectFile(a, file, true);
}

// ---------------------------------------------------------------------------
//  DPI helpers
// ---------------------------------------------------------------------------
static UINT SystemDpi() {
    HDC dc = GetDC(nullptr);
    UINT dpi = dc ? (UINT)GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(nullptr, dc);
    return dpi ? dpi : 96;
}

template <typename T>
static T LoadProcAddress(HMODULE module, const char* name) {
    FARPROC p = GetProcAddress(module, name);
    T fn = nullptr;
    if (p && sizeof(fn) == sizeof(p)) memcpy(&fn, &p, sizeof(fn));
    return fn;
}

static void ApplyImmersiveDarkMode(HWND hwnd, bool dark) {
    if (!hwnd) return;
    typedef HRESULT(WINAPI* FnDwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
    static HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    static FnDwmSetWindowAttribute fn = dwm
        ? LoadProcAddress<FnDwmSetWindowAttribute>(dwm, "DwmSetWindowAttribute")
        : nullptr;
    if (!fn) return;
    BOOL value = dark ? TRUE : FALSE;
    // 20 = DWMWA_USE_IMMERSIVE_DARK_MODE (Win10 1903+), 19 = older Win10.
    if (FAILED(fn(hwnd, 20, &value, sizeof(value)))) {
        fn(hwnd, 19, &value, sizeof(value));
    }
}

static void UpdateTitleBarTheme(App& a) {
    ApplyImmersiveDarkMode(a.hwnd, a.s.dark);
}

static UINT WindowDpi(HWND h) {
    typedef UINT(WINAPI* FnGetDpiForWindow)(HWND);
    static FnGetDpiForWindow fn = LoadProcAddress<FnGetDpiForWindow>(
        GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    if (fn && h) {
        UINT d = fn(h);
        if (d) return d;
    }
    return SystemDpi();
}

static void EnableDpiAwareness() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    typedef BOOL(WINAPI* FnSetCtx)(HANDLE);
    auto setCtx = LoadProcAddress<FnSetCtx>(user32, "SetProcessDpiAwarenessContext");
    if (setCtx && setCtx((HANDLE)-4)) return;
    typedef BOOL(WINAPI* FnSetDpiAware)();
    auto setAware = LoadProcAddress<FnSetDpiAware>(user32, "SetProcessDPIAware");
    if (setAware) setAware();
}

// ---------------------------------------------------------------------------
//  About window
// ---------------------------------------------------------------------------
static const wchar_t* kAboutGithubUrl = L"https://github.com/SlimeDolsHain";
static const wchar_t* kAboutBilibiliUrl = L"https://space.bilibili.com/349457353";
static const wchar_t* kAboutSponsorUrl = L"https://llmme.top/support.html";

struct AboutRects {
    RECT title{}, subtitle{}, version{}, author{}, license{};
    RECT githubButton{}, bilibiliButton{}, githubUrl{}, bilibiliUrl{};
    RECT sponsorButton{}, sponsorUrl{}, closeButton{};
};

static AboutRects GetAboutRects(int dpi) {
    auto px = [dpi](int v) { return MulDiv(v, dpi, 96); };
    AboutRects r;
    const int m = px(24);
    const int w = px(440);
    r.title = { m, px(14), w - m, px(44) };
    r.subtitle = { m, px(46), w - m, px(68) };
    r.version = { m, px(78), w - m, px(98) };
    r.author = { m, px(102), w - m, px(122) };
    r.license = { m, px(126), w - m, px(146) };
    const int half = (w - 2 * m - px(10)) / 2;
    r.githubButton = { m, px(162), m + half, px(196) };
    r.bilibiliButton = { m + half + px(10), px(162), w - m, px(196) };
    r.githubUrl = { m, px(200), w - m, px(218) };
    r.bilibiliUrl = { m, px(220), w - m, px(238) };
    r.sponsorButton = { m, px(252), m + px(230), px(290) };
    r.sponsorUrl = { m, px(294), w - m, px(312) };
    r.closeButton = { w - m - px(100), px(334), w - m, px(368) };
    return r;
}

static void LayoutAbout(App& a, HWND h) {
    AboutRects r = GetAboutRects(a.aboutDpi);
    auto move = [](HWND ctl, const RECT& rc) {
        if (!ctl) return;
        SetWindowPos(ctl, nullptr, rc.left, rc.top,
                     rc.right - rc.left, rc.bottom - rc.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    };
    move(a.aboutGithubBtn, r.githubButton);
    move(a.aboutBiliBtn, r.bilibiliButton);
    move(a.aboutSponsorBtn, r.sponsorButton);
    move(a.aboutCloseBtn, r.closeButton);
    (void)h;
}

static void PaintAbout(App& a, HDC dc) {
    HWND h = a.aboutWindow;
    if (!h) return;
    RECT rc;
    GetClientRect(h, &rc);
    DoubleBuffer db;
    if (!db.Begin(dc, rc.right, rc.bottom)) return;
    HDC mem = db.mem;
    const Palette& p = a.pal;
    FillSolid(mem, rc, p.bg);
    AboutRects r = GetAboutRects(a.aboutDpi);

    RECT accent{ r.title.left, r.title.top, r.title.left + MulDiv(56, a.aboutDpi, 96),
                 r.title.top + std::max(2, MulDiv(3, a.aboutDpi, 96)) };
    FillSolid(mem, accent, p.accent);

    HFONT titleFont = CreateUiFont(18, FW_BOLD, a.aboutDpi);
    RECT titleRect = r.title;
    titleRect.top += MulDiv(6, a.aboutDpi, 96);
    DrawTextRect(mem, L"WordsWallpaper", titleRect, p.text, titleFont,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (titleFont) DeleteObject(titleFont);

    DrawTextRect(mem, L"一款生成单词壁纸的辅助学习工具", r.subtitle, p.muted,
                 a.fontUi, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    DrawTextRect(mem, L"版本  V1.0", r.version, p.text, a.fontUi,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    DrawTextRect(mem, L"作者  @SlimeDolsHain", r.author, p.text, a.fontUi,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    DrawTextRect(mem, L"许可证  AGPL-3.0", r.license, p.text, a.fontUi,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    HPEN sep = CreatePen(PS_SOLID, 1, p.border);
    HGDIOBJ oldPen = SelectObject(mem, sep);
    int sy = r.license.bottom + MulDiv(6, a.aboutDpi, 96);
    MoveToEx(mem, r.title.left, sy, nullptr);
    LineTo(mem, rc.right - r.title.left, sy);
    SelectObject(mem, oldPen);
    DeleteObject(sep);

    DrawTextRect(mem, kAboutGithubUrl, r.githubUrl, p.muted, a.fontSmall,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    DrawTextRect(mem, kAboutBilibiliUrl, r.bilibiliUrl, p.muted, a.fontSmall,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    DrawTextRect(mem, kAboutSponsorUrl, r.sponsorUrl, p.muted, a.fontSmall,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    HBRUSH border = CreateSolidBrush(p.border);
    FrameRect(mem, &rc, border);
    DeleteObject(border);
    db.Blit(dc);
    db.End(dc);
}

static void OpenAboutUrl(const wchar_t* url) {
    HINSTANCE result = ShellExecuteW(nullptr, L"open", url, nullptr, nullptr,
                                     SW_SHOWNORMAL);
    if ((INT_PTR)result <= 32) {
        std::wstring msg = std::wstring(L"无法打开浏览器。\n请手动访问：\n") + url;
        MessageBoxW(g_app && g_app->aboutWindow ? g_app->aboutWindow : nullptr,
                    msg.c_str(), kAppTitle, MB_OK | MB_ICONWARNING);
    }
}

static LRESULT CALLBACK AboutWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    App* a = reinterpret_cast<App*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_NCCREATE) {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(l);
        a = reinterpret_cast<App*>(cs->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(a));
        return DefWindowProcW(h, m, w, l);
    }
    if (!a) return DefWindowProcW(h, m, w, l);

    switch (m) {
    case WM_CREATE: {
        a->aboutWindow = h;
        a->aboutDpi = a->dpi;
        ApplyImmersiveDarkMode(h, a->s.dark);
        HICON icon = LoadIconW(g_hInst, MAKEINTRESOURCEW(IDI_APP));
        if (icon) {
            SendMessageW(h, WM_SETICON, ICON_BIG, (LPARAM)icon);
            SendMessageW(h, WM_SETICON, ICON_SMALL, (LPARAM)icon);
        }
        a->aboutGithubBtn = CreateUIButton(h, IDC_ABOUT_GITHUB, L"GitHub 主页", BTN_NORMAL);
        a->aboutBiliBtn = CreateUIButton(h, IDC_ABOUT_BILIBILI, L"BiliBili 主页", BTN_NORMAL);
        a->aboutSponsorBtn = CreateUIButton(h, IDC_ABOUT_SPONSOR, L"赞助作者", BTN_PRIMARY);
        a->aboutCloseBtn = CreateUIButton(h, IDC_ABOUT_CLOSE, L"关闭", BTN_NORMAL);
        a->aboutTabStops.clear();
        for (HWND ctl : { a->aboutGithubBtn, a->aboutBiliBtn,
                          a->aboutSponsorBtn, a->aboutCloseBtn }) {
            if (ctl) a->aboutTabStops.push_back(ctl);
        }
        LayoutAbout(*a, h);
        return 0;
    }
    case WM_SIZE:
        LayoutAbout(*a, h);
        return 0;
    case WM_DPICHANGED: {
        UINT newDpi = LOWORD(w);
        if (newDpi) a->aboutDpi = newDpi;
        RECT* nr = reinterpret_cast<RECT*>(l);
        if (nr) {
            SetWindowPos(h, nullptr, nr->left, nr->top,
                         nr->right - nr->left, nr->bottom - nr->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        LayoutAbout(*a, h);
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_ERASEBKGND: {
        HDC dc = (HDC)w;
        RECT rc;
        GetClientRect(h, &rc);
        FillSolid(dc, rc, a->pal.bg);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        PaintAbout(*a, dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == IDC_ABOUT_GITHUB) OpenAboutUrl(kAboutGithubUrl);
        else if (id == IDC_ABOUT_BILIBILI) OpenAboutUrl(kAboutBilibiliUrl);
        else if (id == IDC_ABOUT_SPONSOR) OpenAboutUrl(kAboutSponsorUrl);
        else if (id == IDC_ABOUT_CLOSE) DestroyWindow(h);
        return 0;
    }
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) {
            DestroyWindow(h);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        a->aboutWindow = nullptr;
        a->aboutGithubBtn = nullptr;
        a->aboutBiliBtn = nullptr;
        a->aboutSponsorBtn = nullptr;
        a->aboutCloseBtn = nullptr;
        a->aboutTabStops.clear();
        if (IsWindow(a->hwnd)) {
            EnableWindow(a->hwnd, TRUE);
            SetForegroundWindow(a->hwnd);
            if (a->aboutBtn && IsWindow(a->aboutBtn)) SetFocus(a->aboutBtn);
            InvalidateRect(a->hwnd, nullptr, TRUE);
        }
        return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void ShowAboutDialog(App& a) {
    if (a.aboutWindow && IsWindow(a.aboutWindow)) {
        SetForegroundWindow(a.aboutWindow);
        if (a.aboutCloseBtn) SetFocus(a.aboutCloseBtn);
        return;
    }
    a.aboutDpi = a.dpi;
    int clientW = MulDiv(440, a.aboutDpi, 96);
    int clientH = MulDiv(400, a.aboutDpi, 96);
    RECT wr{ 0, 0, clientW, clientH };
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    DWORD exStyle = WS_EX_DLGMODALFRAME;
    AdjustWindowRectEx(&wr, style, FALSE, exStyle);
    int ww = wr.right - wr.left;
    int wh = wr.bottom - wr.top;
    RECT mr{};
    GetWindowRect(a.hwnd, &mr);
    int x = mr.left + ((mr.right - mr.left) - ww) / 2;
    int y = mr.top + ((mr.bottom - mr.top) - wh) / 2;
    HWND h = CreateWindowExW(exStyle, kAboutClass, L"关于 WordsWallpaper",
                             style, x, y, ww, wh, a.hwnd, nullptr, g_hInst, &a);
    if (!h) {
        MessageBoxW(a.hwnd, L"无法打开“关于”窗口。", kAppTitle,
                    MB_OK | MB_ICONERROR);
        return;
    }
    ShowWindow(h, SW_SHOW);
    UpdateWindow(h);
    EnableWindow(a.hwnd, FALSE);
    SetForegroundWindow(h);
    if (a.aboutCloseBtn) SetFocus(a.aboutCloseBtn);
}

// ---------------------------------------------------------------------------
//  Main window procedure
// ---------------------------------------------------------------------------
static LRESULT CALLBACK MainWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    App* a = reinterpret_cast<App*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_NCCREATE) {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(l);
        a = reinterpret_cast<App*>(cs->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(a));
        return DefWindowProcW(h, m, w, l);
    }
    if (!a) return DefWindowProcW(h, m, w, l);

    switch (m) {
    case WM_CREATE: {
        a->hwnd = h;
        a->inst = g_hInst;
        g_app = a;
        a->dpi = WindowDpi(h);
        a->pal = MakePalette(a->s.dark);
        CreateAppFonts(*a);
        RebuildThemeBrushes(*a);
        HICON icon = LoadIconW(g_hInst, MAKEINTRESOURCEW(IDI_APP));
        if (icon) {
            SendMessageW(h, WM_SETICON, ICON_BIG, (LPARAM)icon);
            SendMessageW(h, WM_SETICON, ICON_SMALL, (LPARAM)icon);
        }
        UpdateTitleBarTheme(*a);
        CreateControls(*a);
        a->creating = false;
        if (g_demoMode) FillDemoRows(*a);

        if (a->firstRun) {
            SetStatus(*a, L"首次使用：生成壁纸前会提示设置保存位置");
        } else if (!IsYaHeiName(g_imageFace)) {
            SetStatus(*a, L"未找到微软雅黑字体，已使用系统字体替代", true);
        } else {
            SetStatus(*a, L"就绪 · 微软雅黑粗体 · JPEG 50%");
        }
        PostMessageW(h, WM_APP_INITFOCUS, 0, 0);
        return 0;
    }

    case WM_ERASEBKGND: {
        HDC dc = (HDC)w;
        RECT rc;
        GetClientRect(h, &rc);
        FillSolid(dc, rc, a->pal.bg);
        return 1;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        MainPaint(*a, dc);
        EndPaint(h, &ps);
        return 0;
    }

    case WM_SIZE:
        if (w != SIZE_MINIMIZED) {
            Relayout(*a);
            if (!a->creating) SchedulePreview(*a, 60);
        }
        return 0;

    case WM_EXITSIZEMOVE:
        ScheduleSave(*a);
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(l);
        RECT r{ 0, 0, MulDiv(kMinW, a->dpi, 96), MulDiv(kMinH, a->dpi, 96) };
        AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                           FALSE, 0);
        mmi->ptMinTrackSize.x = r.right - r.left;
        mmi->ptMinTrackSize.y = r.bottom - r.top;
        return 0;
    }

    case WM_DPICHANGED: {
        UINT newDpi = LOWORD(w);
        if (newDpi) a->dpi = newDpi;
        CreateAppFonts(*a);
        RebuildThemeBrushes(*a);
        ApplyFontsToControls(*a);
        RECT* nr = reinterpret_cast<RECT*>(l);
        if (nr) {
            SetWindowPos(h, nullptr, nr->left, nr->top,
                         nr->right - nr->left, nr->bottom - nr->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        Relayout(*a);
        SchedulePreview(*a, 30);
        RedrawWindow(h, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
        return 0;
    }

    case WM_SETFOCUS:
        if (!a->tabStops.empty() && GetFocus() == h) SetFocus(a->tabStops[0]);
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(w);
        switch (id) {
        case IDC_ADD_ROW: {
            int insertAt = (int)a->rows.size();
            if (a->activeRow >= 0 && a->activeRow < (int)a->rows.size()) {
                insertAt = a->activeRow + 1;
            }
            AddRowAt(*a, insertAt, true);
            SetStatus(*a, Fmt(L"已在第 %d 行后添加一行", insertAt));
            return 0;
        }
        case IDC_BROWSE:
            BrowseForSaveDir(*a);
            return 0;
        case IDC_IMPORT:
            ImportProjectDialog(*a);
            return 0;
        case IDC_OPEN_DIR:
            ShowOpenFolder(*a);
            return 0;
        case IDC_SAVE_ONLY:
            DoSave(*a, false);
            return 0;
        case IDC_APPLY:
            DoSave(*a, true);
            return 0;
        case IDC_FONT_COLOR:
            SetColorAndPreview(*a, true);
            return 0;
        case IDC_BG_COLOR:
            SetColorAndPreview(*a, false);
            return 0;
        case IDC_THEME_DARK:
            SetTheme(*a, true);
            return 0;
        case IDC_THEME_LIGHT:
            SetTheme(*a, false);
            return 0;
        case IDC_ABOUT:
            ShowAboutDialog(*a);
            return 0;
        case IDC_PRESET:
            ShowPresetMenu(*a);
            return 0;
        default:
            break;
        }
        break;
    }

    case WM_APP_NUMCHANGE: {
        int id = (int)w;
        HWND box = (HWND)l;
        int value = (int)SendMessageW(box, NB_GETVALUE, 0, 0);
        OnNumberChange(*a, id, value);
        return 0;
    }

    case WM_APP_INITFOCUS:
        if (a->rows.empty()) AddRow(*a, false);
        if (!a->rows.empty() && a->rows[0].hWord) SetFocus(a->rows[0].hWord);
        RenderPreviewNow(*a);
        return 0;

    case WM_TIMER:
        if (w == TIMER_PREVIEW) {
            KillTimer(h, TIMER_PREVIEW);
            a->previewTimer = 0;
            RenderPreviewNow(*a);
            return 0;
        }
        if (w == TIMER_SAVE) {
            KillTimer(h, TIMER_SAVE);
            a->saveTimer = 0;
            SaveSettingsNow(*a);
            return 0;
        }
        break;

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = (HDC)w;
        SetTextColor(dc, a->pal.text);
        SetBkColor(dc, a->pal.input);
        return (LRESULT)a->brInput;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)w;
        SetTextColor(dc, a->pal.text);
        SetBkColor(dc, a->pal.bg);
        return (LRESULT)a->brBg;
    }

    case WM_CLOSE:
        SaveSettingsNow(*a);
        DestroyWindow(h);
        return 0;

    case WM_DESTROY:
        KillTimer(h, TIMER_PREVIEW);
        KillTimer(h, TIMER_SAVE);
        a->previewTimer = 0;
        a->saveTimer = 0;
        PostQuitMessage(0);
        return 0;

    case WM_NCDESTROY:
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// ---------------------------------------------------------------------------
//  Window classes + entry point
// ---------------------------------------------------------------------------
static void RegisterWindowClasses(HINSTANCE hInst, HBRUSH mainBackground) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = mainBackground;

    wc.lpfnWndProc = MainWndProc;
    wc.lpszClassName = kMainClass;
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);

    wc.hIcon = nullptr;
    wc.hIconSm = nullptr;
    wc.hbrBackground = nullptr;
    wc.lpfnWndProc = WordPanelProc;
    wc.lpszClassName = kPanelClass;
    RegisterClassExW(&wc);

    wc.lpfnWndProc = PreviewProc;
    wc.lpszClassName = kPreviewClass;
    RegisterClassExW(&wc);

    wc.lpfnWndProc = ButtonProc;
    wc.lpszClassName = kButtonClass;
    RegisterClassExW(&wc);

    wc.lpfnWndProc = NumberProc;
    wc.lpszClassName = kNumberClass;
    RegisterClassExW(&wc);

    wc.lpfnWndProc = AboutWndProc;
    wc.lpszClassName = kAboutClass;
    wc.hbrBackground = nullptr;
    RegisterClassExW(&wc);
}

static void DestroyAppResources(App& a) {
    if (a.previewBmp) {
        delete a.previewBmp;
        a.previewBmp = nullptr;
    }
    if (a.accel) {
        DestroyAcceleratorTable(a.accel);
        a.accel = nullptr;
    }
    HFONT fonts[] = { a.fontUi, a.fontBold, a.fontSmall, a.fontSection, a.fontTitle };
    for (HFONT f : fonts) if (f) DeleteObject(f);
    a.fontUi = a.fontBold = a.fontSmall = a.fontSection = a.fontTitle = nullptr;
    HBRUSH brushes[] = { a.brInput, a.brBg, a.brPanel, a.brStatus };
    for (HBRUSH b : brushes) if (b) DeleteObject(b);
    a.brInput = a.brBg = a.brPanel = a.brStatus = nullptr;
}

static int RunApp(HINSTANCE hInstance, int nCmdShow) {
    EnableDpiAwareness();

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icc);

    GdiplusStartupInput gsi;
    if (GdiplusStartup(&g_gdiplusToken, &gsi, nullptr) != Ok) {
        MessageBoxW(nullptr, L"无法初始化图形子系统（GDI+）。", kAppTitle,
                    MB_OK | MB_ICONERROR);
        return 1;
    }
    g_jpegClsidValid = GetEncoderClsid(L"image/jpeg", &g_jpegClsid);
    if (!g_jpegClsidValid) {
        MessageBoxW(nullptr, L"找不到 JPEG 编码器，无法生成压缩图片。", kAppTitle,
                    MB_OK | MB_ICONERROR);
        GdiplusShutdown(g_gdiplusToken);
        g_gdiplusToken = 0;
        return 1;
    }

    g_hInst = hInstance;
    InitImageFont();

    App app;
    app.inst = hInstance;
    app.dpi = SystemDpi();
    app.configPath = ConfigPathForApp();
    LoadSettings(app.s, app.configPath);
    app.firstRun = !app.s.hasConfig;
    app.pal = MakePalette(app.s.dark);
    g_app = &app;

    g_mainClassBrush = CreateSolidBrush(app.pal.bg);
    RegisterWindowClasses(hInstance, g_mainClassBrush);

    int logicalW = ClampInt(app.s.winW, kMinW, 10000);
    int logicalH = ClampInt(app.s.winH, kMinH, 10000);
    RECT wr{ 0, 0, MulDiv(logicalW, app.dpi, 96), MulDiv(logicalH, app.dpi, 96) };
    AdjustWindowRectEx(&wr, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                       FALSE, 0);
    int ww = wr.right - wr.left;
    int wh = wr.bottom - wr.top;
    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int x = wa.left + std::max(0, (int)(((wa.right - wa.left) - ww) / 2));
    int y = wa.top + std::max(0, (int)(((wa.bottom - wa.top) - wh) / 2));

    // Create and paint the window off-screen first.  This avoids the white
    // first frame that would otherwise appear before the dark theme paints.
    HWND hwnd = CreateWindowExW(0, kMainClass, kAppTitle,
                                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                -32000, -32000, ww, wh, nullptr, nullptr,
                                hInstance, &app);
    if (!hwnd) {
        wchar_t msg[128];
        swprintf(msg, 128, L"无法创建主窗口（错误代码 %lu）。", GetLastError());
        MessageBoxW(nullptr, msg, kAppTitle, MB_OK | MB_ICONERROR);
        DestroyAppResources(app);
        if (g_mainClassBrush) {
            DeleteObject(g_mainClassBrush);
            g_mainClassBrush = nullptr;
        }
        GdiplusShutdown(g_gdiplusToken);
        g_gdiplusToken = 0;
        ReleaseImageFont();
        g_app = nullptr;
        return 1;
    }

    bool startMinimized = (nCmdShow == SW_SHOWMINIMIZED || nCmdShow == SW_MINIMIZE);
    bool wantMaximized = app.s.maximized || nCmdShow == SW_SHOWMAXIMIZED;
    if (startMinimized) {
        ShowWindow(hwnd, SW_SHOWMINIMIZED);
        if (!g_startupImportPath.empty()) {
            ImportProjectFile(app, g_startupImportPath, true);
        }
    } else {
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        Relayout(app);
        if (!g_startupImportPath.empty()) {
            ImportProjectFile(app, g_startupImportPath, true);
        }
        RedrawWindow(hwnd, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        if (wantMaximized) {
            ShowWindow(hwnd, SW_SHOWMAXIMIZED);
        } else {
            SetWindowPos(hwnd, nullptr, x, y, ww, wh,
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
        UpdateWindow(hwnd);
        SetForegroundWindow(hwnd);
        SetFocus(hwnd);
    }

    MSG msg;
    BOOL got;
    while ((got = GetMessageW(&msg, nullptr, 0, 0)) > 0) {
        if (!app.aboutWindow && app.accel &&
            TranslateAcceleratorW(hwnd, app.accel, &msg)) {
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DestroyAppResources(app);
    if (g_mainClassBrush) {
        DeleteObject(g_mainClassBrush);
        g_mainClassBrush = nullptr;
    }
    GdiplusShutdown(g_gdiplusToken);
    g_gdiplusToken = 0;
    ReleaseImageFont();
    g_app = nullptr;
    return got == 0 ? (int)msg.wParam : 1;
}

#ifndef WW_TEST
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (_wcsicmp(argv[i], L"--demo") == 0) {
                g_demoMode = true;
            } else if (_wcsicmp(argv[i], L"--import") == 0 && i + 1 < argc) {
                g_startupImportPath = argv[++i];
            } else if (argv[i][0] != L'-' && g_startupImportPath.empty()) {
                // Drag an image onto the exe / "open with" support.
                g_startupImportPath = argv[i];
            }
        }
        LocalFree(argv);
    }
    return RunApp(hInstance, nCmdShow);
}
#endif

#ifdef WW_TEST
// Console test harness: renders a sample wallpaper with the same code path.
int wmain(int argc, wchar_t** argv) {
    GdiplusStartupInput gsi;
    if (GdiplusStartup(&g_gdiplusToken, &gsi, nullptr) != Ok) return 2;
    g_jpegClsidValid = GetEncoderClsid(L"image/jpeg", &g_jpegClsid);
    InitImageFont();

    Settings s;
    s.fontColor = RGB(255, 255, 255);
    s.bgColor = RGB(0, 0, 0);
    s.fontSize = 70;
    s.posX = 200;
    s.posY = 96;
    s.resW = 1920;
    s.resH = 1080;

    const wchar_t* data[][2] = {
        { L"personnel", L"员工" },
        { L"particular", L"特定的" },
        { L"put on", L"穿上" },
        { L"ordering", L"订购；排序" },
        { L"opportunity", L"机会" },
        { L"direction", L"方向" },
        { L"decoration", L"装饰" },
        { L"persuade", L"说服" },
        { L"moderate", L"中等的" },
        { L"essential", L"必不可少的" },
        { L"suspicion", L"怀疑" },
        { L"invention", L"发明" },
    };
    std::vector<Row> rows;
    for (auto& d : data) {
        Row r;
        r.word = d[0];
        r.meaning = d[1];
        rows.push_back(r);
    }

    std::wstring path = (argc > 1) ? argv[1] : L"test_output.jpg";
    Bitmap* bmp = RenderImage(s, rows, s.resW, s.resH, 1.0);
    if (!bmp) {
        GdiplusShutdown(g_gdiplusToken);
        return 3;
    }
    unsigned long long bytes = 0;
    bool ok = SaveJpeg(bmp, path, kJpegQuality, &bytes);
    delete bmp;

    std::wstring previewPath = path + L".preview.jpg";
    Bitmap* pb = RenderImage(s, rows, 640, 360, 640.0 / s.resW);
    if (pb) {
        SaveJpeg(pb, previewPath, kJpegQuality, nullptr);
        delete pb;
    }

    printf("font face: %ls\n", g_imageFace.c_str());
    printf("saved: %ls (%d x %d, %llu bytes) => %s\n",
           argv[0], s.resW, s.resH, bytes, ok ? "OK" : "FAILED");
    GdiplusShutdown(g_gdiplusToken);
    g_gdiplusToken = 0;
    ReleaseImageFont();
    return ok ? 0 : 4;
}
#endif
