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

// ── Settings ──────────────────────────────────────────────────────────────────

struct Settings {
    char  serverName[128] = "";
    int   port            = 7100;
    bool  fullscreen      = false;
    char  windowSize[32]  = "1920x1080";
    bool  pin             = false;
    char  password[128]   = "";
    int   videoSinkIdx    = 0;
    int   videoDecoderIdx = 3;  // default: Auto (decodebin), graceful hardware fallback
    bool  h265            = true;
    int   audioSinkIdx    = 0;
    float volume          = 1.0f;
    int   fps             = 0;    // 0 = use uxplay default; 1-256 = pass -fps N
    bool  noFreeze        = false;
    bool  debug           = false;
    char  extraArgs[256]  = "";
};

struct Option { const char* label; const char* flag; };

static const Option kVideoSinks[] = {
    { "D3D11 (recommended)",  "-vs d3d11videosink"  },
    { "D3D12 (Alt+Enter FS)", "-vs d3d12videosink"  },
    { "Auto",                 ""                    },
};
static const Option kVideoDecoders[] = {
    { "Hardware D3D11",    "-vd d3d11videodec" },
    { "NVIDIA NVDEC",      "-vd nvh264dec"     },
    { "Software (FFmpeg)", "-vd avdec_h264"    },
    { "Auto (decodebin)",  ""                  },
};
static const Option kAudioSinks[] = {
    { "DirectSound",       "-as directsoundsink" },
    { "WASAPI",            "-as wasapisink"       },
    { "WASAPI Exclusive",  "-as wasapi2sink"      },
    { "Auto",              ""                     },
    { "Disabled",          "-a"                   },
};

static std::string BuildArgs(const Settings& s)
{
    std::string args;
    auto append = [&](const std::string& tok) {
        if (!tok.empty()) { if (!args.empty()) args += ' '; args += tok; }
    };

    if (s.serverName[0]) { append("-n"); append(std::string("\"") + s.serverName + "\""); }
    if (s.port != 7100)  { append("-p"); append(std::to_string(s.port)); }
    if (s.fullscreen)    append("-fs");
    if (s.windowSize[0] && std::string(s.windowSize) != "1920x1080") {
        append("-s"); append(s.windowSize);
    }
    if (s.pin)           append("-pin");
    if (s.password[0])   { append("-pw"); append(std::string("\"") + s.password + "\""); }

    append(kVideoSinks[s.videoSinkIdx].flag);
    append(kVideoDecoders[s.videoDecoderIdx].flag);
    if (s.h265) append("-h265");
    if (s.fps > 0) { append("-fps"); append(std::to_string(s.fps)); }
    append(kAudioSinks[s.audioSinkIdx].flag);
    if (s.volume < 0.995f) {
        char vbuf[16]; snprintf(vbuf, sizeof(vbuf), "%.2f", s.volume);
        append("-vol"); append(vbuf);
    }
    if (s.noFreeze) append("-nofreeze");
    if (s.debug)    append("-d");
    if (s.extraArgs[0]) append(s.extraArgs);

    return args;
}

// ── INI persistence ───────────────────────────────────────────────────────────

static void SaveSettings(const Settings& s, const std::string& path)
{
    std::ofstream f(path);
    if (!f) return;
    f << "serverName="      << s.serverName    << "\n";
    f << "port="            << s.port          << "\n";
    f << "fullscreen="      << (s.fullscreen ? 1 : 0) << "\n";
    f << "windowSize="      << s.windowSize    << "\n";
    f << "pin="             << (s.pin ? 1 : 0) << "\n";
    f << "password="        << s.password      << "\n";
    f << "videoSinkIdx="    << s.videoSinkIdx  << "\n";
    f << "videoDecoderIdx=" << s.videoDecoderIdx << "\n";
    f << "h265="            << (s.h265 ? 1 : 0) << "\n";
    f << "audioSinkIdx="    << s.audioSinkIdx  << "\n";
    f << "volume="          << s.volume        << "\n";
    f << "fps="             << s.fps           << "\n";
    f << "noFreeze="        << (s.noFreeze ? 1 : 0) << "\n";
    f << "debug="           << (s.debug ? 1 : 0) << "\n";
    f << "extraArgs="       << s.extraArgs     << "\n";
}

