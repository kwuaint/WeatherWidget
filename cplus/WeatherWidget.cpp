#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <windowsx.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <winhttp.h>
#include <dwmapi.h>
#include <shlwapi.h>
#include <string>
#include <sstream>
#include <fstream>
#include <thread>
#include <atomic>
#include <vector>

#include <wrl.h>
#include "WebView2.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "uuid.lib")

using namespace Microsoft::WRL;

static const wchar_t* CLASS_NAME = L"WeatherWidgetClass";
static const wchar_t* TITLEBAR_CLASS = L"WeatherTitleBarClass";
static const wchar_t* BUTTON_CLASS = L"WeatherButtonClass";
static const wchar_t* WINDOW_TITLE = L"\x5929\x6c14";
static const int WINDOW_WIDTH = 440;
static const int WINDOW_HEIGHT = 860;
static const int TITLE_BAR_HEIGHT = 40;
static const int CORNER_RADIUS = 20;
static const COLORREF PAPER_BG = RGB(254, 249, 231);
static const COLORREF PENCIL_STROKE = RGB(51, 51, 51);

static HWND g_hWnd = NULL;
static HWND g_hTitleBar = NULL;
static HWND g_hCloseBtn = NULL;
static HWND g_hMinBtn = NULL;
static HWND g_hTopMostBtn = NULL;
static bool g_isTopMost = false;
static bool g_isDragging = false;
static POINT g_dragOffset = {0, 0};
static int g_serverPort = 0;
static std::atomic<bool> g_serverRunning(false);
static SOCKET g_listenSocket = INVALID_SOCKET;
static std::wstring g_exeDir;
static std::wstring g_htmlPath;
static FILE* g_logFile = NULL;

#define BTN_CLOSE   1001
#define BTN_MIN     1002
#define BTN_TOPMOST 1003

static void Log(const wchar_t* msg) {
    if (!g_logFile) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    fwprintf(g_logFile, L"%02d:%02d:%02d %s\n", st.wHour, st.wMinute, st.wSecond, msg);
    fflush(g_logFile);
}

static void LogA(const char* msg) {
    if (!g_logFile) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(g_logFile, "%02d:%02d:%02d %s\n", st.wHour, st.wMinute, st.wSecond, msg);
    fflush(g_logFile);
}

static std::wstring GetExeDirectory() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring result(path);
    size_t pos = result.find_last_of(L"\\/");
    if (pos != std::wstring::npos) result = result.substr(0, pos);
    return result;
}

static std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int sz = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string out(sz, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &out[0], sz, NULL, NULL);
    return out;
}

static std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int sz = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0);
    std::wstring out(sz, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &out[0], sz);
    return out;
}

static std::wstring ReadFileToWString(const std::wstring& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return L"";
    std::stringstream ss;
    ss << file.rdbuf();
    return Utf8ToWide(ss.str());
}

static void UpdateWindowRegion() {
    RECT rc;
    GetWindowRect(g_hWnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    HRGN hRgn = CreateRoundRectRgn(0, 0, w, h, CORNER_RADIUS, CORNER_RADIUS);
    SetWindowRgn(g_hWnd, hRgn, TRUE);
    DeleteObject(hRgn);
}

// ===== HTTP Proxy (WinHTTP) =====

static std::string HttpGet(const std::string& url) {
    std::wstring wurl = Utf8ToWide(url);

    WCHAR szHost[256] = {0};
    WCHAR szPath[2048] = {0};
    URL_COMPONENTS uc = {0};
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = szHost; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = szPath; uc.dwUrlPathLength = 2048;

    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        return "{\"error\":\"URL parse failed\"}";
    }

    bool isHttps = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    INTERNET_PORT port = isHttps ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;

    HINTERNET hSession = WinHttpOpen(L"WeatherWidget/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return "{\"error\":\"WinHttpOpen failed\"}";

    HINTERNET hConnect = WinHttpConnect(hSession, szHost, port, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return "{\"error\":\"WinHttpConnect failed\"}"; }

    DWORD openFlags = isHttps ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", szPath, NULL,
                                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, openFlags);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return "{\"error\":\"WinHttpOpenRequest failed\"}"; }

    if (isHttps) {
        DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                         SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
    }

    BOOL bResults = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (!bResults) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return "{\"error\":\"WinHttpSendRequest failed\"}";
    }

    bResults = WinHttpReceiveResponse(hRequest, NULL);
    if (!bResults) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return "{\"error\":\"WinHttpReceiveResponse failed\"}";
    }

    std::string result;
    DWORD bytesRead = 0;
    char buffer[4096];
    do {
        bytesRead = 0;
        WinHttpReadData(hRequest, buffer, sizeof(buffer), &bytesRead);
        if (bytesRead > 0) result.append(buffer, bytesRead);
    } while (bytesRead > 0);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return result;
}

