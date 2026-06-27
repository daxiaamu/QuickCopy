#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "json_helper.h"
#include <commctrl.h>

#define MAXL 256
#define BH 28
#define BM 2
#define PD 8
#define SH 24
#define W1 140
#define W2 460

static LinkItem gl[MAXL];
static int gn = 0;
static HWND hw = NULL;
static HWND hs = NULL;
static HWND* hb = NULL;
static int bn = 0;
static int ld = 1;
static WCHAR ed[MAX_PATH];
static HINSTANCE mi;

LRESULT CALLBACK WndP(HWND, UINT, WPARAM, LPARAM);
DWORD WINAPI LdT(LPVOID);
void MkB(HWND);
void CpT(HWND, int);

static HFONT g_font = NULL;
static HANDLE g_mu = NULL;

int WINAPI WinMain(HINSTANCE h, HINSTANCE, LPSTR, int ns) {
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

    hs = CreateWindowW(L"STATIC", L"loading...",
        WS_CHILD|WS_VISIBLE|SS_CENTER,
        PD, PD, W1-PD*2, SH, hw, 0, h, 0);

    ShowWindow(hw, ns);
    UpdateWindow(hw);

    RegisterHotKey(hw, 1, MOD_CONTROL, 'W');

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

DWORD WINAPI LdT(LPVOID) {
    wchar_t pth[MAX_PATH];
    wcscpy_s(pth, MAX_PATH, ed);
    wcscat_s(pth, MAX_PATH, L"/links.json");

    FILE* f = _wfopen(pth, L"rb");
    if (!f) { ld=0; PostMessage(hw, WM_USER+1,0,0); return 0; }

    fseek(f,0,SEEK_END); long sz = ftell(f); fseek(f,0,SEEK_SET);
    if (sz <= 2) { fclose(f); return 0; }

    char* buf = (char*)malloc(sz+1);
    if (!buf) { fclose(f); return 0; }
    fread(buf,1,sz,f); buf[sz]=0; fclose(f);

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

// Subclass proc for hover highlight
LRESULT CALLBACK BtnSub(HWND hb, UINT m, WPARAM w, LPARAM l, UINT_PTR uid, DWORD_PTR ref) {
    switch (m) {
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hb, 0};
        TrackMouseEvent(&tme);
        if (!ref && hb) { SetWindowLongPtrW(hb, GWLP_USERDATA, 1); InvalidateRect(hb,0,1); }
        break;
    }
    case WM_MOUSELEAVE: {
        SetWindowLongPtrW(hb, GWLP_USERDATA, 0);
        InvalidateRect(hb,0,1);
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hb, &ps);
        RECT r; GetClientRect(hb, &r);
        // Default button background
        HBRUSH bg = GetSysColorBrush(COLOR_BTNFACE);
        FillRect(dc, &r, bg);
        // Hover highlight
        if (GetWindowLongPtrW(hb, GWLP_USERDATA)) {
            RECT hr = r;
            InflateRect(&hr, -2, -2);
            HBRUSH hb2 = CreateSolidBrush(RGB(200, 215, 240));
            FillRect(dc, &hr, hb2);
            DeleteObject(hb2);
        }
        // Draw border (sunken look for buttons)
        DrawEdge(dc, &r, EDGE_RAISED, BF_RECT);
        // Draw text
        wchar_t txt[256];
        GetWindowTextW(hb, txt, 256);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(0,0,0));
        HFONT oldf = (HFONT)SelectObject(dc, g_font);
        DrawTextW(dc, txt, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, oldf);
        EndPaint(hb, &ps);
        return 0;
    }
    }
    return DefSubclassProc(hb, m, w, l);
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
            WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            PD, y, max_w, btn_h, h, (HMENU)(INT_PTR)(1000+i), mi, 0);
        if (hb[i]) SetWindowSubclass(hb[i], BtnSub, (UINT_PTR)hb[i], 0);
        SetWindowLongPtrW(hb[i], GWLP_USERDATA, 0);
        if (g_font) SendMessageW(hb[i], WM_SETFONT, (WPARAM)g_font, 1);
    }
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
        SetWindowTextW(hb[ix], L"copied!");
        SetTimer(h, 2000+ix, 800, 0);
    }
}

LRESULT CALLBACK WndP(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_USER+0: MkB(h); InvalidateRect(h,0,1); break;
    case WM_USER+1: if (hs) SetWindowTextW(hs, L"links.json not found"); break;
    case WM_USER+2: if (hs) SetWindowTextW(hs, L"parse error"); break;
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id>=1000 && id<1000+gn) CpT(h, id-1000);
        break;
    }
    case WM_TIMER: {
        int id = (int)w;
        if (id>=2000 && id<2000+gn) {
            int i = id-2000;
            KillTimer(h, id);
            if (hb && i<bn && hb[i]) SetWindowTextW(hb[i], gl[i].name);
        }
        break;
    }
    case WM_HOTKEY:
        if (w == 1) DestroyWindow(h);
        break;
    case WM_DESTROY:
        UnregisterHotKey(h, 1);
        if (g_font) DeleteObject(g_font);
        CloseHandle(g_mu);
        PostQuitMessage(0);
        break;
    default: return DefWindowProcW(h,m,w,l);
    }
    return 0;
}