static void LoadSettings(Settings& s, const std::string& path)
{
    std::ifstream f(path);
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        if      (key == "serverName")     strncpy(s.serverName,  val.c_str(), sizeof(s.serverName) - 1);
        else if (key == "port")           s.port = std::stoi(val);
        else if (key == "fullscreen")     s.fullscreen = (val == "1");
        else if (key == "windowSize")     strncpy(s.windowSize, val.c_str(), sizeof(s.windowSize) - 1);
        else if (key == "pin")            s.pin = (val == "1");
        else if (key == "password")       strncpy(s.password,   val.c_str(), sizeof(s.password) - 1);
        else if (key == "videoSinkIdx")   s.videoSinkIdx    = std::stoi(val);
        else if (key == "videoDecoderIdx") s.videoDecoderIdx = std::stoi(val);
        else if (key == "h265")           s.h265 = (val == "1");
        else if (key == "audioSinkIdx")   s.audioSinkIdx    = std::stoi(val);
        else if (key == "volume")         s.volume = std::stof(val);
        else if (key == "fps")            s.fps = std::stoi(val);
        else if (key == "noFreeze")       s.noFreeze = (val == "1");
        else if (key == "debug")          s.debug = (val == "1");
        else if (key == "extraArgs")      strncpy(s.extraArgs,  val.c_str(), sizeof(s.extraArgs) - 1);
    }
    auto clamp = [](int& v, int lo, int hi) { if (v < lo) v = lo; if (v > hi) v = hi; };
    clamp(s.videoSinkIdx,    0, (int)(sizeof(kVideoSinks)    / sizeof(*kVideoSinks))    - 1);
    clamp(s.videoDecoderIdx, 0, (int)(sizeof(kVideoDecoders) / sizeof(*kVideoDecoders)) - 1);
    clamp(s.audioSinkIdx,    0, (int)(sizeof(kAudioSinks)    / sizeof(*kAudioSinks))    - 1);
    if (s.volume < 0.0f) s.volume = 0.0f;
    if (s.volume > 1.0f) s.volume = 1.0f;
    if (s.fps < 0 || s.fps > 256) s.fps = 0;
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

static void LaunchUxPlay(const Settings& s)
{
    if (IsUxPlayRunning()) return;

    std::string args = BuildArgs(s);
    std::wstring wArgs(args.begin(), args.end());

    wchar_t exePath[MAX_PATH];
    wcscpy_s(exePath, g_exeDir);
    wcscat_s(exePath, L"uxplay.exe");

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
    }
}

static void StopUxPlay()
{
    if (!g_hProcess) return;
    TerminateProcess(g_hProcess, 0);
    CloseHandle(g_hProcess); g_hProcess = NULL;
    CloseHandle(g_hThread);  g_hThread  = NULL;
}

// ── Display rotation (window-level, no stream reconnect) ──────────────────────

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

static HMONITOR GetUxPlayMonitor()
{
    HWND uw = FindProcessWindow();
    return uw ? MonitorFromWindow(uw, MONITOR_DEFAULTTONEAREST)
              : MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
}

static const char* OrientationName(DWORD o)
{
    switch (o) {
    case DMDO_DEFAULT: return "0\xc2\xb0";
    case DMDO_90:      return "90\xc2\xb0";
    case DMDO_180:     return "180\xc2\xb0";
    case DMDO_270:     return "270\xc2\xb0";
    default:           return "?";
    }
}

static DWORD CurrentOrientation(HMONITOR hMon)
{
    MONITORINFOEXW mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hMon, &mi)) return DMDO_DEFAULT;
    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);
    EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm);
    return dm.dmDisplayOrientation;
}

static void CycleDisplayRotation()
{
    HMONITOR hMon = GetUxPlayMonitor();
    MONITORINFOEXW mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hMon, &mi)) return;
    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);
    if (!EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) return;

    DWORD next = (dm.dmDisplayOrientation + 1) % 4;
    // swap pixel dimensions when crossing landscape<->portrait boundary
    bool curPortrait  = (dm.dmDisplayOrientation & 1) != 0;
    bool nextPortrait = (next & 1) != 0;
    if (curPortrait != nextPortrait) std::swap(dm.dmPelsWidth, dm.dmPelsHeight);
    dm.dmDisplayOrientation = next;
    dm.dmFields = DM_DISPLAYORIENTATION | DM_PELSWIDTH | DM_PELSHEIGHT;
    ChangeDisplaySettingsExW(mi.szDevice, &dm, NULL, CDS_UPDATEREGISTRY | CDS_RESET, NULL);
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

// ── Combo helper ──────────────────────────────────────────────────────────────

