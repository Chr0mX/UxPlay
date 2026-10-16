// UxPlay GUI Launcher — Win32 + D3D11 host for the Dear ImGui interface.
//
// Responsibilities (everything platform-specific lives here; the UI itself is
// in launcher_ui.cpp and the session model in session.cpp):
//   * starting/stopping uxplay.exe without a console window, capturing its
//     stdout/stderr through a pipe drained by a background thread
//   * a rotating diagnostic log in %LOCALAPPDATA%\UxPlay\logs
//   * settings and UI preferences in %APPDATA%\UxPlay (atomic writes)
//   * window placement, DPI, theme, tray icon, single instance, app icon
//
// Process-management notes:
//   * uxplay.exe used to be started with CREATE_NEW_CONSOLE, which is the
//     console window that appeared on every launch. It is now started with
//     CREATE_NO_WINDOW; its output is shown in the in-app console panel and the
//     log file instead. Hiding the console panel only hides a view: the process,
//     the pipe reader and logging are unaffected.
//   * The pipe is read continuously on a dedicated thread so uxplay.exe can
//     never block on a full pipe buffer. Lines are queued under a mutex and
//     consumed on the UI thread once per frame; all UI state is touched only on
//     the UI thread.
//   * Stopping still uses TerminateProcess, exactly as before (uxplay.exe has no
//     console to receive Ctrl+C, and its Windows build has no other stop channel).

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00   // Windows 10 (the installer's minimum): GetDpiForWindow etc.
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dwmapi.h>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <fstream>
#include <sstream>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "launcher_ui.h"
#include "resource.h"

#ifndef UXPLAY_LAUNCHER_VERSION
#define UXPLAY_LAUNCHER_VERSION "dev"
#endif

// ── Strings and paths ─────────────────────────────────────────────────────────

static std::string Utf8(const std::wstring& w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
    return s;
}

static std::wstring Wide(const std::string& s)
{
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

static std::string LastErrorText(DWORD err)
{
    wchar_t* msg = NULL;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, err, 0, (LPWSTR)&msg, 0, NULL);
    std::string out;
    if (msg) { out = Utf8(msg); LocalFree(msg); }
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ' || out.back() == '.'))
        out.pop_back();
    return out.empty() ? "Unknown error" : out;
}

static std::wstring g_exeDir;       // ends with '\'
static std::wstring g_dataDir;      // %APPDATA%\UxPlay\  (settings, prefs)
static std::wstring g_logDir;       // %LOCALAPPDATA%\UxPlay\logs (with trailing separator)

static std::wstring KnownFolder(REFKNOWNFOLDERID id)
{
    PWSTR p = NULL;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, NULL, &p)) && p) out = p;
    if (p) CoTaskMemFree(p);
    return out;
}

// Returns a usable directory (created if needed), falling back to the exe folder.
static std::wstring EnsureDir(const std::wstring& base, const wchar_t* sub)
{
    if (!base.empty()) {
        std::wstring d = base + L"\\" + sub;
        int r = SHCreateDirectoryExW(NULL, d.c_str(), NULL);
        if (r == ERROR_SUCCESS || r == ERROR_ALREADY_EXISTS || r == ERROR_FILE_EXISTS) return d + L"\\";
    }
    return g_exeDir;
}

static bool ReadFileUtf8(const std::wstring& path, std::string* out)
{
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf();
    *out = ss.str();
    return true;
}