static void HandleClient(SOCKET clientSocket) {
    char buffer[8192];
    int recvLen = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    if (recvLen <= 0) { closesocket(clientSocket); return; }
    buffer[recvLen] = '\0';

    std::string request(buffer);
    std::istringstream iss(request);
    std::string method, path;
    iss >> method >> path;

    std::string response;

    if (path.find("/api/") == 0) {
        std::string apiPath = path.substr(4);
        std::string apiUrl;

        if (apiPath.find("/search") == 0)
            apiUrl = "https://geocoding-api.open-meteo.com/v1" + apiPath;
        else if (apiPath.find("/ipgeo2") == 0)
            apiUrl = "http://ip-api.com/json/?fields=status,message,country,regionName,city,lat,lon,query";
        else if (apiPath.find("/ipgeo") == 0)
            apiUrl = "https://ipinfo.io/json";
        else
            apiUrl = "https://api.open-meteo.com/v1" + apiPath;

        LogA(("Proxy: " + apiUrl).c_str());
        std::string body = HttpGet(apiUrl);

        response = "HTTP/1.1 200 OK\r\n"
                   "Content-Type: application/json; charset=utf-8\r\n"
                   "Access-Control-Allow-Origin: *\r\n"
                   "Cache-Control: no-cache\r\n"
                   "Content-Length: " + std::to_string(body.size()) + "\r\n"
                   "Connection: close\r\n\r\n" + body;
    }
    else if (path.find("/diag") == 0) {
        response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n"
                   "Access-Control-Allow-Origin: *\r\nContent-Length: 2\r\n"
                   "Connection: close\r\n\r\nok";
    }
    else {
        std::string html = WideToUtf8(ReadFileToWString(g_htmlPath));
        if (html.empty()) {
            html = "<html><body style='background:#fef9e7;color:#333;text-align:center;padding:40px'>"
                   "<h2>weather-widget.html not found</h2></body></html>";
        }
        response = "HTTP/1.1 200 OK\r\n"
                   "Content-Type: text/html; charset=utf-8\r\n"
                   "Access-Control-Allow-Origin: *\r\n"
                   "Cache-Control: no-cache\r\n"
                   "Content-Length: " + std::to_string(html.size()) + "\r\n"
                   "Connection: close\r\n\r\n" + html;
    }

    send(clientSocket, response.c_str(), (int)response.size(), 0);
    closesocket(clientSocket);
}

static void ServerLoop() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) { LogA("WSAStartup failed"); return; }

    g_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listenSocket == INVALID_SOCKET) { LogA("Socket creation failed"); return; }

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = 0;

    if (bind(g_listenSocket, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        LogA("Bind failed"); closesocket(g_listenSocket); return;
    }

    int addrLen = sizeof(addr);
    getsockname(g_listenSocket, (sockaddr*)&addr, &addrLen);
    g_serverPort = ntohs(addr.sin_port);

    if (listen(g_listenSocket, 10) == SOCKET_ERROR) {
        LogA("Listen failed"); closesocket(g_listenSocket); return;
    }

    char portMsg[64];
    sprintf(portMsg, "HTTP server on port %d", g_serverPort);
    LogA(portMsg);
    g_serverRunning = true;

    while (g_serverRunning) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(g_listenSocket, &readfds);
        timeval tv = {1, 0};

        int ret = select(0, &readfds, NULL, NULL, &tv);
        if (ret > 0 && FD_ISSET(g_listenSocket, &readfds)) {
            SOCKET client = accept(g_listenSocket, NULL, NULL);
            if (client != INVALID_SOCKET) std::thread(HandleClient, client).detach();
        }
    }
    closesocket(g_listenSocket);
    WSACleanup();
}