static bool ComboOptions(const char* label, int* current, const Option* opts, int count)
{
    const char* preview = ((*current >= 0 && *current < count) ? opts[*current].label : "");
    bool changed = false;
    if (ImGui::BeginCombo(label, preview)) {
        for (int i = 0; i < count; ++i) {
            bool sel = (*current == i);
            if (ImGui::Selectable(opts[i].label, sel)) { *current = i; changed = true; }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

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
    std::string iniPath = iniDir + "uxplay-settings.ini";

    Settings settings;
    LoadSettings(settings, iniPath);

    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"UxPlayGUI";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowW(L"UxPlayGUI", L"UxPlay Launcher",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 500, 570, NULL, NULL, hInst, NULL);

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

    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    ImVec4 clearColor = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);
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

        // Fixed window filling the entire client area
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("##main", NULL,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        // ── Basic ──────────────────────────────────────────────────────────
        ImGui::SeparatorText("Basic");

        ImGui::SetNextItemWidth(250);
        ImGui::InputText("Server name", settings.serverName, sizeof(settings.serverName));
        ImGui::SameLine(); ImGui::TextDisabled("(empty = hostname)");

        ImGui::SetNextItemWidth(100);
        ImGui::InputInt("Port", &settings.port, 0);
        if (settings.port < 1) settings.port = 1;
        if (settings.port > 65535) settings.port = 65535;

        ImGui::Checkbox("Fullscreen on start (-fs)", &settings.fullscreen);

        ImGui::SetNextItemWidth(120);
        ImGui::InputText("Window size (WxH)", settings.windowSize, sizeof(settings.windowSize));

        ImGui::Checkbox("Require PIN (-pin)", &settings.pin);

        ImGui::SetNextItemWidth(200);
        ImGui::InputText("Password (-pw)", settings.password, sizeof(settings.password),
                         ImGuiInputTextFlags_Password);

        // ── Video ──────────────────────────────────────────────────────────
        ImGui::SeparatorText("Video");

        ImGui::SetNextItemWidth(220);
        ComboOptions("Video sink", &settings.videoSinkIdx,
                     kVideoSinks, (int)(sizeof(kVideoSinks) / sizeof(*kVideoSinks)));

        ImGui::SetNextItemWidth(220);
        ComboOptions("Video decoder", &settings.videoDecoderIdx,
                     kVideoDecoders, (int)(sizeof(kVideoDecoders) / sizeof(*kVideoDecoders)));

        ImGui::Checkbox("H.265 / 4K support (-h265)", &settings.h265);

        // Rotation acts on the display directly — no reconnect needed
        {
            HMONITOR hMon = GetUxPlayMonitor();
            DWORD orient = CurrentOrientation(hMon);
            if (ImGui::Button("Rotate display 90\xc2\xb0"))
                CycleDisplayRotation();
            ImGui::SameLine();
            ImGui::Text("%s%s", OrientationName(orient),
                        IsUxPlayRunning() ? "" : " (primary)");
        }

        // ── Audio ──────────────────────────────────────────────────────────
        ImGui::SeparatorText("Audio");

        ImGui::SetNextItemWidth(220);
        ComboOptions("Audio sink", &settings.audioSinkIdx,
                     kAudioSinks, (int)(sizeof(kAudioSinks) / sizeof(*kAudioSinks)));

        ImGui::SetNextItemWidth(220);
        ImGui::SliderFloat("Volume (-vol)", &settings.volume, 0.0f, 1.0f, "%.2f");

        ImGui::SetNextItemWidth(220);
        ImGui::SliderInt("FPS limit (-fps)", &settings.fps, 0, 256,
                         settings.fps == 0 ? "Default" : "%d fps");

        // ── Advanced ────────────────────────────────────────────────────────
        ImGui::SeparatorText("Advanced");

        ImGui::Checkbox("No freeze on disconnect (-nofreeze)", &settings.noFreeze);
        ImGui::Checkbox("Debug logging (-d)", &settings.debug);

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("Extra arguments", settings.extraArgs, sizeof(settings.extraArgs));

        // ── Command preview ─────────────────────────────────────────────────
        ImGui::SeparatorText("Command");
        std::string preview = "uxplay.exe";
        std::string args = BuildArgs(settings);
        if (!args.empty()) { preview += " "; preview += args; }
        ImGui::TextWrapped("%s", preview.c_str());

        // ── Buttons ─────────────────────────────────────────────────────────
        ImGui::Separator();
        float btnW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

        bool running = IsUxPlayRunning();
        if (!running) {
            if (ImGui::Button("Launch UxPlay", ImVec2(btnW, 36)))
                LaunchUxPlay(settings);
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.1f, 0.1f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
            if (ImGui::Button("Stop UxPlay", ImVec2(btnW, 36)))
                StopUxPlay();
            ImGui::PopStyleColor(3);
        }
        ImGui::SameLine();
        if (ImGui::Button("Save Settings", ImVec2(btnW, 36)))
            SaveSettings(settings, iniPath);

        ImGui::End();

        // Render
        ImGui::Render();
        const float cc[4] = { clearColor.x, clearColor.y, clearColor.z, clearColor.w };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, cc);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    // Cleanup
    StopUxPlay();
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, hInst);
    return 0;
}