// Write to a temporary file, then atomically replace the target, so a crash or
// full disk never leaves a truncated settings file behind.
static bool WriteFileAtomic(const std::wstring& path, const std::string& data)
{
    std::wstring tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(h, data.data(), (DWORD)data.size(), &written, NULL) && written == data.size();
    ok = ok && FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok || !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

// ── Diagnostic log ────────────────────────────────────────────────────────────
// Thread-safe (UI thread + pipe reader). Rotates at 2 MB, keeping one backup.
// Never logs the receiver password: command lines are logged in redacted form.

class Logger {
public:
    void Open(const std::wstring& dir)
    {
        std::lock_guard<std::mutex> lk(m_);
        path_ = dir + L"uxplay.log";
        RotateLocked(true);
    }
    void Write(const char* level, const char* source, const std::string& text)
    {
        std::lock_guard<std::mutex> lk(m_);
        if (!f_) return;
        SYSTEMTIME t; GetLocalTime(&t);
        char head[64];
        int n = snprintf(head, sizeof(head), "%04d-%02d-%02d %02d:%02d:%02d.%03d %-5s %-6s ",
                         t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, level, source);
        if (fwrite(head, 1, n, f_) != (size_t)n || fwrite(text.data(), 1, text.size(), f_) != text.size() ||
            fputc('\n', f_) == EOF) {
            Fail("write failed (disk full?)");
            return;
        }
        fflush(f_);
        size_ += n + text.size() + 1;
        if (size_ > kMaxBytes) RotateLocked(false);
    }
    const char* Problem() { std::lock_guard<std::mutex> lk(m_); return problem_.empty() ? nullptr : problem_.c_str(); }
    std::wstring Path() const { return path_; }
    void Close() { std::lock_guard<std::mutex> lk(m_); if (f_) { fclose(f_); f_ = nullptr; } }

private:
    static const size_t kMaxBytes = 2 * 1024 * 1024;
    void Fail(const char* why) { if (f_) { fclose(f_); f_ = nullptr; } problem_ = why; }
    void RotateLocked(bool startup)
    {
        if (f_) { fclose(f_); f_ = nullptr; }
        WIN32_FILE_ATTRIBUTE_DATA fa;
        bool exists = GetFileAttributesExW(path_.c_str(), GetFileExInfoStandard, &fa) != 0;
        size_t cur = exists ? (size_t)fa.nFileSizeLow : 0;
        if (!startup || cur > kMaxBytes)
            MoveFileExW(path_.c_str(), (path_ + L".1").c_str(), MOVEFILE_REPLACE_EXISTING);
        f_ = _wfopen(path_.c_str(), L"ab");
        if (!f_) { problem_ = "cannot open the log file"; return; }
        problem_.clear();
        fseek(f_, 0, SEEK_END);
        size_ = (size_t)ftell(f_);
    }
    std::mutex   m_;
    FILE*        f_ = nullptr;
    size_t       size_ = 0;
    std::wstring path_;
    std::string  problem_;
};

static Logger g_log;

// ── Receiver process ──────────────────────────────────────────────────────────

struct Receiver {
    HANDLE hProcess = NULL;
    HANDLE hThread  = NULL;
    HANDLE hPipe    = NULL;     // read end of stdout/stderr
    HANDLE hReader  = NULL;     // pipe reader thread
    std::mutex              mtx;
    std::deque<std::string> pending;   // lines waiting for the UI thread
    size_t                  dropped = 0;
};

static Receiver g_rx;
static std::string g_secret;   // receiver password, masked in captured output (set before the reader starts)

static void Redact(std::string& line)
{
    if (g_secret.empty()) return;
    for (size_t p = line.find(g_secret); p != std::string::npos; p = line.find(g_secret, p + 3))
        line.replace(p, g_secret.size(), "***");
}
static Session  g_session;
static double   g_now = 0.0;           // ImGui clock, updated each frame

static DWORD WINAPI PipeReaderThread(LPVOID)
{
    std::string partial;
    char buf[4096];
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(g_rx.hPipe, buf, sizeof(buf), &got, NULL) || got == 0) break;   // EOF / cancelled
        partial.append(buf, got);
        size_t start = 0, nl;
        std::vector<std::string> lines;
        while ((nl = partial.find('\n', start)) != std::string::npos) {
            std::string line = partial.substr(start, nl - start);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
            start = nl + 1;
        }
        partial.erase(0, start);
        if (partial.size() > 64 * 1024) { lines.push_back(partial); partial.clear(); }   // no newline for ages
        for (auto& l : lines) {
            Redact(l);
            if (l.compare(0, 15, "@uxplay-status ") == 0) g_log.Write("EVENT", "uxplay", l.substr(15));
            else g_log.Write("OUT", "uxplay", l);
        }
        std::lock_guard<std::mutex> lk(g_rx.mtx);
        for (auto& l : lines) g_rx.pending.push_back(std::move(l));
        while (g_rx.pending.size() > 20000) { g_rx.pending.pop_front(); ++g_rx.dropped; }   // UI stalled: bound memory
    }
    if (!partial.empty()) {
        Redact(partial);
        g_log.Write("OUT", "uxplay", partial);
        std::lock_guard<std::mutex> lk(g_rx.mtx);
        g_rx.pending.push_back(partial);
    }
    return 0;
}

static void DrainPending()
{
    std::deque<std::string> lines;
    size_t dropped = 0;
    {
        std::lock_guard<std::mutex> lk(g_rx.mtx);
        lines.swap(g_rx.pending);
        dropped = g_rx.dropped; g_rx.dropped = 0;
    }
    if (dropped) g_session.AppNote(g_now, std::to_string(dropped) + " output lines skipped in the console (still in the log file).");
    for (const auto& l : lines) g_session.OnLine(g_now, l);
}

