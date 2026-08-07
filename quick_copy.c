#ifndef WINVER
#define WINVER 0x0600
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shellapi.h>
#include "json_helper.h"
#include <commctrl.h>

#define MAXL 256
#define BH 28
#define BM 2
#define PD 8
#define SH 24
#define W1 140
#define W2 460
#define START_X 80
#define START_Y 80

#define WM_TRAY_CALLBACK (WM_APP + 10)
#define WM_RECORDED_BINDING (WM_APP + 11)
#define TRAY_ICON_ID 1

#define MENU_TOGGLE_SERVICE 5001
#define MENU_OPEN_PANEL 5002
#define MENU_EDIT_HOTKEY 5003
#define MENU_EXIT_APP 5004

#define ID_LABEL_CURRENT 6001
#define ID_LABEL_HELP 6002
#define ID_RECORD 6003
#define ID_RESET 6004
#define ID_CLOSE_SETTINGS 6005

#define QC_INPUT_KEYBOARD 0
#define QC_INPUT_MOUSE 1

#define HK_CTRL  0x01
#define HK_ALT   0x02
#define HK_SHIFT 0x04
#define HK_WIN   0x08

typedef struct {
    DWORD type;
    DWORD code;
    DWORD modifiers;
} HotkeyBinding;

static LinkItem gl[MAXL];
static int gn = 0;
static HWND hw = NULL;
static HWND hs = NULL;
static HWND* hb = NULL;
static int bn = 0;
static int ld = 1;
static int hover_ix = -1;
static WCHAR ed[MAX_PATH];
static WCHAR hotkey_settings_path[MAX_PATH];
static HINSTANCE mi;