// ===== WebView2 =====

static ComPtr<ICoreWebView2Environment> g_wvEnv;
static ComPtr<ICoreWebView2Controller> g_wvCtrl;
static ComPtr<ICoreWebView2> g_wv;

static void ResizeWebView() {
    if (!g_wvCtrl) return;
    RECT rc;
    GetClientRect(g_hWnd, &rc);
    rc.top = TITLE_BAR_HEIGHT;
    g_wvCtrl->put_Bounds(rc);
}

static void NavigateToApp() {
    if (!g_wv) return;
    std::wstring url = L"http://127.0.0.1:" + std::to_wstring(g_serverPort) + L"/weather.html";
    g_wv->Navigate(url.c_str());
}

class PermHandler : public ICoreWebView2PermissionRequestedEventHandler {
public:
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* args) override {
        COREWEBVIEW2_PERMISSION_KIND kind;
        args->get_PermissionKind(&kind);
        if (kind == COREWEBVIEW2_PERMISSION_KIND_GEOLOCATION)
            args->put_State(COREWEBVIEW2_PERMISSION_STATE_ALLOW);
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { return --m_ref; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == __uuidof(ICoreWebView2PermissionRequestedEventHandler)) {
            *ppv = this; AddRef(); return S_OK;
        }
        *ppv = NULL; return E_NOINTERFACE;
    }
private:
    std::atomic<ULONG> m_ref{1};
};

static PermHandler g_permHandler;

static void InitWebView() {
    Log(L"InitWebView begin");
    std::wstring userData = g_exeDir + L"\\wv2data";

    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        NULL, userData.c_str(), NULL,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(hr)) { Log(L"CreateEnv failed"); return hr; }
                Log(L"Env created");
                g_wvEnv = env;

                return env->CreateCoreWebView2Controller(g_hWnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [](HRESULT hr, ICoreWebView2Controller* ctrl) -> HRESULT {
                            if (FAILED(hr)) { Log(L"CreateCtrl failed"); return hr; }
                            Log(L"Ctrl created");
                            g_wvCtrl = ctrl;
                            ctrl->get_CoreWebView2(&g_wv);

                            ICoreWebView2Settings* settings;
                            g_wv->get_Settings(&settings);
                            settings->put_IsStatusBarEnabled(FALSE);
                            settings->put_AreDefaultContextMenusEnabled(FALSE);
                            settings->Release();

                            EventRegistrationToken token;
                            g_wv->add_PermissionRequested(&g_permHandler, &token);

                            ResizeWebView();
                            NavigateToApp();
                            Log(L"WebView ready");
                            return S_OK;
                        }).Get());
            }).Get());

    if (FAILED(hr)) {
        Log(L"InitWebView FAILED");
        MessageBoxW(g_hWnd, L"WebView2 init failed.\r\nInstall Edge WebView2 Runtime.",
                    L"Error", MB_OK | MB_ICONERROR);
    }
}

// ===== Drawing =====

