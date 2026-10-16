// UxPlay GUI Launcher — Dear ImGui + Win32 + D3D11
// Provides a settings window to configure and launch uxplay.exe

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <dxgi.h>
#include <tchar.h>
#include <cstdio>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "launcher_ui.h"

#include <dwmapi.h>

// ── INI persistence ───────────────────────────────────────────────────────────

static bool SaveSettings(const Settings& s, const std::string& path)
{
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << SerializeSettings(s);
    return (bool)f;
}

static void LoadSettings(Settings& s, const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return;
    std::stringstream ss; ss << f.rdbuf();
    ParseSettings(s, ss.str());
}

// ── Process management ────────────────────────────────────────────────────────

static HANDLE  g_hProcess = NULL;
static HANDLE  g_hThread  = NULL;
static wchar_t g_exeDir[MAX_PATH] = {};

static bool IsUxPlayRunning()
{
    if (!g_hProcess) return false;
    DWORD code = STILL_ACTIVE;
    GetExitCodeProcess(g_hProcess, &code);
    if (code != STILL_ACTIVE) {
        CloseHandle(g_hProcess); g_hProcess = NULL;
        CloseHandle(g_hThread);  g_hThread  = NULL;
        return false;
    }
    return true;
}

static std::string LastErrorText(DWORD err)
{
    wchar_t* msg = NULL;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, err, 0, (LPWSTR)&msg, 0, NULL);
    std::string out;
    if (msg) {
        int n = WideCharToMultiByte(CP_UTF8, 0, msg, -1, NULL, 0, NULL, NULL);
        if (n > 1) { out.resize(n - 1); WideCharToMultiByte(CP_UTF8, 0, msg, -1, &out[0], n, NULL, NULL); }
        LocalFree(msg);
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) out.pop_back();
    }
    return out;
}

static bool LaunchUxPlay(const Settings& s, std::string* error)
{
    if (IsUxPlayRunning()) return true;

    std::string args = BuildArgs(s);
    std::wstring wArgs(args.begin(), args.end());

    wchar_t exePath[MAX_PATH];
    wcscpy_s(exePath, g_exeDir);
    wcscat_s(exePath, L"uxplay.exe");
    if (GetFileAttributesW(exePath) == INVALID_FILE_ATTRIBUTES) {
        if (error) *error = "uxplay.exe was not found next to the launcher. Reinstall UxPlay or run the launcher from its install folder.";
        return false;
    }

    std::wstring cmdLine = L"\"";
    cmdLine += exePath;
    cmdLine += L"\"";
    if (!wArgs.empty()) { cmdLine += L" "; cmdLine += wArgs; }

    STARTUPINFOW si = {};
    PROCESS_INFORMATION pi = {};
    si.cb = sizeof(si);

    std::vector<wchar_t> buf(cmdLine.begin(), cmdLine.end());
    buf.push_back(0);

    if (CreateProcessW(NULL, buf.data(), NULL, NULL, FALSE,
                       CREATE_NEW_CONSOLE, NULL, g_exeDir[0] ? g_exeDir : NULL,
                       &si, &pi)) {
        g_hProcess = pi.hProcess;
        g_hThread  = pi.hThread;
        return true;
    }
    if (error) *error = "Could not start uxplay.exe: " + LastErrorText(GetLastError());
    return false;
}

static void StopUxPlay()
{
    if (!g_hProcess) return;
    TerminateProcess(g_hProcess, 0);
    CloseHandle(g_hProcess); g_hProcess = NULL;
    CloseHandle(g_hThread);  g_hThread  = NULL;
}

// ── Player window rotation overlay (GDI PlgBlt, no stream reconnect) ──────────

static HWND   g_hOverlay   = NULL;
static int    g_overlayRot = 0;   // 0=none, 1=90°CW, 2=180°, 3=270°CW