LRESULT CALLBACK WndP(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK BtnHoverProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
LRESULT CALLBACK SettingsProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK LowKeyboardProc(int, WPARAM, LPARAM);
LRESULT CALLBACK LowMouseProc(int, WPARAM, LPARAM);
DWORD WINAPI LdT(LPVOID);
void MkB(HWND);
void CpT(HWND, int);
void DrawBtn(const DRAWITEMSTRUCT* dis);
int BtnIndex(HWND b);
void MoveToStartPos(HWND h);
void ShowMainPanel(void);
void HideMainPanel(void);
void InitTray(void);
void RemoveTray(void);
void RefreshTray(void);
void ShowTrayMenu(void);
HICON CreateTrayIcon(void);
void ShowSettingsWindow(void);
void UpdateSettingsText(void);
void ApplyFontToChildren(HWND h);
void LoadHotkeyBinding(void);
void SaveHotkeyBinding(void);
void FormatBinding(const HotkeyBinding* hotkey, WCHAR* buffer, int buffer_count);
DWORD CurrentModifiers(void);
BOOL BindingMatches(DWORD type, DWORD code);
BOOL IsModifierKey(DWORD vk);
DWORD MouseCodeFromMessage(WPARAM message, LPARAM lparam, BOOL* is_down, BOOL* is_up);

static HFONT g_font = NULL;
static HANDLE g_mu = NULL;
static HHOOK g_keyboard_hook = NULL;
static HHOOK g_mouse_hook = NULL;
static HICON g_tray_icon = NULL;
static BOOL g_tray_icon_owned = FALSE;
static BOOL g_service_enabled = TRUE;
static BOOL g_recording_hotkey = FALSE;
static BOOL g_exiting = FALSE;
static LONG g_trigger_held = 0;
static UINT g_taskbar_created_message = 0;
static HWND g_settings_window = NULL;
static HotkeyBinding g_binding = { QC_INPUT_KEYBOARD, 0, 0 };
static HotkeyBinding g_recorded_binding = { QC_INPUT_KEYBOARD, 0, 0 };

int WINAPI WinMain(HINSTANCE h, HINSTANCE hp, LPSTR cmd, int ns) {
    (void)hp;
    (void)cmd;
    InitCommonControls();
    SetProcessDPIAware();

    /* single instance check */
    g_mu = CreateMutexW(NULL, FALSE, L"QuickCopy_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(g_mu);
        HWND ex = FindWindowW(L"QC", NULL);
        if (ex) {
            SetForegroundWindow(ex);
            ShowWindow(ex, SW_RESTORE);
        }
        return 0;
    }

    mi = h;
    GetModuleFileNameW(NULL, ed, MAX_PATH);
    wchar_t* p = wcsrchr(ed, 0x5C);
    if (p) *p = 0;
    wcscpy_s(hotkey_settings_path, MAX_PATH, ed);
    wcscat_s(hotkey_settings_path, MAX_PATH, L"\\quickcopy_hotkey.ini");
    LoadHotkeyBinding();

    /* Create DPI-scaled font */
    HDC sdc = GetDC(0);
    int dpi = GetDeviceCaps(sdc, LOGPIXELSY);
    ReleaseDC(0, sdc);
    int pt = 10;
    g_font = CreateFontW(-MulDiv(pt, dpi, 72), 0, 0, 0, FW_MEDIUM, 0,0,0,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = WndP;
    wc.hInstance = h;
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.lpszClassName = L"QC";
    if (!RegisterClassW(&wc)) return 1;

    hw = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        L"QC", L"quick copy",
        WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_POPUP,
        CW_USEDEFAULT, CW_USEDEFAULT, W1, SH+PD*2+30,
        0, 0, h, 0);
    if (!hw) return 1;
    MoveToStartPos(hw);

    hs = CreateWindowW(L"STATIC", L"loading...",
        WS_CHILD|WS_VISIBLE|SS_CENTER,
        PD, PD, W1-PD*2, SH, hw, 0, h, 0);

    ShowWindow(hw, ns);
    UpdateWindow(hw);

    RegisterHotKey(hw, 1, MOD_CONTROL, 'W');
    InitTray();
    g_keyboard_hook = SetWindowsHookExW(WH_KEYBOARD_LL, LowKeyboardProc, h, 0);
    g_mouse_hook = SetWindowsHookExW(WH_MOUSE_LL, LowMouseProc, h, 0);

    HANDLE th = CreateThread(0,0,LdT,0,0,0);
    if (th) CloseHandle(th);

    MSG msg;
    while (GetMessage(&msg,0,0,0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    if (hb) free(hb);
    return 0;
}

DWORD WINAPI LdT(LPVOID param) {
    (void)param;
    wchar_t pth[MAX_PATH];
    wcscpy_s(pth, MAX_PATH, ed);
    wcscat_s(pth, MAX_PATH, L"\\links.json");

    FILE* f = _wfopen(pth, L"rb");
    if (!f) { ld=0; PostMessage(hw, WM_USER+1,0,0); return 0; }

    fseek(f,0,SEEK_END); long sz = ftell(f); fseek(f,0,SEEK_SET);
    if (sz <= 2) { fclose(f); ld=0; PostMessage(hw, WM_USER+3,0,0); return 0; }

    char* buf = (char*)malloc(sz+1);
    if (!buf) { fclose(f); ld=0; PostMessage(hw, WM_USER+3,0,0); return 0; }
    size_t nr = fread(buf,1,sz,f); buf[nr]=0; fclose(f);
    if (nr != (size_t)sz) { free(buf); ld=0; PostMessage(hw, WM_USER+3,0,0); return 0; }

    char* sp = buf;
    if (sz>=3 && (unsigned char)buf[0]==0xEF && (unsigned char)buf[1]==0xBB && (unsigned char)buf[2]==0xBF) sp=buf+3;

    int cn=0;
    if (parse_json_array(sp, gl, MAXL, &cn)) {
        gn=cn; ld=0; PostMessage(hw, WM_USER+0,0,0);
    } else {
        ld=0; PostMessage(hw, WM_USER+2,0,0);
    }
    free(buf);
    return 0;
}

void MoveToStartPos(HWND h) {
    RECT r;
    RECT wa;
    GetWindowRect(h, &r);
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);

    int win_w = r.right - r.left;
    int win_h = r.bottom - r.top;
    int x = wa.left + START_X;
    int y = wa.top + START_Y;

    if (x + win_w > wa.right) x = wa.right - win_w;
    if (y + win_h > wa.bottom) y = wa.bottom - win_h;
    if (x < wa.left) x = wa.left;
    if (y < wa.top) y = wa.top;

    SetWindowPos(h, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void ShowMainPanel(void) {
    if (!hw) return;
    PostMessageW(hw, WM_USER+4, 0, 0);
}

void HideMainPanel(void) {
    if (!hw) return;
    ShowWindow(hw, SW_HIDE);
}

void AppendPart(WCHAR* buffer, int buffer_count, const WCHAR* part) {
    (void)buffer_count;
    if (buffer[0]) wcscat_s(buffer, 128, L" + ");
    wcscat_s(buffer, 128, part);
}

void KeyName(DWORD vk, WCHAR* buffer, int buffer_count) {
    switch (vk) {
    case VK_SCROLL: wcscpy_s(buffer, buffer_count, L"ScrLk"); return;
    case VK_ESCAPE: wcscpy_s(buffer, buffer_count, L"Esc"); return;
    case VK_SPACE: wcscpy_s(buffer, buffer_count, L"Space"); return;
    case VK_RETURN: wcscpy_s(buffer, buffer_count, L"Enter"); return;
    case VK_TAB: wcscpy_s(buffer, buffer_count, L"Tab"); return;
    case VK_BACK: wcscpy_s(buffer, buffer_count, L"Backspace"); return;
    case VK_DELETE: wcscpy_s(buffer, buffer_count, L"Delete"); return;
    case VK_INSERT: wcscpy_s(buffer, buffer_count, L"Insert"); return;
    case VK_HOME: wcscpy_s(buffer, buffer_count, L"Home"); return;
    case VK_END: wcscpy_s(buffer, buffer_count, L"End"); return;
    case VK_PRIOR: wcscpy_s(buffer, buffer_count, L"PageUp"); return;
    case VK_NEXT: wcscpy_s(buffer, buffer_count, L"PageDown"); return;
    case VK_LEFT: wcscpy_s(buffer, buffer_count, L"Left"); return;
    case VK_RIGHT: wcscpy_s(buffer, buffer_count, L"Right"); return;
    case VK_UP: wcscpy_s(buffer, buffer_count, L"Up"); return;
    case VK_DOWN: wcscpy_s(buffer, buffer_count, L"Down"); return;
    default:
        if ((vk >= L'A' && vk <= L'Z') || (vk >= L'0' && vk <= L'9')) {
            buffer[0] = (WCHAR)vk;
            buffer[1] = 0;
            return;
        }
        if (vk >= VK_F1 && vk <= VK_F24) {
            swprintf(buffer, buffer_count, L"F%lu", vk - VK_F1 + 1);
            return;
        }
        swprintf(buffer, buffer_count, L"VK_%lu", vk);
        return;
    }
}

void FormatBinding(const HotkeyBinding* hotkey, WCHAR* buffer, int buffer_count) {
    WCHAR input[64];
    buffer[0] = 0;
    if (hotkey->code == 0) {
        wcscpy_s(buffer, buffer_count, L"未设置触发键");
        return;
    }

    if (hotkey->modifiers & HK_CTRL) AppendPart(buffer, buffer_count, L"Ctrl");
    if (hotkey->modifiers & HK_ALT) AppendPart(buffer, buffer_count, L"Alt");
    if (hotkey->modifiers & HK_SHIFT) AppendPart(buffer, buffer_count, L"Shift");
    if (hotkey->modifiers & HK_WIN) AppendPart(buffer, buffer_count, L"Win");

    if (hotkey->type == QC_INPUT_MOUSE) {
        switch (hotkey->code) {
        case VK_LBUTTON: wcscpy_s(input, 64, L"Mouse Left"); break;
        case VK_RBUTTON: wcscpy_s(input, 64, L"Mouse Right"); break;
        case VK_MBUTTON: wcscpy_s(input, 64, L"Mouse Middle"); break;
        case VK_XBUTTON1: wcscpy_s(input, 64, L"XButton1"); break;
        case VK_XBUTTON2: wcscpy_s(input, 64, L"XButton2"); break;
        default: swprintf(input, 64, L"Mouse_%lu", hotkey->code); break;
        }
    } else {
        KeyName(hotkey->code, input, 64);
    }
    AppendPart(buffer, buffer_count, input);
}

void LoadHotkeyBinding(void) {
    g_binding.type = (DWORD)GetPrivateProfileIntW(L"hotkey", L"type", QC_INPUT_KEYBOARD, hotkey_settings_path);
    g_binding.code = (DWORD)GetPrivateProfileIntW(L"hotkey", L"code", 0, hotkey_settings_path);
    g_binding.modifiers = (DWORD)GetPrivateProfileIntW(L"hotkey", L"modifiers", 0, hotkey_settings_path);
    if (g_binding.type > QC_INPUT_MOUSE) {
        g_binding.type = QC_INPUT_KEYBOARD;
        g_binding.code = 0;
        g_binding.modifiers = 0;
    }
}

void SaveHotkeyBinding(void) {
    WCHAR value[32];
    swprintf(value, 32, L"%lu", g_binding.type);
    WritePrivateProfileStringW(L"hotkey", L"type", value, hotkey_settings_path);
    swprintf(value, 32, L"%lu", g_binding.code);
    WritePrivateProfileStringW(L"hotkey", L"code", value, hotkey_settings_path);
    swprintf(value, 32, L"%lu", g_binding.modifiers);
    WritePrivateProfileStringW(L"hotkey", L"modifiers", value, hotkey_settings_path);
}

DWORD CurrentModifiers(void) {
    DWORD mods = 0;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) mods |= HK_CTRL;
    if (GetAsyncKeyState(VK_MENU) & 0x8000) mods |= HK_ALT;
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) mods |= HK_SHIFT;
    if ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) mods |= HK_WIN;
    return mods;
}

BOOL IsModifierKey(DWORD vk) {
    return vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL
        || vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU
        || vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT
        || vk == VK_LWIN || vk == VK_RWIN;
}

BOOL BindingMatches(DWORD type, DWORD code) {
    return g_service_enabled
        && !g_recording_hotkey
        && g_binding.code != 0
        && g_binding.type == type
        && g_binding.code == code
        && CurrentModifiers() == g_binding.modifiers;
}

void InitTray(void) {
    g_taskbar_created_message = RegisterWindowMessageW(L"TaskbarCreated");
    g_tray_icon = CreateTrayIcon();
    if (g_tray_icon) {
        g_tray_icon_owned = TRUE;
    } else {
        g_tray_icon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
        g_tray_icon_owned = FALSE;
    }
    RefreshTray();
}

HICON CreateTrayIcon(void) {
    HDC screen = GetDC(NULL);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP color = CreateCompatibleBitmap(screen, 16, 16);
    HBITMAP mask = CreateBitmap(16, 16, 1, 1, NULL);
    if (!dc || !color || !mask) {
        if (mask) DeleteObject(mask);
        if (color) DeleteObject(color);
        if (dc) DeleteDC(dc);
        ReleaseDC(NULL, screen);
        return NULL;
    }

    HGDIOBJ old = SelectObject(dc, color);
    RECT r = {0, 0, 16, 16};
    HBRUSH bg = CreateSolidBrush(RGB(32, 112, 210));
    FillRect(dc, &r, bg);
    DeleteObject(bg);

    HPEN border = CreatePen(PS_SOLID, 1, RGB(12, 70, 150));
    HGDIOBJ old_pen = SelectObject(dc, border);
    HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, 0, 0, 16, 16);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(border);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    HFONT font = CreateFontW(-12, 0, 0, 0, FW_BOLD, 0, 0, 0,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HGDIOBJ old_font = font ? SelectObject(dc, font) : NULL;
    DrawTextW(dc, L"Q", -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (old_font) SelectObject(dc, old_font);
    if (font) DeleteObject(font);
    SelectObject(dc, old);

    HDC mask_dc = CreateCompatibleDC(screen);
    HGDIOBJ old_mask = SelectObject(mask_dc, mask);
    RECT mr = {0, 0, 16, 16};
    FillRect(mask_dc, &mr, GetStockObject(BLACK_BRUSH));
    SelectObject(mask_dc, old_mask);
    DeleteDC(mask_dc);

    ICONINFO info;
    ZeroMemory(&info, sizeof(info));
    info.fIcon = TRUE;
    info.hbmColor = color;
    info.hbmMask = mask;
    HICON icon = CreateIconIndirect(&info);

    DeleteObject(mask);
    DeleteObject(color);
    DeleteDC(dc);
    ReleaseDC(NULL, screen);
    return icon;
}

void RemoveTray(void) {
    NOTIFYICONDATAW data;
    ZeroMemory(&data, sizeof(data));
    data.cbSize = sizeof(data);
    data.hWnd = hw;
    data.uID = TRAY_ICON_ID;
    Shell_NotifyIconW(NIM_DELETE, &data);
}

void RefreshTray(void) {
    NOTIFYICONDATAW data;
    WCHAR hotkey_text[128];
    WCHAR tip[128];
    ZeroMemory(&data, sizeof(data));
    data.cbSize = sizeof(data);
    data.hWnd = hw;
    data.uID = TRAY_ICON_ID;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = WM_TRAY_CALLBACK;
    data.hIcon = g_tray_icon;
    FormatBinding(&g_binding, hotkey_text, 128);
    swprintf(tip, 128, L"QuickCopy 快捷复制 | %s | %s",
             g_service_enabled ? L"快捷键监听已开启" : L"快捷键监听已暂停",
             hotkey_text);
    wcscpy_s(data.szTip, ARRAYSIZE(data.szTip), tip);
    Shell_NotifyIconW(NIM_ADD, &data);
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

void ShowTrayMenu(void) {
    POINT cursor;
    WCHAR hotkey_text[128];
    GetCursorPos(&cursor);
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    FormatBinding(&g_binding, hotkey_text, 128);
    AppendMenuW(menu, MF_STRING | (g_service_enabled ? MF_CHECKED : 0),
                MENU_TOGGLE_SERVICE, g_service_enabled ? L"暂停快捷键监听" : L"开启快捷键监听");
    AppendMenuW(menu, MF_STRING, MENU_OPEN_PANEL, L"显示复制面板");
    AppendMenuW(menu, MF_STRING, MENU_EDIT_HOTKEY, L"设置触发键...");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, hotkey_text);
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, MENU_EXIT_APP, L"退出 QuickCopy");

    SetForegroundWindow(hw);
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                              cursor.x, cursor.y, 0, hw, NULL);
    DestroyMenu(menu);

    if (cmd == MENU_TOGGLE_SERVICE) {
        g_service_enabled = !g_service_enabled;
        InterlockedExchange(&g_trigger_held, 0);
        RefreshTray();
    } else if (cmd == MENU_OPEN_PANEL) {
        ShowMainPanel();
    } else if (cmd == MENU_EDIT_HOTKEY) {
        ShowSettingsWindow();
    } else if (cmd == MENU_EXIT_APP) {
        g_exiting = TRUE;
        DestroyWindow(hw);
    }
}