static void DrawTitleBar(HDC hdc, RECT rc) {
    HBRUSH bg = CreateSolidBrush(PAPER_BG);
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    HFONT font = CreateFontW(16, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
    HFONT oldFont = (HFONT)SelectObject(hdc, font);
    SetTextColor(hdc, PENCIL_STROKE);
    SetBkMode(hdc, TRANSPARENT);
    RECT trc = {12, 8, 120, TITLE_BAR_HEIGHT - 8};
    DrawTextW(hdc, WINDOW_TITLE, -1, &trc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);
    DeleteObject(font);

    HPEN pen = CreatePen(PS_SOLID, 2, PENCIL_STROKE);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    MoveToEx(hdc, 0, TITLE_BAR_HEIGHT - 1, NULL);
    LineTo(hdc, rc.right, TITLE_BAR_HEIGHT - 1);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

static void DrawBtn(HWND hWnd, HDC hdc, bool hover) {
    RECT rc;
    GetClientRect(hWnd, &rc);
    wchar_t text[8] = {};
    GetWindowTextW(hWnd, text, 8);

    COLORREF bg = hover ? RGB(230, 230, 230) : PAPER_BG;
    HBRUSH brush = CreateSolidBrush(bg);
    FillRect(hdc, &rc, brush);
    DeleteObject(brush);

    HFONT font = CreateFontW(18, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HFONT oldFont = (HFONT)SelectObject(hdc, font);
    SetTextColor(hdc, hover ? RGB(200, 50, 50) : RGB(80, 80, 80));
    SetBkMode(hdc, TRANSPARENT);
    DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);
    DeleteObject(font);
}

// ===== Window Procedures =====

static LRESULT CALLBACK BtnProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static bool hover = false;
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc; GetClientRect(hWnd, &rc);
        DrawBtn(hWnd, hdc, hover);
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (!hover) { hover = true; InvalidateRect(hWnd, NULL, TRUE); TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, hWnd, 0}; TrackMouseEvent(&t); }
        return 0;
    case WM_MOUSELEAVE:
        hover = false; InvalidateRect(hWnd, NULL, TRUE);
        return 0;
    case WM_LBUTTONUP: {
        int id = GetDlgCtrlID(hWnd);
        SendMessage(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), (LPARAM)hWnd);
        return 0;
    }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static LRESULT CALLBACK TitleProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc; GetClientRect(hWnd, &rc);
        DrawTitleBar(hdc, rc);
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
        g_isDragging = true; SetCapture(hWnd);
        g_dragOffset.x = GET_X_LPARAM(lParam); g_dragOffset.y = GET_Y_LPARAM(lParam);
        return 0;
    case WM_MOUSEMOVE:
        if (g_isDragging) {
            POINT pt; GetCursorPos(&pt);
            SetWindowPos(g_hWnd, NULL, pt.x - g_dragOffset.x, pt.y - g_dragOffset.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }
        return 0;
    case WM_LBUTTONUP:
        g_isDragging = false; ReleaseCapture();
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static void LayoutTitleBar() {
    RECT rc; GetClientRect(g_hWnd, &rc);
    MoveWindow(g_hTitleBar, 0, 0, rc.right, TITLE_BAR_HEIGHT, TRUE);
    MoveWindow(g_hCloseBtn, rc.right - 50, 0, 40, TITLE_BAR_HEIGHT, TRUE);
    MoveWindow(g_hMinBtn, rc.right - 100, 0, 40, TITLE_BAR_HEIGHT, TRUE);
    MoveWindow(g_hTopMostBtn, rc.right - 160, 0, 40, TITLE_BAR_HEIGHT, TRUE);
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hTitleBar = CreateWindowExW(0, TITLEBAR_CLASS, L"", WS_CHILD | WS_VISIBLE,
                                       0, 0, WINDOW_WIDTH, TITLE_BAR_HEIGHT, hWnd, NULL, GetModuleHandle(NULL), NULL);
        g_hCloseBtn = CreateWindowExW(0, BUTTON_CLASS, L"\x00D7", WS_CHILD | WS_VISIBLE,
                                       WINDOW_WIDTH - 50, 0, 40, TITLE_BAR_HEIGHT, hWnd, (HMENU)BTN_CLOSE, GetModuleHandle(NULL), NULL);
        g_hMinBtn = CreateWindowExW(0, BUTTON_CLASS, L"\x2013", WS_CHILD | WS_VISIBLE,
                                     WINDOW_WIDTH - 100, 0, 40, TITLE_BAR_HEIGHT, hWnd, (HMENU)BTN_MIN, GetModuleHandle(NULL), NULL);
        g_hTopMostBtn = CreateWindowExW(0, BUTTON_CLASS, L"\x25B3", WS_CHILD | WS_VISIBLE,
                                         WINDOW_WIDTH - 160, 0, 40, TITLE_BAR_HEIGHT, hWnd, (HMENU)BTN_TOPMOST, GetModuleHandle(NULL), NULL);
        return 0;
    }
    case WM_SIZE:
        ResizeWebView();
        UpdateWindowRegion();
        LayoutTitleBar();
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc; GetClientRect(hWnd, &rc);
        HBRUSH bg = CreateSolidBrush(PAPER_BG);
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);
        HPEN pen = CreatePen(PS_SOLID, 2, PENCIL_STROKE);
        HGDIOBJ op = SelectObject(hdc, pen);
        HGDIOBJ ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, CORNER_RADIUS, CORNER_RADIUS);
        SelectObject(hdc, op);
        SelectObject(hdc, ob);
        DeleteObject(pen);
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case BTN_CLOSE: PostMessage(hWnd, WM_CLOSE, 0, 0); break;
        case BTN_MIN: ShowWindow(hWnd, SW_MINIMIZE); break;
        case BTN_TOPMOST:
            g_isTopMost = !g_isTopMost;
            SetWindowPos(hWnd, g_isTopMost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            InvalidateRect(g_hTopMostBtn, NULL, TRUE);
            break;
        }
        return 0;
    case WM_NCHITTEST: {
        LRESULT hit = DefWindowProc(hWnd, msg, wParam, lParam);
        if (hit == HTCLIENT) {
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hWnd, &pt);
            if (pt.y < TITLE_BAR_HEIGHT) return HTCAPTION;
        }
        return hit;
    }
    case WM_DESTROY:
        g_serverRunning = false;
        if (g_listenSocket != INVALID_SOCKET) { closesocket(g_listenSocket); g_listenSocket = INVALID_SOCKET; }
        g_wv.Reset(); g_wvCtrl.Reset(); g_wvEnv.Reset();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nShow) {
    g_exeDir = GetExeDirectory();
    g_htmlPath = g_exeDir + L"\\weather-widget.html";

    std::wstring logPath = g_exeDir + L"\\debug.log";
    g_logFile = _wfopen(logPath.c_str(), L"w");
    Log(L"=== WeatherWidget C++ Start ===");

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(PAPER_BG);
    wc.lpszClassName = CLASS_NAME;
    RegisterClassExW(&wc);

    WNDCLASSEXW bwc = {};
    bwc.cbSize = sizeof(bwc);
    bwc.style = CS_HREDRAW | CS_VREDRAW;
    bwc.lpfnWndProc = BtnProc;
    bwc.hInstance = hInst;
    bwc.hCursor = LoadCursor(NULL, IDC_HAND);
    bwc.hbrBackground = CreateSolidBrush(PAPER_BG);
    bwc.lpszClassName = BUTTON_CLASS;
    RegisterClassExW(&bwc);

    WNDCLASSEXW twc = {};
    twc.cbSize = sizeof(twc);
    twc.style = CS_HREDRAW | CS_VREDRAW;
    twc.lpfnWndProc = TitleProc;
    twc.hInstance = hInst;
    twc.hCursor = LoadCursor(NULL, IDC_SIZEALL);
    twc.hbrBackground = CreateSolidBrush(PAPER_BG);
    twc.lpszClassName = TITLEBAR_CLASS;
    RegisterClassExW(&twc);

    int sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);
    g_hWnd = CreateWindowExW(WS_EX_LAYERED, CLASS_NAME, WINDOW_TITLE, WS_POPUP | WS_VISIBLE,
                              (sx - WINDOW_WIDTH) / 2, (sy - WINDOW_HEIGHT) / 2, WINDOW_WIDTH, WINDOW_HEIGHT,
                              NULL, NULL, hInst, NULL);
    SetLayeredWindowAttributes(g_hWnd, 0, 255, LWA_ALPHA);
    UpdateWindowRegion();

    std::thread(ServerLoop).detach();
    Sleep(200);
    InitWebView();

    ShowWindow(g_hWnd, nShow);
    UpdateWindow(g_hWnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    if (g_logFile) fclose(g_logFile);
    return (int)msg.wParam;
}