// Joins the reader and closes all handles. The reader may be blocked in ReadFile
// if a helper process inherited the pipe, so pending I/O is cancelled first.
static void ReleaseProcessHandles()
{
    if (g_rx.hReader) {
        if (WaitForSingleObject(g_rx.hReader, 1500) == WAIT_TIMEOUT) {
            CancelSynchronousIo(g_rx.hReader);
            WaitForSingleObject(g_rx.hReader, 2000);
        }
        CloseHandle(g_rx.hReader); g_rx.hReader = NULL;
    }
    if (g_rx.hPipe)    { CloseHandle(g_rx.hPipe);    g_rx.hPipe = NULL; }
    if (g_rx.hThread)  { CloseHandle(g_rx.hThread);  g_rx.hThread = NULL; }
    if (g_rx.hProcess) { CloseHandle(g_rx.hProcess); g_rx.hProcess = NULL; }
    DrainPending();
}

static bool IsUxPlayRunning()
{
    if (!g_rx.hProcess) return false;
    return WaitForSingleObject(g_rx.hProcess, 0) == WAIT_TIMEOUT;
}

// Detects an exit the user did not ask for (called once per frame).
static void PollProcess()
{
    DrainPending();
    if (g_rx.hProcess && WaitForSingleObject(g_rx.hProcess, 0) == WAIT_OBJECT_0) {
        DWORD code = 0;
        GetExitCodeProcess(g_rx.hProcess, &code);
        ReleaseProcessHandles();
        char buf[96];
        snprintf(buf, sizeof(buf), "uxplay.exe exited on its own with code %lu", (unsigned long)code);
        g_log.Write("WARN", "app", buf);
        g_session.OnProcessExited(g_now, (long)code);
    }
}

static void SetOverlayRotation(int rot);

static void LaunchUxPlay(const Settings& s)
{
    if (IsUxPlayRunning()) return;                 // never two receivers at once
    if (g_rx.hProcess) ReleaseProcessHandles();    // previous run's leftovers

    std::wstring exePath = g_exeDir + L"uxplay.exe";
    if (GetFileAttributesW(exePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        g_log.Write("ERROR", "app", "uxplay.exe not found at " + Utf8(exePath));
        g_session.OnLaunchFailed(g_now, MakeMissingExeError(Utf8(exePath)));
        return;
    }

    std::string args = BuildArgs(s);
    g_secret = s.password;
    std::wstring cmdLine = L"\"" + exePath + L"\"";
    if (!args.empty()) cmdLine += L" " + Wide(args);
    std::string redacted = "uxplay.exe " + BuildArgs(s, true);

    // stdout+stderr -> one pipe; stdin -> NUL. Only these two handles are inherited.
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE rd = NULL, wr = NULL;
    if (!CreatePipe(&rd, &wr, &sa, 64 * 1024)) {
        DWORD e = GetLastError();
        g_log.Write("ERROR", "app", "CreatePipe failed: " + LastErrorText(e));
        g_session.OnLaunchFailed(g_now, MakeLaunchError(LastErrorText(e), e));
        return;
    }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);

    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attrSize);
    std::vector<char> attrBuf(attrSize);
    auto attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
    HANDLE inherit[2] = { wr, nul };
    bool haveList = InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize) &&
                    UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit,
                                              (nul != INVALID_HANDLE_VALUE ? 2 : 1) * sizeof(HANDLE), NULL, NULL);

    STARTUPINFOEXW si = {};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdOutput = wr;
    si.StartupInfo.hStdError  = wr;
    si.StartupInfo.hStdInput  = nul != INVALID_HANDLE_VALUE ? nul : NULL;
    if (haveList) si.lpAttributeList = attrs;

    PROCESS_INFORMATION pi = {};
    std::vector<wchar_t> buf(cmdLine.begin(), cmdLine.end());
    buf.push_back(0);
    BOOL ok = CreateProcessW(NULL, buf.data(), NULL, NULL, TRUE,
                             CREATE_NO_WINDOW | (haveList ? EXTENDED_STARTUPINFO_PRESENT : 0),
                             NULL, g_exeDir.c_str(), &si.StartupInfo, &pi);
    DWORD err = ok ? 0 : GetLastError();
    if (haveList) DeleteProcThreadAttributeList(attrs);
    CloseHandle(wr);                                   // child owns the write end now
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);

    if (!ok) {
        CloseHandle(rd);
        g_log.Write("ERROR", "app", "CreateProcess failed: " + LastErrorText(err));
        g_session.OnLaunchFailed(g_now, MakeLaunchError(LastErrorText(err), err));
        return;
    }
    g_rx.hProcess = pi.hProcess;
    g_rx.hThread  = pi.hThread;
    g_rx.hPipe    = rd;
    g_rx.hReader  = CreateThread(NULL, 0, PipeReaderThread, NULL, 0, NULL);
    g_log.Write("INFO", "app", "Started (PID " + std::to_string(pi.dwProcessId) + "): " + redacted);
    g_session.OnLaunched(g_now, pi.dwProcessId, redacted);
}