static HWND FindProcessWindow()
{
    if (!IsUxPlayRunning()) return NULL;
    DWORD targetPid = GetProcessId(g_hProcess);
    struct Ctx { DWORD pid; HWND hwnd; };
    Ctx ctx = { targetPid, NULL };
    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lp);
        DWORD wpid = 0;
        GetWindowThreadProcessId(hwnd, &wpid);
        if (wpid == c->pid && IsWindowVisible(hwnd)) { c->hwnd = hwnd; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    return ctx.hwnd;
}

static void UpdateOverlay()
{
    if (!g_hOverlay || !IsWindow(g_hOverlay)) return;

    HWND hUx = FindProcessWindow();
    if (!hUx || !IsWindow(hUx)) {
        ShowWindow(g_hOverlay, SW_HIDE);
        return;
    }

    RECT uxRect;
    GetWindowRect(hUx, &uxRect);
    int sw = uxRect.right  - uxRect.left;
    int sh = uxRect.bottom - uxRect.top;
    if (sw <= 0 || sh <= 0) return;

    bool swapDims = (g_overlayRot & 1);
    int ow = swapDims ? sh : sw;
    int oh = swapDims ? sw : sh;

    SetWindowPos(g_hOverlay, HWND_TOPMOST,
                 uxRect.left, uxRect.top, ow, oh,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);

    HDC hdcScreen  = GetDC(NULL);
    HDC hdcCapture = CreateCompatibleDC(hdcScreen);
    HBITMAP hbmCap = CreateCompatibleBitmap(hdcScreen, sw, sh);
    SelectObject(hdcCapture, hbmCap);
    PrintWindow(hUx, hdcCapture, PW_RENDERFULLCONTENT);

    HDC hdcOut  = CreateCompatibleDC(hdcScreen);
    HBITMAP hbmOut = CreateCompatibleBitmap(hdcScreen, ow, oh);
    SelectObject(hdcOut, hbmOut);

    POINT pts[3];
    switch (g_overlayRot) {
    case 1: pts[0]={sh-1,0}; pts[1]={sh-1,sw-1}; pts[2]={0,0};       break; // 90° CW
    case 2: pts[0]={sw-1,sh-1}; pts[1]={0,sh-1}; pts[2]={sw-1,0};    break; // 180°
    case 3: pts[0]={0,sw-1}; pts[1]={0,0}; pts[2]={sh-1,sw-1};       break; // 270° CW
    default: BitBlt(hdcOut, 0, 0, sw, sh, hdcCapture, 0, 0, SRCCOPY); goto done_blt;
    }
    PlgBlt(hdcOut, pts, hdcCapture, 0, 0, sw, sh, NULL, 0, 0);
    done_blt:;

    POINT    ptSrc = {0, 0};
    SIZE     sz    = {ow, oh};
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, 0};
    UpdateLayeredWindow(g_hOverlay, hdcScreen, NULL, &sz, hdcOut, &ptSrc, 0, &bf, ULW_OPAQUE);

    DeleteObject(hbmOut);   DeleteDC(hdcOut);
    DeleteObject(hbmCap);   DeleteDC(hdcCapture);
    ReleaseDC(NULL, hdcScreen);
}

static LRESULT WINAPI OverlayWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_TIMER && wParam == 1) { UpdateOverlay(); return 0; }
    if (msg == WM_PAINT) { PAINTSTRUCT ps; BeginPaint(hWnd, &ps); EndPaint(hWnd, &ps); return 0; }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static void SetOverlayRotation(int rot)
{
    g_overlayRot = rot & 3;
    if (g_overlayRot == 0) {
        if (g_hOverlay) { KillTimer(g_hOverlay, 1); DestroyWindow(g_hOverlay); g_hOverlay = NULL; }
        return;
    }
    if (!g_hOverlay) {
        g_hOverlay = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
            L"UxPlayRotOverlay", NULL, WS_POPUP,
            0, 0, 100, 100, NULL, NULL, GetModuleHandleW(NULL), NULL);
        if (!g_hOverlay) return;
        SetTimer(g_hOverlay, 1, 33, NULL);  // ~30 fps update
    }
    UpdateOverlay();
}

// ── System tray ───────────────────────────────────────────────────────────────

static NOTIFYICONDATA g_nid    = {};
static bool           g_inTray = false;
#define WM_TRAYICON (WM_APP + 1)