void UpdateSettingsText(void) {
    if (!g_settings_window) return;
    WCHAR hotkey_text[128];
    WCHAR current_text[192];
    FormatBinding(&g_binding, hotkey_text, 128);
    swprintf(current_text, 192, L"当前快捷键：%s", hotkey_text);
    SetDlgItemTextW(g_settings_window, ID_LABEL_CURRENT, current_text);
    SetDlgItemTextW(g_settings_window, ID_LABEL_HELP,
        g_recording_hotkey
            ? L"请按下新的键盘键或鼠标键，可同时按住 Ctrl / Alt / Shift / Win。"
            : L"点击“录制触发键”，再按下键盘键或鼠标键。");
}

void ApplyFontToChildren(HWND h) {
    if (!g_font) return;
    HWND child = GetWindow(h, GW_CHILD);
    while (child) {
        SendMessageW(child, WM_SETFONT, (WPARAM)g_font, TRUE);
        child = GetWindow(child, GW_HWNDNEXT);
    }
}

LRESULT CALLBACK SettingsProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    (void)l;
    switch (m) {
    case WM_CREATE:
        CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 16, 16, 360, 24, h, (HMENU)ID_LABEL_CURRENT, mi, NULL);
        CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 16, 46, 390, 42, h, (HMENU)ID_LABEL_HELP, mi, NULL);
        CreateWindowW(L"BUTTON", L"录制触发键", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 16, 96, 128, 30, h, (HMENU)ID_RECORD, mi, NULL);
        CreateWindowW(L"BUTTON", L"清除触发键", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 154, 96, 108, 30, h, (HMENU)ID_RESET, mi, NULL);
        CreateWindowW(L"BUTTON", L"关闭", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 272, 96, 80, 30, h, (HMENU)ID_CLOSE_SETTINGS, mi, NULL);
        ApplyFontToChildren(h);
        UpdateSettingsText();
        return 0;
    case WM_COMMAND:
        if (LOWORD(w) == ID_RECORD) {
            g_recording_hotkey = TRUE;
            UpdateSettingsText();
            return 0;
        }
        if (LOWORD(w) == ID_RESET) {
            g_binding.type = QC_INPUT_KEYBOARD;
            g_binding.code = 0;
            g_binding.modifiers = 0;
            g_recording_hotkey = FALSE;
            SaveHotkeyBinding();
            UpdateSettingsText();
            RefreshTray();
            return 0;
        }
        if (LOWORD(w) == ID_CLOSE_SETTINGS) {
            DestroyWindow(h);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        g_recording_hotkey = FALSE;
        g_settings_window = NULL;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

void ShowSettingsWindow(void) {
    if (g_settings_window) {
        ShowWindow(g_settings_window, SW_SHOWNORMAL);
        SetForegroundWindow(g_settings_window);
        return;
    }

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = SettingsProc;
    wc.hInstance = mi;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"QuickCopyHotkeySettings";
    RegisterClassW(&wc);

    g_settings_window = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"QuickCopyHotkeySettings",
        L"QuickCopy 触发键设置",
        WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 430, 180,
        hw, NULL, mi, NULL);
    if (g_settings_window) {
        ShowWindow(g_settings_window, SW_SHOWNORMAL);
        UpdateWindow(g_settings_window);
    }
}