static void StopUxPlay()
{
    if (!g_rx.hProcess) return;
    SetOverlayRotation(0);
    if (IsUxPlayRunning()) {
        g_session.OnStoppedByUser(g_now);   // first, so the exit is not reported as a crash
        TerminateProcess(g_rx.hProcess, 0);
        WaitForSingleObject(g_rx.hProcess, 3000);
        g_log.Write("INFO", "app", "Receiver stopped by user");
    }
    ReleaseProcessHandles();
}

// ── Player window rotation overlay (GDI PlgBlt, no stream reconnect) ──────────

static HWND   g_hOverlay   = NULL;
static int    g_overlayRot = 0;   // 0=none, 1=90°CW, 2=180°, 3=270°CW

static HWND FindProcessWindow()
{
    if (!IsUxPlayRunning()) return NULL;
    DWORD targetPid = GetProcessId(g_rx.hProcess);
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

// ── Icons ─────────────────────────────────────────────────────────────────────

static HICON g_iconBig = NULL, g_iconSmall = NULL;

static void LoadAppIcons(HINSTANCE hInst)
{
    g_iconBig   = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_UXPLAY), IMAGE_ICON,
                                    GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
    g_iconSmall = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_UXPLAY), IMAGE_ICON,
                                    GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    if (!g_iconBig)   g_iconBig   = LoadIcon(NULL, IDI_APPLICATION);
    if (!g_iconSmall) g_iconSmall = g_iconBig;
}

// The player window belongs to uxplay.exe (created by GStreamer); give it the
// same icon so it matches in the taskbar and Alt+Tab.
static HWND g_brandedPlayer = NULL;
static void BrandPlayerWindow()
{
    HWND h = FindProcessWindow();
    if (!h || h == g_brandedPlayer) return;
    SendMessageTimeoutW(h, WM_SETICON, ICON_BIG,   (LPARAM)g_iconBig,   SMTO_ABORTIFHUNG, 200, NULL);
    SendMessageTimeoutW(h, WM_SETICON, ICON_SMALL, (LPARAM)g_iconSmall, SMTO_ABORTIFHUNG, 200, NULL);
    g_brandedPlayer = h;
}

// ── System tray ───────────────────────────────────────────────────────────────

static NOTIFYICONDATAW g_nid       = {};
static bool            g_inTray    = false;
static bool            g_trayHinted = false;
#define WM_TRAYICON     (WM_APP + 1)
#define WM_APP_ACTIVATE (WM_APP + 2)   // sent by a second launcher instance
#define WM_APP_EXIT     (WM_APP + 3)   // File > Exit, deferred until the frame ends

static void MinimizeToTray(HWND hWnd)
{
    if (g_inTray) return;
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = hWnd;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon            = g_iconSmall;
    wcscpy_s(g_nid.szTip, IsUxPlayRunning() ? L"UxPlay \x2014 receiver running" : L"UxPlay");
    if (!g_trayHinted && IsUxPlayRunning()) {   // explain once why the window disappeared
        g_nid.uFlags |= NIF_INFO;
        wcscpy_s(g_nid.szInfoTitle, L"UxPlay is still running");
        wcscpy_s(g_nid.szInfo, L"The receiver keeps running in the notification area. "
                               L"Double-click the icon to reopen, or right-click to stop.");
        g_nid.dwInfoFlags = NIIF_USER | NIIF_NOSOUND;
        g_trayHinted = true;
    }
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    ShowWindow(hWnd, SW_HIDE);
    g_inTray = true;
}

static void RestoreWindow(HWND hWnd)
{
    if (g_inTray) { Shell_NotifyIconW(NIM_DELETE, &g_nid); g_inTray = false; }
    ShowWindow(hWnd, SW_SHOW);
    if (IsIconic(hWnd)) ShowWindow(hWnd, SW_RESTORE);
    SetForegroundWindow(hWnd);
}

// ── Preferences, theme and placement ──────────────────────────────────────────

static Settings g_settings;
static UiPrefs  g_prefs;
static UiState  g_ui;
static bool     g_prefsDirty = false;
static double   g_prefsDirtyAt = 0.0;
static HWND     g_hwnd = NULL;
static bool     g_dpiChanged = false;
static float    g_dpiScale = 1.0f;