static void MinimizeToTray(HWND hWnd)
{
    if (g_inTray) return;
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = hWnd;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon            = (HICON)LoadImage(NULL, IDI_APPLICATION, IMAGE_ICON, 0, 0, LR_SHARED);
    wcscpy_s(g_nid.szTip, L"UxPlay \x2014 running");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    ShowWindow(hWnd, SW_HIDE);
    g_inTray = true;
}

static void RestoreFromTray(HWND hWnd)
{
    if (!g_inTray) return;
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    ShowWindow(hWnd, SW_SHOW);
    ShowWindow(hWnd, SW_RESTORE);
    SetForegroundWindow(hWnd);
    g_inTray = false;
}

// ── D3D11 device / swap chain ─────────────────────────────────────────────────

static ID3D11Device*            g_pd3dDevice          = NULL;
static ID3D11DeviceContext*     g_pd3dDeviceContext    = NULL;
static IDXGISwapChain*          g_pSwapChain           = NULL;
static ID3D11RenderTargetView*  g_mainRenderTargetView = NULL;

static bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60; sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL levels[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
        levels, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain,
        &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (FAILED(hr)) return false;

    ID3D11Texture2D* pBackBuffer = NULL;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (pBackBuffer) {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }
    return true;
}

static void CleanupDeviceD3D()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = NULL; }
    if (g_pSwapChain)           { g_pSwapChain->Release();           g_pSwapChain = NULL; }
    if (g_pd3dDeviceContext)    { g_pd3dDeviceContext->Release();    g_pd3dDeviceContext = NULL; }
    if (g_pd3dDevice)           { g_pd3dDevice->Release();           g_pd3dDevice = NULL; }
}

static void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer = NULL;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (pBackBuffer) {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }
}

static void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = NULL; }
}

// ── Win32 message loop ────────────────────────────────────────────────────────

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_SIZE:
        if (g_pd3dDevice && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_GETMINMAXINFO: {
        UINT dpi = 96;
        HMODULE u32 = GetModuleHandleW(L"user32.dll");
        typedef UINT (WINAPI *GetDpiForWindowFn)(HWND);
        if (auto fn = u32 ? (GetDpiForWindowFn)GetProcAddress(u32, "GetDpiForWindow") : NULL) dpi = fn(hWnd);
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = MulDiv(560, dpi, 96);
        mmi->ptMinTrackSize.y = MulDiv(480, dpi, 96);
        return 0;
    }
    case WM_DPICHANGED: {
        const RECT* r = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(hWnd, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_MINIMIZE) { MinimizeToTray(hWnd); return 0; }
        if ((wParam & 0xfff0) == SC_KEYMENU)  return 0;
        break;
    case WM_CLOSE:
        if (IsUxPlayRunning()) { MinimizeToTray(hWnd); }
        else                   { DestroyWindow(hWnd); }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_TRAYICON:
        if (lParam == WM_LBUTTONDBLCLK) {
            RestoreFromTray(hWnd);
        } else if (lParam == WM_RBUTTONUP) {
            POINT pt; GetCursorPos(&pt);
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, 1, L"Show");
            AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(menu, MF_STRING | (IsUxPlayRunning() ? 0 : MF_GRAYED), 2, L"Stop UxPlay");
            AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(menu, MF_STRING, 3, L"Exit");
            SetForegroundWindow(hWnd);  // required so menu dismisses on click-away
            int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                     pt.x, pt.y, 0, hWnd, NULL);
            DestroyMenu(menu);
            switch (cmd) {
            case 1: RestoreFromTray(hWnd); break;
            case 2: StopUxPlay(); break;
            case 3: StopUxPlay(); Shell_NotifyIconW(NIM_DELETE, &g_nid); DestroyWindow(hWnd); break;
            }
        }
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ── UI host bindings ──────────────────────────────────────────────────────────

static std::string g_iniPath;
static bool HostSave(const Settings& s)            { return SaveSettings(s, g_iniPath); }
static int  HostGetRotation()                      { return g_overlayRot; }
static void HostSetRotation(int rot)               { SetOverlayRotation(rot); }

