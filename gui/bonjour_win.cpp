// UxPlay GUI Launcher — Apple Bonjour detection and automatic installation (Windows).
// See bonjour_win.h for the overview.

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00   // Windows 10 (the installer's minimum): task dialogs, token elevation
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wininet.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>
#include <setupapi.h>
#include <shellapi.h>
#include <commctrl.h>
#include <objbase.h>

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "bonjour_win.h"

namespace bonjour {

const char* kInstallerUrl = "https://download.info.apple.com/Mac_OS_X/061-8098.20100603.gthyu/BonjourPSSetup.exe";
const char* kSupportUrl   = "https://support.apple.com/en-us/106380";

// The installer published at kInstallerUrl (Bonjour Print Services 2.0.2,
// signed by Apple). A file with this exact hash is accepted without further
// checks; anything else must carry a valid Authenticode signature from Apple.
static const char* kInstallerSha256 = "7f1ec347cd429cfb25a34b2147e02231334f28290e0c28be213415b0f99da1a0";

static std::mutex        g_mtx;
static Status            g_status;
static std::atomic<bool> g_busy(false);
static std::atomic<bool> g_cancel(false);
static std::atomic<HWND> g_owner(NULL);

void SetOwnerWindow(void* hwnd) { g_owner = (HWND)hwnd; }

static std::wstring W(const char* s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], n);
    return w;
}

static void Set(State st, const std::string& msg, int percent = -1)
{
    std::lock_guard<std::mutex> lk(g_mtx);
    g_status.state   = st;
    g_status.message = msg;
    g_status.percent = percent;
    if (st != State::Failed) g_status.error.clear();
}

static void Fail(const std::string& err)
{
    std::lock_guard<std::mutex> lk(g_mtx);
    g_status.state   = State::Failed;
    g_status.error   = err;
    g_status.percent = -1;
    g_status.message.clear();
}

// ── Detection ────────────────────────────────────────────────────────────────

bool IsInstalled()
{
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (scm) {
        SC_HANDLE svc = OpenServiceW(scm, L"Bonjour Service", SERVICE_QUERY_STATUS);
        DWORD err = GetLastError();
        if (svc) CloseServiceHandle(svc);
        CloseServiceHandle(scm);
        if (svc) return true;
        if (err == ERROR_SERVICE_DOES_NOT_EXIST) return false;
    }
    // Service manager not reachable: fall back to the service's registry key.
    HKEY k;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\Bonjour Service", 0,
                      KEY_READ, &k) == ERROR_SUCCESS) {
        RegCloseKey(k);
        return true;
    }
    return false;
}

// Starts the service if it is installed but stopped (best effort; needs admin).
static void TryStartService()
{
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scm) return;
    SC_HANDLE svc = OpenServiceW(scm, L"Bonjour Service", SERVICE_QUERY_STATUS | SERVICE_START);
    if (svc) {
        SERVICE_STATUS ss;
        if (QueryServiceStatus(svc, &ss) && ss.dwCurrentState == SERVICE_STOPPED) StartServiceW(svc, 0, NULL);
        CloseServiceHandle(svc);
    }
    CloseServiceHandle(scm);
}

void Refresh()
{
    if (g_busy) return;
    bool ok = IsInstalled();
    std::lock_guard<std::mutex> lk(g_mtx);
    if (ok) { g_status = Status(); g_status.state = State::Installed; }
    else if (g_status.state != State::Failed) { g_status.state = State::Missing; g_status.message.clear(); }
}

bool Busy() { return g_busy; }

Status Snapshot()
{
    std::lock_guard<std::mutex> lk(g_mtx);
    return g_status;
}

// ── Files ────────────────────────────────────────────────────────────────────

static std::wstring WorkDir()
{
    wchar_t tmp[MAX_PATH + 1] = L"";
    DWORD n = GetTempPathW(MAX_PATH + 1, tmp);
    std::wstring d = (n && n <= MAX_PATH) ? std::wstring(tmp) : std::wstring(L"C:\\Windows\\Temp\\");
    d += L"UxPlay\\";
    CreateDirectoryW(d.c_str(), NULL);
    return d;
}

static std::wstring InstallerPath() { return WorkDir() + L"BonjourPSSetup.exe"; }