static std::wstring SettingsPath() { return g_dataDir + L"uxplay-settings.ini"; }
static std::wstring PrefsPath()    { return g_dataDir + L"launcher-prefs.ini"; }

static void LoadSettingsFile()
{
    std::string text;
    if (ReadFileUtf8(SettingsPath(), &text)) {
        ParseSettings(g_settings, text);
        g_log.Write("INFO", "app", "Loaded settings from " + Utf8(SettingsPath()));
    } else if (ReadFileUtf8(g_exeDir + L"uxplay-settings.ini", &text)) {
        // Older launchers saved next to the exe (not writable under Program Files).
        // Read it once; the next save goes to %APPDATA%. The old file is left untouched.
        ParseSettings(g_settings, text);
        g_log.Write("INFO", "app", "Imported settings from " + Utf8(g_exeDir) + "uxplay-settings.ini");
    } else {
        g_log.Write("INFO", "app", "No saved settings; using defaults");
    }
}

static bool HostSave(const Settings& s)
{
    bool ok = WriteFileAtomic(SettingsPath(), SerializeSettings(s));
    if (ok) g_log.Write("INFO", "app", "Settings saved");
    else    g_log.Write("ERROR", "app", "Could not save settings to " + Utf8(SettingsPath()) + ": " + LastErrorText(GetLastError()));
    return ok;
}

static void CaptureWindowGeometry()
{
    if (!g_hwnd) return;
    WINDOWPLACEMENT wp = {};
    wp.length = sizeof(wp);
    if (!GetWindowPlacement(g_hwnd, &wp)) return;
    RECT r = wp.rcNormalPosition;
    g_prefs.x = r.left; g_prefs.y = r.top; g_prefs.w = r.right - r.left; g_prefs.h = r.bottom - r.top;
    g_prefs.maximized = (wp.showCmd == SW_SHOWMAXIMIZED) || (IsZoomed(g_hwnd) != 0);
}

static void SavePrefs(bool captureGeometry = true)
{
    if (captureGeometry) CaptureWindowGeometry();
    if (!WriteFileAtomic(PrefsPath(), SerializePrefs(g_prefs)))
        g_log.Write("WARN", "app", "Could not save launcher preferences: " + LastErrorText(GetLastError()));
    g_prefsDirty = false;
}

static void MarkPrefsDirty() { g_prefsDirty = true; g_prefsDirtyAt = g_now; }

static bool ReadSystemDark()
{
    DWORD v = 1, sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &v, &sz) == ERROR_SUCCESS)
        return v == 0;
    return true;   // the launcher's original look was dark
}

static bool ReadReduceMotion()
{
    BOOL anim = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &anim, 0)) return !anim;
    return false;
}

static void HostThemeApplied(bool dark)
{
    if (!g_hwnd) return;
    BOOL d = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(g_hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &d, sizeof(d));
    COLORREF caption = dark ? RGB(0x11, 0x12, 0x17) : RGB(0xE9, 0xEA, 0xF0);
    DwmSetWindowAttribute(g_hwnd, 35 /* DWMWA_CAPTION_COLOR (Windows 11) */, &caption, sizeof(caption));
}

// Applies saved geometry only if enough of it is visible on a current monitor;
// otherwise the window is centered on the monitor it would have opened on.
static void RestorePlacement(HWND hwnd, int defaultW, int defaultH)
{
    WINDOWPLACEMENT wp = {};
    wp.length = sizeof(wp);
    GetWindowPlacement(hwnd, &wp);
    RECT r = wp.rcNormalPosition;
    if (g_prefs.w > 0 && g_prefs.h > 0) {
        r = { g_prefs.x, g_prefs.y, g_prefs.x + g_prefs.w, g_prefs.y + g_prefs.h };
    } else {
        r.right = r.left + defaultW; r.bottom = r.top + defaultH;
    }
    HMONITOR mon = MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(mon, &mi);
    RECT wa = mi.rcWork, vis;
    int w = r.right - r.left, h = r.bottom - r.top;
    if (w > wa.right - wa.left) w = wa.right - wa.left;
    if (h > wa.bottom - wa.top) h = wa.bottom - wa.top;
    bool visibleEnough = IntersectRect(&vis, &r, &wa) && (vis.right - vis.left) >= 160 && (vis.bottom - vis.top) >= 80
                         && r.top >= wa.top - 8;   // title bar must be reachable
    if (!visibleEnough || g_prefs.w == 0) {
        r.left = wa.left + ((wa.right - wa.left) - w) / 2;
        r.top  = wa.top  + ((wa.bottom - wa.top) - h) / 2;
    }
    r.right = r.left + w; r.bottom = r.top + h;
    wp.rcNormalPosition = r;
    wp.showCmd = g_prefs.maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
    wp.flags = 0;
    SetWindowPlacement(hwnd, &wp);
}