// ── WinMain ───────────────────────────────────────────────────────────────────

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    // Determine exe directory for relative paths
    GetModuleFileNameW(NULL, g_exeDir, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(g_exeDir, L'\\');
    if (lastSlash) { lastSlash[1] = 0; } else { g_exeDir[0] = 0; }

    // Set GStreamer environment so spawned uxplay.exe inherits it (replaces bat file)
    std::wstring dir(g_exeDir);
    SetEnvironmentVariableW(L"GST_PLUGIN_PATH",        (dir + L"gst-plugins").c_str());
    SetEnvironmentVariableW(L"GST_PLUGIN_SCANNER",     (dir + L"gst-plugin-scanner.exe").c_str());
    SetEnvironmentVariableW(L"GST_REGISTRY",           (dir + L"gstreamer-registry.bin").c_str());
    SetEnvironmentVariableW(L"GST_PLUGIN_SYSTEM_PATH", L"");
    wchar_t existingPath[32768] = {};
    GetEnvironmentVariableW(L"PATH", existingPath, 32767);
    SetEnvironmentVariableW(L"PATH", (dir + L";" + existingPath).c_str());

    std::string iniDir(g_exeDir, g_exeDir + wcslen(g_exeDir));
    g_iniPath = iniDir + "uxplay-settings.ini";

    Settings settings;
    LoadSettings(settings, g_iniPath);
    UiState ui;
    ui.savedSnapshot = SerializeSettings(settings);

    // Register window classes
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hbrBackground = CreateSolidBrush(RGB(0x0C, 0x0D, 0x10));  // no white flash before first frame
    wc.lpszClassName = L"UxPlayGUI";
    RegisterClassExW(&wc);

    WNDCLASSEXW wcOv = {};
    wcOv.cbSize = sizeof(wcOv);
    wcOv.lpfnWndProc = OverlayWndProc;
    wcOv.hInstance = hInst;
    wcOv.lpszClassName = L"UxPlayRotOverlay";
    RegisterClassExW(&wcOv);

    // Per-monitor DPI awareness so text renders crisply; the UI scales itself.
    ImGui_ImplWin32_EnableDpiAwareness();
    POINT origin = {0, 0};
    float dpiScale = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY));
    if (dpiScale < 1.0f) dpiScale = 1.0f;

    const DWORD style = WS_OVERLAPPEDWINDOW;
    const int kClientW = (int)(900 * dpiScale), kClientH = (int)(640 * dpiScale);
    RECT rc = {0, 0, kClientW, kClientH};
    AdjustWindowRect(&rc, style, FALSE);
    HWND hwnd = CreateWindowW(L"UxPlayGUI", L"UxPlay", style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        NULL, NULL, hInst, NULL);

    // Dark title bar + caption color matching the sidebar (Windows 10 20H1+ / 11; ignored elsewhere).
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    COLORREF caption = RGB(0x11, 0x12, 0x17);
    DwmSetWindowAttribute(hwnd, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof(caption));

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, hInst);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ApplyUiTheme(dpiScale);
    UiFonts fonts = LoadUiFonts(dpiScale,
                                "C:\\Windows\\Fonts\\segoeui.ttf",
                                "C:\\Windows\\Fonts\\seguisb.ttf",
                                "C:\\Windows\\Fonts\\consola.ttf");
    const UiHost host = { IsUxPlayRunning, LaunchUxPlay, StopUxPlay, HostSave, HostGetRotation, HostSetRotation };
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    ImVec4 clearColor = UiClearColor();
    bool done = false;

    while (!done) {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        DrawLauncherUI(settings, ui, host, fonts);

        // Render
        ImGui::Render();
        const float cc[4] = { clearColor.x, clearColor.y, clearColor.z, clearColor.w };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, cc);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    // Cleanup
    SetOverlayRotation(0);  // destroy overlay window + timer
    StopUxPlay();
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, hInst);
    UnregisterClassW(wcOv.lpszClassName, hInst);
    return 0;
}