static bool FileExists(const std::wstring& p)
{
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static std::string Sha256Hex(const std::wstring& path)
{
    std::string hex;
    HCRYPTPROV prov = 0;
    HCRYPTHASH hash = 0;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return hex;
    if (CryptAcquireContextW(&prov, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) &&
        CryptCreateHash(prov, CALG_SHA_256, 0, 0, &hash)) {
        std::vector<BYTE> buf(1 << 16);
        DWORD got = 0;
        bool ok = true;
        while (ok && ReadFile(f, buf.data(), (DWORD)buf.size(), &got, NULL) && got)
            ok = CryptHashData(hash, buf.data(), got, 0) != FALSE;
        BYTE digest[32];
        DWORD len = sizeof(digest);
        if (ok && CryptGetHashParam(hash, HP_HASHVAL, digest, &len, 0)) {
            static const char* hx = "0123456789abcdef";
            for (DWORD i = 0; i < len; ++i) { hex += hx[digest[i] >> 4]; hex += hx[digest[i] & 15]; }
        }
    }
    if (hash) CryptDestroyHash(hash);
    if (prov) CryptReleaseContext(prov, 0);
    CloseHandle(f);
    return hex;
}

// Valid Authenticode signature whose signer is Apple.
static bool SignedByApple(const std::wstring& path)
{
    WINTRUST_FILE_INFO fi = {};
    fi.cbStruct = sizeof(fi);
    fi.pcwszFilePath = path.c_str();
    WINTRUST_DATA wd = {};
    wd.cbStruct = sizeof(wd);
    wd.dwUIChoice = WTD_UI_NONE;
    wd.fdwRevocationChecks = WTD_REVOKE_NONE;
    wd.dwUnionChoice = WTD_CHOICE_FILE;
    wd.pFile = &fi;
    wd.dwStateAction = WTD_STATEACTION_VERIFY;
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    LONG r = WinVerifyTrust((HWND)INVALID_HANDLE_VALUE, &action, &wd);
    bool apple = false;
    if (r == ERROR_SUCCESS) {
        CRYPT_PROVIDER_DATA* pd = WTHelperProvDataFromStateData(wd.hWVTStateData);
        CRYPT_PROVIDER_SGNR* sg = pd ? WTHelperGetProvSignerFromChain(pd, 0, FALSE, 0) : NULL;
        if (sg && sg->csCertChain && sg->pasCertChain && sg->pasCertChain[0].pCert) {
            wchar_t name[256] = L"";
            CertGetNameStringW(sg->pasCertChain[0].pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, NULL, name, 256);
            apple = wcsstr(name, L"Apple") != NULL;
        }
    }
    wd.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust((HWND)INVALID_HANDLE_VALUE, &action, &wd);
    return apple;
}

static bool InstallerTrusted(const std::wstring& path)
{
    return Sha256Hex(path) == kInstallerSha256 || SignedByApple(path);
}

// ── Download ─────────────────────────────────────────────────────────────────

static std::string NetError(DWORD e)
{
    switch (e) {
    case ERROR_INTERNET_NAME_NOT_RESOLVED:
    case ERROR_INTERNET_CANNOT_CONNECT:
    case ERROR_INTERNET_DISCONNECTED:
        return "This PC could not reach Apple's download server. Check the internet connection (or proxy) and try again.";
    case ERROR_INTERNET_TIMEOUT:
        return "The download timed out. Check the internet connection and try again.";
    case ERROR_INTERNET_SEC_CERT_DATE_INVALID:
    case ERROR_INTERNET_SEC_CERT_CN_INVALID:
    case ERROR_INTERNET_INVALID_CA:
    case ERROR_INTERNET_SEC_CERT_ERRORS:
        return "The secure connection to Apple's download server could not be verified (check the PC's date and time).";
    default:
        return "The download failed (network error " + std::to_string(e) + ").";
    }
}

static bool Download(const std::wstring& dest, std::string* err)
{
    Set(State::Downloading, "Connecting to Apple\xe2\x80\xa6", -1);
    HINTERNET net = InternetOpenW(L"UxPlay", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!net) { *err = NetError(GetLastError()); return false; }
    DWORD timeout = 30000;
    InternetSetOptionW(net, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionW(net, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    HINTERNET url = InternetOpenUrlW(net, W(kInstallerUrl).c_str(), NULL, 0,
                                     INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI |
                                     INTERNET_FLAG_NO_COOKIES, 0);
    if (!url) { *err = NetError(GetLastError()); InternetCloseHandle(net); return false; }

    DWORD code = 0, len = sizeof(code), idx = 0;
    HttpQueryInfoW(url, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &code, &len, &idx);
    if (code != 200) {
        *err = "Apple's download server answered with HTTP " + std::to_string(code) +
               " \xe2\x80\x94 the installer may have moved.";
        InternetCloseHandle(url); InternetCloseHandle(net);
        return false;
    }
    DWORD total = 0;
    len = sizeof(total); idx = 0;
    if (!HttpQueryInfoW(url, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, &total, &len, &idx)) total = 0;

    std::wstring part = dest + L".part";
    HANDLE f = CreateFileW(part.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        *err = "Could not save the installer to the temporary folder (error " + std::to_string(GetLastError()) + ").";
        InternetCloseHandle(url); InternetCloseHandle(net);
        return false;
    }
    std::vector<char> buf(1 << 16);
    DWORD got = 0, done = 0, wrote = 0;
    bool ok = true;
    for (;;) {
        if (g_cancel) { ok = false; *err = "The download was cancelled."; break; }
        if (!InternetReadFile(url, buf.data(), (DWORD)buf.size(), &got)) { ok = false; *err = NetError(GetLastError()); break; }
        if (!got) break;
        if (!WriteFile(f, buf.data(), got, &wrote, NULL) || wrote != got) {
            ok = false; *err = "Could not write the installer to disk (is the disk full?)."; break;
        }
        done += got;
        int pct = total ? (int)((unsigned long long)done * 100 / total) : -1;
        char msg[96];
        snprintf(msg, sizeof(msg), "Downloading Bonjour from Apple\xe2\x80\xa6 %.1f MB", done / 1048576.0);
        Set(State::Downloading, msg, pct);
    }
    CloseHandle(f);
    InternetCloseHandle(url);
    InternetCloseHandle(net);
    if (ok && total && done != total) { ok = false; *err = "The download was incomplete. Try again."; }
    if (!ok) { DeleteFileW(part.c_str()); return false; }
    if (!MoveFileExW(part.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        *err = "Could not save the installer (error " + std::to_string(GetLastError()) + ").";
        DeleteFileW(part.c_str());
        return false;
    }
    return true;
}

// Reuses a previously downloaded, verified installer; downloads otherwise.
static bool EnsureInstaller(std::wstring* path, std::string* err)
{
    *path = InstallerPath();
    if (FileExists(*path)) {
        Set(State::Downloading, "Checking the downloaded installer\xe2\x80\xa6", -1);
        if (InstallerTrusted(*path)) {
            std::lock_guard<std::mutex> lk(g_mtx);
            g_status.canRunInteractive = true;
            return true;
        }
        DeleteFileW(path->c_str());
    }
    if (!Download(*path, err)) return false;
    Set(State::Downloading, "Verifying Apple's signature\xe2\x80\xa6", 100);
    if (!InstallerTrusted(*path)) {
        DeleteFileW(path->c_str());
        *err = "The downloaded file is not Apple's signed Bonjour installer, so it was deleted and not run.";
        return false;
    }
    std::lock_guard<std::mutex> lk(g_mtx);
    g_status.canRunInteractive = true;
    return true;
}

// ── Extract Bonjour(64).msi from the installer's CABINET resource ─────────────

struct CabCtx { std::wstring want, outDir, found; };

static UINT CALLBACK CabCallback(PVOID ctx, UINT note, UINT_PTR p1, UINT_PTR)
{
    CabCtx* c = static_cast<CabCtx*>(ctx);
    if (note == SPFILENOTIFY_FILEINCABINET) {
        FILE_IN_CABINET_INFO_W* fi = reinterpret_cast<FILE_IN_CABINET_INFO_W*>(p1);
        if (lstrcmpiW(fi->NameInCabinet, c->want.c_str()) != 0) return FILEOP_SKIP;
        c->found = c->outDir + c->want;
        lstrcpynW(fi->FullTargetName, c->found.c_str(), MAX_PATH);
        return FILEOP_DOIT;
    }
    if (note == SPFILENOTIFY_FILEEXTRACTED) return reinterpret_cast<FILEPATHS_W*>(p1)->Win32Error;
    if (note == SPFILENOTIFY_NEEDNEWCABINET) return ERROR_FILE_NOT_FOUND;
    return NO_ERROR;
}

static bool Is64BitWindows()
{
#ifdef _WIN64
    return true;
#else
    BOOL wow = FALSE;
    return IsWow64Process(GetCurrentProcess(), &wow) && wow;
#endif
}

static bool ExtractMsi(const std::wstring& setup, std::wstring* msi, std::string* err)
{
    std::wstring dir = WorkDir();
    std::wstring cab = dir + L"Bonjour.cab";
    HMODULE m = LoadLibraryExW(setup.c_str(), NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    HRSRC r = m ? FindResourceW(m, L"CABINET", MAKEINTRESOURCEW(10) /* RT_RCDATA */) : NULL;
    HGLOBAL g = r ? LoadResource(m, r) : NULL;
    const void* data = g ? LockResource(g) : NULL;
    DWORD size = r ? SizeofResource(m, r) : 0;
    bool ok = false;
    if (data && size) {
        HANDLE f = CreateFileW(cab.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        DWORD wrote = 0;
        if (f != INVALID_HANDLE_VALUE) {
            ok = WriteFile(f, data, size, &wrote, NULL) && wrote == size;
            CloseHandle(f);
        }
    }
    if (m) FreeLibrary(m);
    if (!ok) { *err = "The Bonjour package inside Apple's installer could not be read."; return false; }

    CabCtx c;
    c.want = Is64BitWindows() ? L"Bonjour64.msi" : L"Bonjour.msi";
    c.outDir = dir;
    DeleteFileW((dir + c.want).c_str());
    BOOL it = SetupIterateCabinetW(cab.c_str(), 0, CabCallback, &c);
    DeleteFileW(cab.c_str());
    if (!it || c.found.empty() || !FileExists(c.found)) {
        *err = "Bonjour could not be unpacked from Apple's installer (error " + std::to_string(GetLastError()) + ").";
        return false;
    }
    *msi = c.found;
    return true;
}

// ── Install ──────────────────────────────────────────────────────────────────

static bool IsElevated()
{
    HANDLE tok = NULL;
    TOKEN_ELEVATION el = {};
    DWORD len = 0;
    bool yes = false;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
        if (GetTokenInformation(tok, TokenElevation, &el, sizeof(el), &len)) yes = el.TokenIsElevated != 0;
        CloseHandle(tok);
    }
    return yes;
}

// Runs a program (elevated through UAC when needed), waits, returns its exit code.
// Returns false when it could not be started (*startErr = Win32 error).
static bool RunAndWait(const std::wstring& file, const std::wstring& params, bool hidden, DWORD* exitCode,
                       DWORD* startErr)
{
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    sei.hwnd = g_owner;   // brings the permission prompt to the front of our window
    sei.lpVerb = IsElevated() ? L"open" : L"runas";
    sei.lpFile = file.c_str();
    sei.lpParameters = params.empty() ? NULL : params.c_str();
    sei.nShow = hidden ? SW_HIDE : SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei) || !sei.hProcess) { *startErr = GetLastError(); return false; }
    WaitForSingleObject(sei.hProcess, INFINITE);
    GetExitCodeProcess(sei.hProcess, exitCode);
    CloseHandle(sei.hProcess);
    return true;
}

static std::string ElevationError(DWORD e)
{
    if (e == ERROR_CANCELLED)
        return "Installing Bonjour needs administrator permission, and the Windows permission prompt was declined.";
    return "The Bonjour installer could not be started (error " + std::to_string(e) + ").";
}

static std::string MsiError(DWORD code, const std::wstring& log)
{
    switch (code) {
    case 1602: return "The installation was cancelled.";
    case 1618: return "Another installation is already running on this PC. Wait for it to finish, then try again.";
    case 1625: return "Installing software is blocked by this PC's policy. Ask your administrator to install Bonjour.";
    case 1925: return "Installing Bonjour needs administrator rights.";
    default: {
        std::string l;
        for (wchar_t ch : log) l += ch < 128 ? (char)ch : '?';
        return "Windows Installer reported error " + std::to_string(code) + ". Details are in " + l + ".";
    }
    }
}

static bool WaitInstalled()
{
    for (int i = 0; i < 20; ++i) {   // the service is registered by the end of msiexec; allow a moment
        if (IsInstalled()) return true;
        Sleep(250);
    }
    return false;
}

static void InstallWorker(bool interactive)
{
    std::wstring setup, msi;
    std::string err;
    if (!EnsureInstaller(&setup, &err)) { Fail(err); return; }

    wchar_t sys[MAX_PATH] = L"C:\\Windows\\System32";
    GetSystemDirectoryW(sys, MAX_PATH);
    DWORD code = 0, startErr = 0;

    if (interactive) {
        Set(State::Installing, "Follow the steps in Apple's installer\xe2\x80\xa6");
        if (!RunAndWait(setup, L"", false, &code, &startErr)) { Fail(ElevationError(startErr)); return; }
        if (!WaitInstalled()) {
            Fail("Apple's installer finished, but the Bonjour service was not found. "
                 "If you cancelled it, run it again; otherwise restart the PC and check again.");
            return;
        }
    } else {
        Set(State::Installing, "Unpacking Bonjour\xe2\x80\xa6");
        if (!ExtractMsi(setup, &msi, &err)) { Fail(err); return; }
        Set(State::Installing, "Installing Bonjour (approve the Windows permission prompt if it appears)\xe2\x80\xa6");
        std::wstring log = WorkDir() + L"bonjour-install.log";
        std::wstring params = L"/i \"" + msi + L"\" /qn /norestart /l*v \"" + log + L"\"";
        if (!RunAndWait(std::wstring(sys) + L"\\msiexec.exe", params, true, &code, &startErr)) {
            Fail(ElevationError(startErr));
            return;
        }
        if (code != 0 && code != 3010 && code != 1641 && !IsInstalled()) { Fail(MsiError(code, log)); return; }
        if (!WaitInstalled()) {
            Fail("The installation finished, but the Bonjour service was not found. Restart the PC and check again.");
            return;
        }
        DeleteFileW(msi.c_str());
    }
    TryStartService();
    std::lock_guard<std::mutex> lk(g_mtx);
    g_status = Status();
    g_status.state = State::Installed;
    g_status.message = code == 3010 || code == 1641 ? "Bonjour is installed. Windows asks for a restart to finish."
                                                     : "Bonjour is installed.";
}

static DWORD WINAPI InstallThread(LPVOID arg)
{
    HRESULT co = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);   // for ShellExecuteEx
    InstallWorker(arg != NULL);
    if (SUCCEEDED(co)) CoUninitialize();
    g_busy = false;
    return 0;
}

void StartInstall(bool interactive)
{
    bool expected = false;
    if (!g_busy.compare_exchange_strong(expected, true)) return;
    if (IsInstalled()) {   // never download when it is already there
        { std::lock_guard<std::mutex> lk(g_mtx); g_status = Status(); g_status.state = State::Installed; }
        g_busy = false;
        return;
    }
    g_cancel = false;
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        bool keep = g_status.canRunInteractive;
        g_status = Status();
        g_status.canRunInteractive = keep;
        g_status.state = interactive ? State::Installing : State::Downloading;
        g_status.message = "Preparing\xe2\x80\xa6";
    }
    HANDLE t = CreateThread(NULL, 0, InstallThread, interactive ? (LPVOID)1 : NULL, 0, NULL);
    if (t) CloseHandle(t);
    else { Fail("Could not start the installation (out of resources)."); g_busy = false; }
}

// ── Setup dialog ("uxplay-gui.exe --install-bonjour") ────────────────────────
// A Windows task dialog (comctl32 v6, activated from the manifest resource with
// ID 2 so the main window's styles are untouched).

typedef HRESULT (WINAPI *TaskDialogIndirectFn)(const TASKDIALOGCONFIG*, int*, int*, BOOL*);

enum { kBtnRetry = 100, kBtnInteractive, kBtnCheck };

struct DlgCtx {
    TASKDIALOGCONFIG progress;
    TASKDIALOGCONFIG failed;
    std::wstring     failText;
    TASKDIALOG_BUTTON failButtons[3];
    std::wstring     shown;   // content text currently on the progress page
    bool onFailPage = false;
    bool marquee = true;
};

static HRESULT CALLBACK DlgProc(HWND h, UINT n, WPARAM wp, LPARAM lp, LONG_PTR ref);

static void BuildFailPage(DlgCtx& c, const Status& st)
{
    std::wstring err = W(st.error.c_str());
    c.failText = err + L"\n\nInstall Bonjour yourself, then choose \x201cI installed it\x201d:\n"
                 L"<a href=\"" + W(kInstallerUrl) + L"\">Download Bonjour Installer</a>\n"
                 L"<a href=\"" + W(kSupportUrl) + L"\">Apple Support \x2014 Bonjour</a>";
    int nb = 0;
    c.failButtons[nb].nButtonID = kBtnRetry;  c.failButtons[nb++].pszButtonText = L"Try again";
    if (st.canRunInteractive) {
        c.failButtons[nb].nButtonID = kBtnInteractive;
        c.failButtons[nb++].pszButtonText = L"Open Apple's installer";
    }
    c.failButtons[nb].nButtonID = kBtnCheck;  c.failButtons[nb++].pszButtonText = L"I installed it \x2014 check again";
    TASKDIALOGCONFIG& f = c.failed;
    f = c.progress;
    f.dwFlags = TDF_ENABLE_HYPERLINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_CALLBACK_TIMER;
    f.pszMainIcon = TD_WARNING_ICON;
    f.pszMainInstruction = L"Bonjour could not be installed automatically";
    f.pszContent = c.failText.c_str();
    f.cButtons = nb;
    f.pButtons = c.failButtons;
    f.dwCommonButtons = TDCBF_CANCEL_BUTTON;   // "Cancel" = continue setup without Bonjour
    f.nDefaultButton = kBtnRetry;
}

static void ShowProgress(HWND h, DlgCtx& c, bool interactive)
{
    c.onFailPage = false;
    c.marquee = true;
    c.shown.clear();
    SendMessageW(h, TDM_NAVIGATE_PAGE, 0, (LPARAM)&c.progress);
    StartInstall(interactive);
}

static HRESULT CALLBACK DlgProc(HWND h, UINT n, WPARAM wp, LPARAM lp, LONG_PTR ref)
{
    DlgCtx& c = *reinterpret_cast<DlgCtx*>(ref);
    switch (n) {
    case TDN_CREATED:
        SetOwnerWindow(h);
        if (!c.onFailPage) SendMessageW(h, TDM_SET_PROGRESS_BAR_MARQUEE, TRUE, 30);
        break;
    case TDN_NAVIGATED:
        if (!c.onFailPage) SendMessageW(h, TDM_SET_PROGRESS_BAR_MARQUEE, TRUE, 30);
        break;
    case TDN_TIMER: {
        if (c.onFailPage) break;
        Status st = Snapshot();
        if (st.state == State::Installed && !Busy()) { SendMessageW(h, TDM_CLICK_BUTTON, IDOK, 0); break; }
        if (st.state == State::Failed && !Busy()) {
            BuildFailPage(c, st);
            c.onFailPage = true;
            SendMessageW(h, TDM_NAVIGATE_PAGE, 0, (LPARAM)&c.failed);
            break;
        }
        std::wstring m = W(st.message.c_str());
        if (m != c.shown) {   // SET (not UPDATE) re-lays out the dialog, so longer text is not clipped
            c.shown = m;
            SendMessageW(h, TDM_SET_ELEMENT_TEXT, TDE_CONTENT, (LPARAM)c.shown.c_str());
        }
        bool wantMarquee = st.percent < 0;
        if (wantMarquee != c.marquee) {
            c.marquee = wantMarquee;
            SendMessageW(h, TDM_SET_MARQUEE_PROGRESS_BAR, wantMarquee, 0);
            SendMessageW(h, TDM_SET_PROGRESS_BAR_MARQUEE, wantMarquee, 30);
            if (!wantMarquee) SendMessageW(h, TDM_SET_PROGRESS_BAR_RANGE, 0, MAKELPARAM(0, 100));
        }
        if (!wantMarquee) SendMessageW(h, TDM_SET_PROGRESS_BAR_POS, st.percent, 0);
        break;
    }
    case TDN_HYPERLINK_CLICKED:
        ShellExecuteW(h, L"open", reinterpret_cast<LPCWSTR>(lp), NULL, NULL, SW_SHOWNORMAL);
        break;
    case TDN_BUTTON_CLICKED:
        if (wp == kBtnRetry)       { ShowProgress(h, c, false); return S_FALSE; }
        if (wp == kBtnInteractive) { ShowProgress(h, c, true);  return S_FALSE; }
        if (wp == kBtnCheck) {
            if (IsInstalled()) return S_OK;   // closes the dialog
            MessageBoxW(h, L"Bonjour is still not installed on this PC.", L"UxPlay Setup", MB_ICONINFORMATION | MB_OK);
            return S_FALSE;
        }
        if (wp == IDCANCEL && !c.onFailPage) {
            if (Busy()) { g_cancel = true; return S_FALSE; }   // stop the download; the failure page follows
        }
        break;
    }
    return S_OK;
}

static TaskDialogIndirectFn LoadTaskDialog(HANDLE* actx, ULONG_PTR* cookie)
{
    wchar_t exe[MAX_PATH] = L"";
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    ACTCTXW ac = {};
    ac.cbSize = sizeof(ac);
    ac.dwFlags = ACTCTX_FLAG_RESOURCE_NAME_VALID;
    ac.lpSource = exe;
    ac.lpResourceName = MAKEINTRESOURCEW(2);
    *actx = CreateActCtxW(&ac);
    if (*actx != INVALID_HANDLE_VALUE) ActivateActCtx(*actx, cookie);
    HMODULE cc = LoadLibraryW(L"comctl32.dll");
    return cc ? (TaskDialogIndirectFn)(void*)GetProcAddress(cc, "TaskDialogIndirect") : NULL;
}

int RunSetupDialog(bool quiet)
{
    if (IsInstalled()) return 0;   // nothing to do, nothing downloaded
    if (quiet) {
        StartInstall(false);
        while (Busy()) Sleep(200);
        return IsInstalled() ? 0 : 1;
    }

    HANDLE actx = INVALID_HANDLE_VALUE;
    ULONG_PTR cookie = 0;
    TaskDialogIndirectFn taskDialog = LoadTaskDialog(&actx, &cookie);
    if (!taskDialog) {   // very old comctl32: install without the progress dialog
        StartInstall(false);
        while (Busy()) Sleep(200);
        if (!IsInstalled()) {
            std::wstring msg = L"Bonjour could not be installed automatically.\n\n" + W(Snapshot().error.c_str()) +
                               L"\n\nDownload it from:\n" + W(kInstallerUrl) + L"\n\nMore information:\n" + W(kSupportUrl);
            MessageBoxW(NULL, msg.c_str(), L"UxPlay Setup", MB_ICONWARNING | MB_OK);
        }
    } else {
        DlgCtx c;
        TASKDIALOGCONFIG& p = c.progress;
        p = TASKDIALOGCONFIG();
        p.cbSize = sizeof(p);
        p.hInstance = GetModuleHandleW(NULL);
        p.dwFlags = TDF_SHOW_MARQUEE_PROGRESS_BAR | TDF_CALLBACK_TIMER | TDF_ALLOW_DIALOG_CANCELLATION |
                    TDF_ENABLE_HYPERLINKS;
        p.pszWindowTitle = L"UxPlay Setup";
        p.pszMainIcon = MAKEINTRESOURCEW(1);   // the app icon
        p.pszMainInstruction = L"Installing Bonjour";
        p.pszContent = L"Preparing\x2026";
        p.pszFooter = L"Bonjour lets iPhone, iPad and Mac find this PC for AirPlay.";
        p.pszFooterIcon = TD_INFORMATION_ICON;
        p.dwCommonButtons = TDCBF_CANCEL_BUTTON;
        p.pfCallback = DlgProc;
        p.lpCallbackData = (LONG_PTR)&c;
        p.cxWidth = 260;
        int button = 0;
        StartInstall(false);
        taskDialog(&p, &button, NULL, NULL);
        g_cancel = true;
        while (Busy()) Sleep(100);   // a cancelled download stops at the next chunk
    }
    if (actx != INVALID_HANDLE_VALUE) { DeactivateActCtx(0, cookie); ReleaseActCtx(actx); }
    return IsInstalled() ? 0 : 1;
}

}  // namespace bonjour