// ── UI host bindings ──────────────────────────────────────────────────────────

static void OpenPath(const std::wstring& p)
{
    HINSTANCE r = ShellExecuteW(g_hwnd, L"open", p.c_str(), NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32) g_log.Write("WARN", "app", "Could not open " + Utf8(p));
}

static void HostStart(const Settings& s)      { LaunchUxPlay(s); }
static void HostStop()                        { StopUxPlay(); }
static int  HostGetRotation()                 { return g_overlayRot; }
static void HostSetRotation(int rot)          { SetOverlayRotation(rot); }
static void HostOpenLogFile()                 { OpenPath(g_log.Path()); }
static void HostOpenLogFolder()               { OpenPath(g_logDir); }
static void HostOpenSettingsFolder()          { OpenPath(g_dataDir); }
static const char* HostLogStatus()            { return g_log.Problem(); }
static void HostExit()                        { if (g_hwnd) PostMessageW(g_hwnd, WM_APP_EXIT, 0, 0); }

static void HostOpenDocs()
{
    const wchar_t* local[] = { L"README-Windows.txt", L"README.html", L"README.md" };
    for (const wchar_t* f : local) {
        std::wstring p = g_exeDir + f;
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) { OpenPath(p); return; }
    }
    OpenPath(L"https://github.com/Chr0mX/UxPlay#readme");
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
    if (hr == DXGI_ERROR_UNSUPPORTED)   // no hardware D3D11 (e.g. some VMs): use WARP
        hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_WARP, NULL, 0,
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
        if (wParam == SIZE_MAXIMIZED || wParam == SIZE_RESTORED) MarkPrefsDirty();
        return 0;
    case WM_EXITSIZEMOVE:
        MarkPrefsDirty();
        return 0;
    case WM_GETMINMAXINFO: {
        UINT dpi = GetDpiForWindow(hWnd);
        if (!dpi) dpi = 96;
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = MulDiv(560, dpi, 96);
        mmi->ptMinTrackSize.y = MulDiv(500, dpi, 96);
        return 0;
    }
    case WM_DPICHANGED: {
        const RECT* r = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(hWnd, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        g_dpiScale = HIWORD(wParam) / 96.0f;
        g_dpiChanged = true;   // fonts and sizes are rebuilt before the next frame
        return 0;
    }
    case WM_SETTINGCHANGE:
        if (lParam && lstrcmpW(reinterpret_cast<LPCWSTR>(lParam), L"ImmersiveColorSet") == 0)
            g_ui.systemDark = ReadSystemDark();
        g_ui.reduceMotion = ReadReduceMotion();
        break;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_MINIMIZE && IsUxPlayRunning()) { MinimizeToTray(hWnd); return 0; }
        if ((wParam & 0xfff0) == SC_KEYMENU)  return 0;
        break;
    case WM_CLOSE:
        // Closing the window while the receiver runs keeps it running in the tray
        // (same as before); use File > Exit or the tray menu to quit.
        if (IsUxPlayRunning()) { MinimizeToTray(hWnd); }
        else                   { DestroyWindow(hWnd); }
        return 0;
    case WM_DESTROY:
        CaptureWindowGeometry();   // last chance: the window is about to go away
        PostQuitMessage(0);
        return 0;
    case WM_APP_EXIT:
        DestroyWindow(hWnd);
        return 0;
    case WM_APP_ACTIVATE:
        RestoreWindow(hWnd);
        return 0;
    case WM_TRAYICON:
        if (lParam == WM_LBUTTONDBLCLK) {
            RestoreWindow(hWnd);
        } else if (lParam == WM_RBUTTONUP) {
            POINT pt; GetCursorPos(&pt);
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, 1, L"Show UxPlay");
            AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
            if (IsUxPlayRunning()) AppendMenuW(menu, MF_STRING, 2, L"Stop Receiver");
            else                   AppendMenuW(menu, MF_STRING, 4, L"Start Receiver");
            AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(menu, MF_STRING, 3, L"Exit");
            SetMenuDefaultItem(menu, 1, FALSE);
            SetForegroundWindow(hWnd);  // required so menu dismisses on click-away
            int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                     pt.x, pt.y, 0, hWnd, NULL);
            DestroyMenu(menu);
            switch (cmd) {
            case 1: RestoreWindow(hWnd); break;
            case 2: StopUxPlay(); break;
            case 4: LaunchUxPlay(g_settings); break;
            case 3: PostMessageW(hWnd, WM_APP_EXIT, 0, 0); break;
            }
        }
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ── WinMain ───────────────────────────────────────────────────────────────────