DWORD MouseCodeFromMessage(WPARAM message, LPARAM lparam, BOOL* is_down, BOOL* is_up) {
    const MSLLHOOKSTRUCT* mouse = (const MSLLHOOKSTRUCT*)lparam;
    *is_down = FALSE;
    *is_up = FALSE;
    switch (message) {
    case WM_LBUTTONDOWN: *is_down = TRUE; return VK_LBUTTON;
    case WM_LBUTTONUP: *is_up = TRUE; return VK_LBUTTON;
    case WM_RBUTTONDOWN: *is_down = TRUE; return VK_RBUTTON;
    case WM_RBUTTONUP: *is_up = TRUE; return VK_RBUTTON;
    case WM_MBUTTONDOWN: *is_down = TRUE; return VK_MBUTTON;
    case WM_MBUTTONUP: *is_up = TRUE; return VK_MBUTTON;
    case WM_XBUTTONDOWN: *is_down = TRUE; return HIWORD(mouse->mouseData) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2;
    case WM_XBUTTONUP: *is_up = TRUE; return HIWORD(mouse->mouseData) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2;
    default: return 0;
    }
}

LRESULT CALLBACK LowKeyboardProc(int code, WPARAM w, LPARAM l) {
    if (code == HC_ACTION) {
        const KBDLLHOOKSTRUCT* key = (const KBDLLHOOKSTRUCT*)l;
        BOOL down = w == WM_KEYDOWN || w == WM_SYSKEYDOWN;
        BOOL up = w == WM_KEYUP || w == WM_SYSKEYUP;

        if (g_recording_hotkey && down && !IsModifierKey(key->vkCode)) {
            g_recorded_binding.type = QC_INPUT_KEYBOARD;
            g_recorded_binding.code = key->vkCode;
            g_recorded_binding.modifiers = CurrentModifiers();
            PostMessageW(hw, WM_RECORDED_BINDING, 0, 0);
            return 1;
        }
        if (down && BindingMatches(QC_INPUT_KEYBOARD, key->vkCode)) {
            if (InterlockedExchange(&g_trigger_held, 1) == 0) ShowMainPanel();
            return 1;
        }
        if (up && g_binding.type == QC_INPUT_KEYBOARD && g_binding.code == key->vkCode) {
            InterlockedExchange(&g_trigger_held, 0);
        }
    }
    return CallNextHookEx(g_keyboard_hook, code, w, l);
}

LRESULT CALLBACK LowMouseProc(int code, WPARAM w, LPARAM l) {
    if (code == HC_ACTION) {
        BOOL down = FALSE, up = FALSE;
        DWORD mouse_code = MouseCodeFromMessage(w, l, &down, &up);
        if (mouse_code) {
            if (g_recording_hotkey && down) {
                g_recorded_binding.type = QC_INPUT_MOUSE;
                g_recorded_binding.code = mouse_code;
                g_recorded_binding.modifiers = CurrentModifiers();
                PostMessageW(hw, WM_RECORDED_BINDING, 0, 0);
                return 1;
            }
            if (down && BindingMatches(QC_INPUT_MOUSE, mouse_code)) {
                if (InterlockedExchange(&g_trigger_held, 1) == 0) ShowMainPanel();
                return 1;
            }
            if (up && g_binding.type == QC_INPUT_MOUSE && g_binding.code == mouse_code) {
                InterlockedExchange(&g_trigger_held, 0);
            }
        }
    }
    return CallNextHookEx(g_mouse_hook, code, w, l);
}

void MkB(HWND h) {
    if (hs) { DestroyWindow(hs); hs=0; }
    if (hb) {
        for (int i=0; i<bn; i++) if (hb[i]) DestroyWindow(hb[i]);
        free(hb); hb=0; bn=0;
    }
    if (gn==0) {
        hs = CreateWindowW(L"STATIC", L"empty links.json",
            WS_CHILD|WS_VISIBLE|SS_CENTER, PD,PD,200,SH, h,0,mi,0);
        return;
    }

    /* measure text widths using DPI font */
    HDC dc = GetDC(h);
    HFONT old = (HFONT)SelectObject(dc, g_font);
    int max_w = W1 - PD*2;
    int max_h = 0;
    for (int i=0; i<gn; i++) {
        SIZE sz;
        GetTextExtentPoint32W(dc, gl[i].name, lstrlenW(gl[i].name), &sz);
        int tw = sz.cx + 24;
        if (tw > max_w) max_w = tw;
        if (sz.cy > max_h) max_h = sz.cy;
    }
    SelectObject(dc, old);
    ReleaseDC(h, dc);
    if (max_w > W2 - PD*2) max_w = W2 - PD*2;
    if (max_w < 80) max_w = 80;
    int btn_h = max_h + 12;
    if (btn_h < BH) btn_h = BH;

    /* calculate window size from desired client area */
    int client_w = max_w + PD*2;
    int client_h = SH + PD*2 + gn*(btn_h+BM);
    RECT wr = {0, 0, client_w, client_h};
    DWORD style = GetWindowLongW(h, GWL_STYLE);
    DWORD exstyle = GetWindowLongW(h, GWL_EXSTYLE);
    AdjustWindowRectEx(&wr, style, FALSE, exstyle);
    int win_w = wr.right - wr.left;
    int win_h = wr.bottom - wr.top;

    /* clamp to work area */
    RECT cur; GetWindowRect(h, &cur);
    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int x = cur.left;
    int y = cur.top;
    if (y + win_h > wa.bottom) y = wa.bottom - win_h;
    if (y < wa.top) y = wa.top;
    if (x + win_w > wa.right) x = wa.right - win_w;
    if (x < wa.left) x = wa.left;
    SetWindowPos(h, 0, x, y, win_w, win_h, SWP_NOZORDER);

    hb = (HWND*)malloc(gn * sizeof(HWND));
    if (!hb) return;
    memset(hb, 0, gn * sizeof(HWND));
    bn = gn;

    for (int i=0; i<gn; i++) {
        int y = PD + SH + i*(btn_h+BM);
        hb[i] = CreateWindowW(L"BUTTON", gl[i].name,
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
            PD, y, max_w, btn_h, h, (HMENU)(INT_PTR)(1000+i), mi, 0);
        if (hb[i]) SetWindowSubclass(hb[i], BtnHoverProc, (UINT_PTR)(1000+i), 0);
        if (g_font) SendMessageW(hb[i], WM_SETFONT, (WPARAM)g_font, 1);
    }
}