static UiFonts BuildFonts(float scale)
{
    wchar_t win[MAX_PATH] = L"C:\\Windows";
    GetWindowsDirectoryW(win, MAX_PATH);
    std::string fonts = Utf8(win) + "\\Fonts\\";
    return LoadUiFonts(scale, (fonts + "segoeui.ttf").c_str(), (fonts + "seguisb.ttf").c_str(),
                       (fonts + "consola.ttf").c_str());
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    // One launcher at a time: a second start brings the first one forward
    // instead of starting a second receiver.
    HANDLE single = CreateMutexW(NULL, TRUE, L"Local\\UxPlayLauncher.SingleInstance");
    if (single && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowW(L"UxPlayGUI", NULL);
        if (other) { AllowSetForegroundWindow(ASFW_ANY); PostMessageW(other, WM_APP_ACTIVATE, 0, 0); }
        CloseHandle(single);
        return 0;
    }

    // Executable directory for uxplay.exe, GStreamer and bundled docs.
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    g_exeDir = exe;
    size_t slash = g_exeDir.find_last_of(L'\\');
    g_exeDir = slash == std::wstring::npos ? L"" : g_exeDir.substr(0, slash + 1);

    g_dataDir = EnsureDir(KnownFolder(FOLDERID_RoamingAppData), L"UxPlay");
    g_logDir  = EnsureDir(KnownFolder(FOLDERID_LocalAppData), L"UxPlay\\logs");
    g_log.Open(g_logDir);
    g_log.Write("INFO", "app", std::string("UxPlay launcher ") + UXPLAY_LAUNCHER_VERSION + " starting");

    // Set GStreamer environment so spawned uxplay.exe inherits it (replaces bat file)
    SetEnvironmentVariableW(L"GST_PLUGIN_PATH",        (g_exeDir + L"gst-plugins").c_str());
    SetEnvironmentVariableW(L"GST_PLUGIN_SCANNER",     (g_exeDir + L"gst-plugin-scanner.exe").c_str());
    SetEnvironmentVariableW(L"GST_REGISTRY",           (g_exeDir + L"gstreamer-registry.bin").c_str());
    SetEnvironmentVariableW(L"GST_PLUGIN_SYSTEM_PATH", L"");
    SetEnvironmentVariableW(L"UXPLAY_GUI_STATUS",      L"1");   // machine-readable session events
    {
        std::vector<wchar_t> path(32768);
        DWORD n = GetEnvironmentVariableW(L"PATH", path.data(), (DWORD)path.size());
        std::wstring p = g_exeDir + L";" + std::wstring(path.data(), n < path.size() ? n : 0);
        SetEnvironmentVariableW(L"PATH", p.c_str());
    }

    LoadSettingsFile();
    {
        std::string text;
        if (ReadFileUtf8(PrefsPath(), &text)) g_prefs = ParsePrefs(text);   // invalid values fall back to defaults
    }
    g_ui.savedSnapshot   = SerializeSettings(g_settings);
    g_ui.page            = g_prefs.page;
    g_ui.consoleVisible  = g_prefs.showConsoleOnStartup;   // hidden unless the user opted in
    g_ui.systemDark      = ReadSystemDark();
    g_ui.reduceMotion    = ReadReduceMotion();
    g_ui.launcherVersion = UXPLAY_LAUNCHER_VERSION;

    LoadAppIcons(hInst);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = g_iconBig;
    wc.hIconSm = g_iconSmall;
    wc.hbrBackground = CreateSolidBrush(RGB(0x0C, 0x0D, 0x10));  // no white flash before first frame
    wc.lpszClassName = L"UxPlayGUI";
    RegisterClassExW(&wc);

    WNDCLASSEXW wcOv = {};
    wcOv.cbSize = sizeof(wcOv);
    wcOv.lpfnWndProc = OverlayWndProc;
    wcOv.hInstance = hInst;
    wcOv.lpszClassName = L"UxPlayRotOverlay";
    RegisterClassExW(&wcOv);

    // Per-monitor DPI awareness; the UI scales itself and rebuilds fonts on DPI change.
    ImGui_ImplWin32_EnableDpiAwareness();

    HWND hwnd = CreateWindowExW(0, L"UxPlayGUI", L"UxPlay", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 900, 660, NULL, NULL, hInst, NULL);
    g_hwnd = hwnd;
    UINT dpi = GetDpiForWindow(hwnd);
    g_dpiScale = dpi ? dpi / 96.0f : 1.0f;
    RestorePlacement(hwnd, (int)(920 * g_dpiScale), (int)(680 * g_dpiScale));
    // The restored rectangle may sit on a monitor with a different DPI.
    dpi = GetDpiForWindow(hwnd);
    if (dpi) g_dpiScale = dpi / 96.0f;
    g_ui.scale = g_dpiScale;

    if (!CreateDeviceD3D(hwnd)) {
        g_log.Write("ERROR", "app", "Direct3D 11 is not available; cannot show the launcher window");
        MessageBoxW(hwnd, L"UxPlay could not start its window because Direct3D 11 is not available.\n\n"
                          L"Update your graphics driver and try again.", L"UxPlay", MB_ICONERROR | MB_OK);
        CleanupDeviceD3D();
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, hInst);
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.LogFilename = NULL;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    UiFonts fonts = BuildFonts(g_dpiScale);
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    const UiHost host = {
        IsUxPlayRunning, HostStart, HostStop, HostSave, HostGetRotation, HostSetRotation,
        HostOpenLogFile, HostOpenLogFolder, HostOpenSettingsFolder, HostOpenDocs, HostExit,
        HostThemeApplied, HostLogStatus,
    };

    ShowWindow(hwnd, g_prefs.maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL);
    UpdateWindow(hwnd);

    Phase  loggedPhase = g_session.phase;
    double lastBrand = 0.0;
    bool done = false;

    while (!done) {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        // While hidden in the tray there is nothing to draw; keep the process
        // and pipe monitored at a relaxed pace instead of spinning.
        if (g_inTray || IsIconic(hwnd)) {
            g_now = ImGui::GetTime();
            PollProcess();
            if (g_inTray) {
                std::wstring tip = IsUxPlayRunning() ? L"UxPlay \x2014 " + Wide(PhaseLabel(g_session.phase)) : L"UxPlay";
                if (tip != g_nid.szTip) {
                    wcsncpy_s(g_nid.szTip, tip.c_str(), _TRUNCATE);
                    g_nid.uFlags = NIF_TIP;
                    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
                }
            }
            MsgWaitForMultipleObjects(0, NULL, FALSE, 100, QS_ALLINPUT);
            continue;
        }

        if (g_dpiChanged) {
            g_dpiChanged = false;
            g_ui.scale = g_dpiScale;
            fonts = BuildFonts(g_dpiScale);
            ImGui_ImplDX11_InvalidateDeviceObjects();   // font atlas is rebuilt on the next NewFrame
            g_ui.appliedTheme = -1;                    // re-apply style sizes at the new scale
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        g_now = ImGui::GetTime();

        PollProcess();
        if (g_session.phase != loggedPhase) {
            loggedPhase = g_session.phase;
            std::string what = std::string("State: ") + PhaseLabel(loggedPhase);
            if (!g_session.deviceName.empty() && (loggedPhase == Phase::Connecting || loggedPhase == Phase::Connected))
                what += " (" + g_session.deviceName + ")";
            if (loggedPhase == Phase::Error) what += " - " + g_session.error.title;
            g_log.Write(loggedPhase == Phase::Error ? "WARN" : "INFO", "app", what);
        }
        if (IsUxPlayRunning() && g_now - lastBrand > 1.0) { lastBrand = g_now; BrandPlayerWindow(); }

        DrawLauncherUI(g_settings, g_ui, g_session, g_prefs, host, fonts);

        if (g_ui.prefsDirty) { g_ui.prefsDirty = false; MarkPrefsDirty(); }
        // Debounced: geometry and preference changes are written at most once a second.
        if (g_prefsDirty && g_now - g_prefsDirtyAt > 1.0 && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) SavePrefs();

        ImGui::Render();
        ImVec4 c = UiClearColor();
        const float cc[4] = { c.x, c.y, c.z, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, cc);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    // Shutdown: persist, stop the receiver (as before), release everything.
    SavePrefs(false);   // geometry was captured in WM_DESTROY
    SetOverlayRotation(0);
    StopUxPlay();
    g_log.Write("INFO", "app", "Launcher exiting");
    g_log.Close();
    if (g_inTray) Shell_NotifyIconW(NIM_DELETE, &g_nid);
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    if (IsWindow(hwnd)) DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, hInst);
    UnregisterClassW(wcOv.lpszClassName, hInst);
    if (single) CloseHandle(single);
    return 0;
}