static void SetBtnTextNow(HWND b, const wchar_t* text) {
    if (!b) return;
    SetWindowTextW(b, text);
    RedrawWindow(b, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
}

int BtnIndex(HWND b) {
    if (!hb) return -1;
    for (int i=0; i<bn; i++) {
        if (hb[i] == b) return i;
    }
    return -1;
}

void DrawBtn(const DRAWITEMSTRUCT* dis) {
    int ix = BtnIndex(dis->hwndItem);
    RECT r = dis->rcItem;
    HDC dc = dis->hDC;
    int hot = ix == hover_ix;
    int down = (dis->itemState & ODS_SELECTED) != 0;
    int focused = (dis->itemState & ODS_FOCUS) != 0;

    COLORREF bg = hot ? RGB(198, 222, 252) : GetSysColor(COLOR_BTNFACE);
    if (down) bg = RGB(172, 203, 241);

    HBRUSH brush = CreateSolidBrush(bg);
    FillRect(dc, &r, brush);
    DeleteObject(brush);

    DrawEdge(dc, &r, down ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT);
    if (hot) {
        RECT hr = dis->rcItem;
        InflateRect(&hr, -2, -2);
        HPEN pen = CreatePen(PS_SOLID, 2, RGB(45, 115, 205));
        HGDIOBJ old_pen = SelectObject(dc, pen);
        HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, hr.left, hr.top, hr.right, hr.bottom);
        SelectObject(dc, old_brush);
        SelectObject(dc, old_pen);
        DeleteObject(pen);
    }
    InflateRect(&r, -6, -2);

    wchar_t txt[256];
    GetWindowTextW(dis->hwndItem, txt, 256);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
    HFONT oldf = g_font ? (HFONT)SelectObject(dc, g_font) : NULL;
    DrawTextW(dc, txt, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (oldf) SelectObject(dc, oldf);

    if (focused) {
        RECT fr = dis->rcItem;
        InflateRect(&fr, -4, -4);
        DrawFocusRect(dc, &fr);
    }
}

LRESULT CALLBACK BtnHoverProc(HWND b, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref) {
    (void)w;
    (void)l;
    (void)id;
    (void)ref;

    switch (m) {
    case WM_MOUSEMOVE: {
        int ix = BtnIndex(b);
        if (ix != hover_ix) {
            int old_hover = hover_ix;
            hover_ix = ix;
            if (old_hover >= 0 && old_hover < bn && hb && hb[old_hover]) InvalidateRect(hb[old_hover], NULL, TRUE);
            if (hover_ix >= 0 && hover_ix < bn && hb && hb[hover_ix]) InvalidateRect(hb[hover_ix], NULL, TRUE);
        }
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, b, 0 };
        TrackMouseEvent(&tme);
        break;
    }
    case WM_MOUSELEAVE:
        if (BtnIndex(b) == hover_ix) {
            int old_hover = hover_ix;
            hover_ix = -1;
            if (old_hover >= 0 && old_hover < bn && hb && hb[old_hover]) InvalidateRect(hb[old_hover], NULL, TRUE);
        }
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(b, BtnHoverProc, id);
        break;
    }

    return DefSubclassProc(b, m, w, l);
}

void CpT(HWND h, int ix) {
    if (ix<0 || ix>=gn) return;
    const wchar_t* t = gl[ix].content;
    if (!t || !t[0]) return;

    if (!OpenClipboard(h)) return;
    EmptyClipboard();
    int len = lstrlenW(t);
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, (len+1)*sizeof(wchar_t));
    if (hg) {
        wchar_t* d = (wchar_t*)GlobalLock(hg);
        if (d) { wcscpy_s(d, len+1, t); GlobalUnlock(hg); SetClipboardData(CF_UNICODETEXT, hg); }
    }
    CloseClipboard();

    if (hb && ix<bn && hb[ix]) {
        SetBtnTextNow(hb[ix], L"copied!");
        KillTimer(h, 2000+ix);
        SetTimer(h, 2000+ix, 800, 0);
    }
}

LRESULT CALLBACK WndP(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == g_taskbar_created_message) {
        RefreshTray();
        return 0;
    }

    switch (m) {
    case WM_USER+0: MkB(h); InvalidateRect(h,0,1); break;
    case WM_USER+1: if (hs) SetWindowTextW(hs, L"links.json not found"); break;
    case WM_USER+2: if (hs) SetWindowTextW(hs, L"parse error"); break;
    case WM_USER+3: if (hs) SetWindowTextW(hs, L"load error"); break;
    case WM_USER+4:
        ShowWindow(h, SW_SHOWNORMAL);
        SetForegroundWindow(h);
        break;
    case WM_TRAY_CALLBACK:
        if (l == WM_RBUTTONUP || l == WM_CONTEXTMENU) {
            ShowTrayMenu();
        } else if (l == WM_LBUTTONDBLCLK) {
            ShowMainPanel();
        }
        break;
    case WM_RECORDED_BINDING:
        g_binding = g_recorded_binding;
        g_recording_hotkey = FALSE;
        InterlockedExchange(&g_trigger_held, 0);
        SaveHotkeyBinding();
        UpdateSettingsText();
        RefreshTray();
        break;
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id>=1000 && id<1000+gn) CpT(h, id-1000);
        break;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)l;
        if (dis && dis->CtlID >= 1000 && (int)dis->CtlID < 1000+gn) {
            DrawBtn(dis);
            return TRUE;
        }
        break;
    }
    case WM_TIMER: {
        int id = (int)w;
        if (id>=2000 && id<2000+gn) {
            int i = id-2000;
            KillTimer(h, id);
            if (hb && i<bn && hb[i]) SetBtnTextNow(hb[i], gl[i].name);
        }
        break;
    }
    case WM_HOTKEY:
        if (w == 1) HideMainPanel();
        break;
    case WM_CLOSE:
        if (g_exiting) {
            DestroyWindow(h);
        } else {
            HideMainPanel();
        }
        break;
    case WM_DESTROY:
        RemoveTray();
        UnregisterHotKey(h, 1);
        if (g_keyboard_hook) UnhookWindowsHookEx(g_keyboard_hook);
        if (g_mouse_hook) UnhookWindowsHookEx(g_mouse_hook);
        if (g_settings_window) DestroyWindow(g_settings_window);
        if (g_tray_icon_owned && g_tray_icon) DestroyIcon(g_tray_icon);
        if (g_font) DeleteObject(g_font);
        CloseHandle(g_mu);
        PostQuitMessage(0);
        break;
    default: return DefWindowProcW(h,m,w,l);
    }
    return 0;
}
