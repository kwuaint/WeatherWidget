// WeatherWidget - C++ edition, rewritten from python/WeatherWidget.py
//
// Behaviour mirrors the Python implementation:
//   * allocate a free localhost port and serve the shared frontend (weather-widget.html)
//   * proxy /api/* to the public weather APIs; /diag?msg=... appends the message to debug.log
//   * open a 440x860 window (resizable, minimum 380x700) hosting WebView2
//
// Portability notes:
//   * C++17 standard library for strings, files, threads and containers
//   * no Microsoft::WRL, no #pragma comment(lib) and no MSVC import libraries:
//     the WebView2 loader is resolved at runtime with LoadLibrary/GetProcAddress,
//     and a tiny COM smart pointer / callback helper is implemented below
//   * only Windows system DLLs are linked (user32, gdi32, ws2_32, winhttp, ole32)

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>
#include <objbase.h>
#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "WebView2.h"

// ===== Constants =====

static const wchar_t* CLASS_NAME   = L"WeatherWidgetCpp";
static const wchar_t* WINDOW_TITLE = L"\x5929\x6c14"; // 天气
static const wchar_t* HTML_FILE    = L"weather-widget.html";
static const wchar_t* LOADER_DLL   = L"WebView2Loader.dll";
static const wchar_t* USER_AGENT   = L"WeatherWidget-Cpp/1.0";

static const int WINDOW_WIDTH  = 440;
static const int WINDOW_HEIGHT = 860;
static const int MIN_WIDTH     = 380;
static const int MIN_HEIGHT    = 700;

// ===== State =====

static HWND g_hWnd = NULL;
static std::wstring g_exeDir;
static std::wstring g_htmlPath;
static int g_port = 0;
static std::atomic<bool> g_running(false);
static SOCKET g_listenSocket = INVALID_SOCKET;
static std::ofstream g_log;
static std::mutex g_logMutex;

// ===== Logging =====

static std::string Timestamp()
{
    std::time_t now = std::time(nullptr);
    std::tm* local = std::localtime(&now);
    std::ostringstream out;
    if (local)
    {
        out << std::setfill('0') << std::setw(2) << local->tm_hour << ':'
            << std::setfill('0') << std::setw(2) << local->tm_min << ':'
            << std::setfill('0') << std::setw(2) << local->tm_sec;
    }
    else
    {
        out << "??:??:??";
    }
    return out.str();
}

static void Log(const std::string& message)
{
    std::lock_guard<std::mutex> guard(g_logMutex);
    if (!g_log.is_open()) return;
    g_log << Timestamp() << ' ' << message << '\n';
    g_log.flush();
}

// ===== String / path helpers =====

static std::string WideToUtf8(const std::wstring& text)
{
    if (text.empty()) return std::string();
    int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), NULL, 0, NULL, NULL);
    std::string out(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &out[0], size, NULL, NULL);
    return out;
}

static std::wstring Utf8ToWide(const std::string& text)
{
    if (text.empty()) return std::wstring();
    int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), NULL, 0);
    std::wstring out(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), &out[0], size);
    return out;
}

static std::wstring GetExeDirectory()
{
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring result(path);
    size_t pos = result.find_last_of(L"\\/");
    if (pos != std::wstring::npos) result = result.substr(0, pos);
    return result;
}

// Same lookup order as python/WeatherWidget.py: script/exe directory,
// then the project root, then the current working directory.
static std::wstring ResolveHtmlPath()
{
    std::filesystem::path base = g_exeDir;
    std::vector<std::filesystem::path> candidates;
    candidates.push_back(base / HTML_FILE);
    if (base.has_parent_path()) candidates.push_back(base.parent_path() / HTML_FILE);
    candidates.push_back(std::filesystem::current_path() / HTML_FILE);

    for (const std::filesystem::path& candidate : candidates)
    {
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec)) return candidate.wstring();
    }
    return candidates.front().wstring();
}

static int HexValue(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Equivalent of urllib.parse.unquote used for /diag?msg=...
static std::string UrlDecode(const std::string& input)
{
    std::string out;
    out.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i)
    {
        char c = input[i];
        if (c == '%' && i + 2 < input.size())
        {
            int hi = HexValue(input[i + 1]);
            int lo = HexValue(input[i + 2]);
            if (hi >= 0 && lo >= 0)
            {
                out.push_back(static_cast<char>(hi * 16 + lo));
                i += 2;
                continue;
            }
        }
        if (c == '+') { out.push_back(' '); continue; }
        out.push_back(c);
    }
    return out;
}

static std::string HexString(HRESULT value)
{
    std::ostringstream out;
    out << "0x" << std::hex << std::uppercase << (unsigned long)(unsigned int)value;
    return out.str();
}

static std::string JsonEscape(const std::string& value)
{
    std::string out;
    for (char c : value)
    {
        switch (c)
        {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:   out.push_back(c); break;
        }
    }
    return out;
}

// ===== Outbound HTTP (WinHTTP, a Windows system component used for TLS) =====

static std::string HttpGet(const std::string& url)
{
    std::wstring wideUrl = Utf8ToWide(url);

    wchar_t host[256] = {0};
    wchar_t path[2048] = {0};
    URL_COMPONENTS parts;
    ZeroMemory(&parts, sizeof(parts));
    parts.dwStructSize     = sizeof(parts);
    parts.lpszHostName     = host;
    parts.dwHostNameLength = 256;
    parts.lpszUrlPath      = path;
    parts.dwUrlPathLength  = 2048;

    if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &parts))
        return "{\"error\":\"URL parse failed\"}";

    bool isHttps = (parts.nScheme == INTERNET_SCHEME_HTTPS);
    INTERNET_PORT port = isHttps ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;

    HINTERNET session = WinHttpOpen(USER_AGENT, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return "{\"error\":\"WinHttpOpen failed\"}";

    HINTERNET connect = WinHttpConnect(session, host, port, 0);
    if (!connect)
    {
        WinHttpCloseHandle(session);
        return "{\"error\":\"WinHttpConnect failed\"}";
    }

    HINTERNET request = WinHttpOpenRequest(connect, L"GET", path, NULL, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           isHttps ? WINHTTP_FLAG_SECURE : 0);
    if (!request)
    {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return "{\"error\":\"WinHttpOpenRequest failed\"}";
    }

    if (isHttps)
    {
        // Matches the Python edition, which disables certificate verification.
        DWORD securityFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                              SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(request, WINHTTP_OPTION_SECURITY_FLAGS, &securityFlags, sizeof(securityFlags));
    }

    std::string error;
    std::string body;

    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
    {
        error = "WinHttpSendRequest failed";
    }
    else if (!WinHttpReceiveResponse(request, NULL))
    {
        error = "WinHttpReceiveResponse failed";
    }
    else
    {
        char buffer[4096];
        DWORD bytesRead = 0;
        do
        {
            bytesRead = 0;
            if (WinHttpReadData(request, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0)
                body.append(buffer, bytesRead);
        } while (bytesRead > 0);
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);

    if (!error.empty())
    {
        Log("HTTP GET error (" + url + "): " + error);
        return "{\"error\":\"" + JsonEscape(error) + "\"}";
    }
    return body;
}

static std::string BuildApiUrl(const std::string& apiPath)
{
    std::string path = apiPath;
    size_t query = path.find('?');
    if (query != std::string::npos) path = path.substr(0, query);

    if (path.rfind("/search", 0) == 0)
        return "https://geocoding-api.open-meteo.com/v1" + apiPath;
    if (path == "/ipgeo")
        return "https://ipinfo.io/json";
    if (path == "/ipgeo2")
        return "http://ip-api.com/json/?fields=status,message,country,regionName,city,lat,lon,query";
    return "https://api.open-meteo.com/v1" + apiPath;
}

// ===== Local HTTP server (WinSock2) =====

static void SendAll(SOCKET socket, const std::string& data)
{
    size_t sent = 0;
    while (sent < data.size())
    {
        int written = send(socket, data.data() + sent, (int)(data.size() - sent), 0);
        if (written <= 0) break;
        sent += (size_t)written;
    }
}

static void SendResponse(SOCKET socket, const std::string& statusLine,
                         const std::string& contentType, const std::string& body,
                         const std::string& extraHeaders = std::string())
{
    std::ostringstream response;
    response << statusLine << "\r\n"
             << "Content-Type: " << contentType << "\r\n"
             << "Access-Control-Allow-Origin: *\r\n"
             << "Cache-Control: no-cache\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n";
    if (!extraHeaders.empty()) response << extraHeaders << "\r\n";
    response << "\r\n" << body;

    SendAll(socket, response.str());
}

static std::string ReadHtml()
{
    std::ifstream file(std::filesystem::path(g_htmlPath), std::ios::binary);
    if (!file.is_open())
    {
        return "<html><body style='background:#fef9e7;color:#333;text-align:center;padding:40px;"
               "font-family:Microsoft YaHei'><h2>weather-widget.html not found</h2></body></html>";
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

static void HandleClient(SOCKET client)
{
    char buffer[8192];
    int received = recv(client, buffer, sizeof(buffer) - 1, 0);
    if (received <= 0) { closesocket(client); return; }
    buffer[received] = '\0';

    std::istringstream request(buffer);
    std::string method;
    std::string path;
    request >> method >> path;

    if (method == "OPTIONS")
    {
        SendResponse(client, "HTTP/1.1 200 OK", "text/plain", std::string(),
                     "Access-Control-Allow-Methods: GET, OPTIONS\r\nAccess-Control-Allow-Headers: *");
    }
    else if (method == "GET" || method == "HEAD")
    {
        if (path.rfind("/api/", 0) == 0)
        {
            std::string apiUrl = BuildApiUrl(path.substr(4));
            Log("Proxy: " + apiUrl);
            std::string body = HttpGet(apiUrl);
            SendResponse(client, "HTTP/1.1 200 OK", "application/json; charset=utf-8", body);
        }
        else if (path.rfind("/diag", 0) == 0)
        {
            std::string message;
            size_t marker = path.find("?msg=");
            if (marker != std::string::npos) message = UrlDecode(path.substr(marker + 5));
            Log("DIAG: " + message);
            SendResponse(client, "HTTP/1.1 200 OK", "text/plain", "ok");
        }
        else
        {
            SendResponse(client, "HTTP/1.1 200 OK", "text/html; charset=utf-8", ReadHtml());
        }
    }
    else
    {
        SendResponse(client, "HTTP/1.1 405 Method Not Allowed", "text/plain", "method not allowed");
    }

    closesocket(client);
}

static void ServerLoop(int port)
{
    g_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listenSocket == INVALID_SOCKET) { Log("Socket creation failed"); return; }

    sockaddr_in address;
    ZeroMemory(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
    address.sin_port = htons((u_short)port);

    if (bind(g_listenSocket, (sockaddr*)&address, sizeof(address)) == SOCKET_ERROR)
    {
        Log("Bind failed");
        closesocket(g_listenSocket);
        g_listenSocket = INVALID_SOCKET;
        return;
    }

    if (listen(g_listenSocket, 16) == SOCKET_ERROR)
    {
        Log("Listen failed");
        closesocket(g_listenSocket);
        g_listenSocket = INVALID_SOCKET;
        return;
    }

    Log("HTTP server started on port " + std::to_string(port));

    while (g_running)
    {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(g_listenSocket, &readable);
        timeval timeout = {1, 0};

        int ready = select(0, &readable, NULL, NULL, &timeout);
        if (ready > 0 && FD_ISSET(g_listenSocket, &readable))
        {
            SOCKET client = accept(g_listenSocket, NULL, NULL);
            if (client != INVALID_SOCKET) std::thread(HandleClient, client).detach();
        }
    }

    if (g_listenSocket != INVALID_SOCKET)
    {
        closesocket(g_listenSocket);
        g_listenSocket = INVALID_SOCKET;
    }
}

// Same approach as python/WeatherWidget.py: bind port 0 and read the assigned port.
static int FindFreePort()
{
    SOCKET probe = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (probe == INVALID_SOCKET) return 0;

    sockaddr_in address;
    ZeroMemory(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
    address.sin_port = 0;

    int port = 0;
    if (bind(probe, (sockaddr*)&address, sizeof(address)) != SOCKET_ERROR)
    {
        int length = sizeof(address);
        if (getsockname(probe, (sockaddr*)&address, &length) != SOCKET_ERROR)
            port = ntohs(address.sin_port);
    }
    closesocket(probe);
    return port;
}

// ===== Minimal COM support (replacement for Microsoft::WRL) =====

template <typename T>
class ComPtr
{
public:
    ComPtr() = default;
    ComPtr(std::nullptr_t) noexcept {}
    explicit ComPtr(T* ptr) noexcept : ptr_(ptr) { if (ptr_) ptr_->AddRef(); }
    ComPtr(const ComPtr& other) noexcept : ptr_(other.ptr_) { if (ptr_) ptr_->AddRef(); }
    ComPtr(ComPtr&& other) noexcept : ptr_(other.ptr_) { other.ptr_ = nullptr; }
    ~ComPtr() { reset(); }

    ComPtr& operator=(std::nullptr_t) noexcept { reset(); return *this; }
    ComPtr& operator=(T* ptr) noexcept { if (ptr_ != ptr) { ComPtr tmp(ptr); swap(tmp); } return *this; }
    ComPtr& operator=(const ComPtr& other) noexcept { if (this != &other) { ComPtr tmp(other); swap(tmp); } return *this; }
    ComPtr& operator=(ComPtr&& other) noexcept
    {
        if (this != &other) { reset(); ptr_ = other.ptr_; other.ptr_ = nullptr; }
        return *this;
    }

    T* operator->() const noexcept { return ptr_; }
    T* get() const noexcept { return ptr_; }
    T** address_of() noexcept { return &ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }
    bool operator!() const noexcept { return ptr_ == nullptr; }

    void reset() noexcept
    {
        T* tmp = ptr_;
        if (tmp) { ptr_ = nullptr; tmp->Release(); }
    }

    void swap(ComPtr& other) noexcept { T* tmp = ptr_; ptr_ = other.ptr_; other.ptr_ = tmp; }

private:
    T* ptr_ = nullptr;
};

// IID of the WebView2 interfaces used here (avoids the compiler-specific __uuidof).
template <typename T>
struct InterfaceId;

template <>
struct InterfaceId<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>
{
    static const IID& get() { return IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler; }
};

template <>
struct InterfaceId<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>
{
    static const IID& get() { return IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler; }
};

template <>
struct InterfaceId<ICoreWebView2PermissionRequestedEventHandler>
{
    static const IID& get() { return IID_ICoreWebView2PermissionRequestedEventHandler; }
};

namespace detail {

template <typename TDelegate, typename TLambda, typename TSignature>
class LambdaDelegate;

// Specialised on the signature of TDelegate::Invoke so the override matches exactly.
template <typename TDelegate, typename TLambda, typename R, typename... Args>
class LambdaDelegate<TDelegate, TLambda, R (TDelegate::*)(Args...)> : public TDelegate
{
public:
    explicit LambdaDelegate(TLambda fn) : fn_(std::move(fn)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == InterfaceId<TDelegate>::get())
        {
            *ppv = static_cast<TDelegate*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return (ULONG)InterlockedIncrement(&ref_);
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG remaining = (ULONG)InterlockedDecrement(&ref_);
        if (remaining == 0) delete this;
        return remaining;
    }

    R STDMETHODCALLTYPE Invoke(Args... args) override
    {
        return fn_(args...);
    }

private:
    TLambda fn_;
    LONG ref_ = 0;
};

} // namespace detail

// Wraps a lambda in a reference-counted COM callback object.
template <typename TDelegate, typename TLambda>
ComPtr<TDelegate> MakeCallback(TLambda&& fn)
{
    using Impl = detail::LambdaDelegate<TDelegate,
                                        typename std::decay<TLambda>::type,
                                        decltype(&TDelegate::Invoke)>;
    return ComPtr<TDelegate>(static_cast<TDelegate*>(new Impl(std::forward<TLambda>(fn))));
}

// ===== WebView2 =====

static ComPtr<ICoreWebView2Environment> g_environment;
static ComPtr<ICoreWebView2Controller>  g_controller;
static ComPtr<ICoreWebView2>            g_webView;

class PermissionHandler : public ICoreWebView2PermissionRequestedEventHandler
{
public:
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* args) override
    {
        COREWEBVIEW2_PERMISSION_KIND kind;
        args->get_PermissionKind(&kind);
        if (kind == COREWEBVIEW2_PERMISSION_KIND_GEOLOCATION)
            args->put_State(COREWEBVIEW2_PERMISSION_STATE_ALLOW);
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&ref_); }

    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG remaining = (ULONG)InterlockedDecrement(&ref_);
        if (remaining == 0) delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == InterfaceId<ICoreWebView2PermissionRequestedEventHandler>::get())
        {
            *ppv = this;
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

private:
    LONG ref_ = 1;
};

static PermissionHandler* g_permissionHandler = nullptr;

static void ResizeWebView()
{
    if (!g_controller) return;
    RECT bounds;
    GetClientRect(g_hWnd, &bounds);
    g_controller->put_Bounds(bounds);
}

// Resolved at runtime so that no MSVC import library is required.
using CreateEnvironmentFn = HRESULT (STDMETHODCALLTYPE*)(
    PCWSTR browserExecutableFolder,
    PCWSTR userDataFolder,
    ICoreWebView2EnvironmentOptions* environmentOptions,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* handler);

static CreateEnvironmentFn LoadWebView2Loader()
{
    std::wstring localLoader = (std::filesystem::path(g_exeDir) / LOADER_DLL).wstring();

    HMODULE module = LoadLibraryW(localLoader.c_str());
    if (!module) module = LoadLibraryW(LOADER_DLL);
    if (!module)
    {
        Log("LoadLibrary failed for WebView2Loader.dll");
        return nullptr;
    }

    FARPROC proc = GetProcAddress(module, "CreateCoreWebView2EnvironmentWithOptions");
    if (!proc)
    {
        Log("GetProcAddress failed for CreateCoreWebView2EnvironmentWithOptions");
        return nullptr;
    }
    return reinterpret_cast<CreateEnvironmentFn>(proc);
}

static void InitWebView()
{
    Log("InitWebView begin");

    CreateEnvironmentFn createEnvironment = LoadWebView2Loader();
    if (!createEnvironment)
    {
        MessageBoxW(g_hWnd,
                    L"WebView2 loader (WebView2Loader.dll) not found.\r\n"
                    L"Keep it next to the executable.",
                    L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    std::wstring userData = (std::filesystem::path(g_exeDir) / L"wv2data").wstring();

    auto onEnvironmentCreated = [](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
        if (FAILED(result)) { Log("CreateEnv failed: " + HexString(result)); return result; }
        Log("Environment created");
        g_environment = environment;

        auto onControllerCreated = [](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
            if (FAILED(result)) { Log("CreateCtrl failed: " + HexString(result)); return result; }
            Log("Controller created");

            g_controller = controller;
            controller->get_CoreWebView2(g_webView.address_of());

            ComPtr<ICoreWebView2Settings> settings;
            g_webView->get_Settings(settings.address_of());
            if (settings)
            {
                settings->put_IsStatusBarEnabled(FALSE);
                settings->put_AreDefaultContextMenusEnabled(FALSE);
            }

            // pywebview is created with text_select=False; mirror that with a stylesheet.
            const wchar_t* disableSelection =
                L"(function(){var s=document.createElement('style');"
                L"s.textContent='*{user-select:none!important;}';"
                L"(document.head||document.documentElement).appendChild(s);})();";
            g_webView->AddScriptToExecuteOnDocumentCreated(disableSelection, nullptr);

            EventRegistrationToken token;
            g_webView->add_PermissionRequested(g_permissionHandler, &token);

            ResizeWebView();

            std::wstring url = L"http://127.0.0.1:" + std::to_wstring(g_port) + L"/weather.html";
            Log("Navigating to: " + WideToUtf8(url));
            g_webView->Navigate(url.c_str());
            Log("WebView ready");
            return S_OK;
        };

        return environment->CreateCoreWebView2Controller(g_hWnd,
            MakeCallback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(onControllerCreated).get());
    };

    HRESULT result = createEnvironment(NULL, userData.c_str(), NULL,
        MakeCallback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(onEnvironmentCreated).get());

    if (FAILED(result))
    {
        Log("InitWebView FAILED: " + HexString(result));
        MessageBoxW(g_hWnd, L"WebView2 init failed.\r\nInstall Edge WebView2 Runtime.",
                    L"Error", MB_OK | MB_ICONERROR);
    }
}

// ===== Window =====

static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_GETMINMAXINFO:
    {
        MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = MIN_WIDTH;
        info->ptMinTrackSize.y = MIN_HEIGHT;
        return 0;
    }
    case WM_SIZE:
        ResizeWebView();
        return 0;
    case WM_DESTROY:
        g_running = false;
        if (g_listenSocket != INVALID_SOCKET)
        {
            closesocket(g_listenSocket);
            g_listenSocket = INVALID_SOCKET;
        }
        g_webView.reset();
        g_controller.reset();
        g_environment.reset();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, message, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    g_exeDir = GetExeDirectory();
    g_htmlPath = ResolveHtmlPath();

    g_log.open(std::filesystem::path(g_exeDir) / "debug.log", std::ios::app);
    Log("=== WeatherWidget C++ Start ===");
    Log("ExeDir: " + WideToUtf8(g_exeDir));

    std::error_code ec;
    bool htmlExists = std::filesystem::exists(std::filesystem::path(g_htmlPath), ec);
    Log("HTML: " + WideToUtf8(g_htmlPath) + " exists=" + (htmlExists ? "True" : "False"));

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        Log("WSAStartup failed");
        return 1;
    }

    g_port = FindFreePort();
    Log("Port allocated: " + std::to_string(g_port));
    if (g_port == 0)
    {
        Log("Failed to allocate a port");
        WSACleanup();
        return 1;
    }

    g_running = true;
    std::thread(ServerLoop, g_port).detach();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    WNDCLASSEXW windowClass;
    ZeroMemory(&windowClass, sizeof(windowClass));
    windowClass.cbSize        = sizeof(windowClass);
    windowClass.style         = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc   = WndProc;
    windowClass.hInstance     = hInstance;
    windowClass.hCursor       = LoadCursor(NULL, IDC_ARROW);
    windowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    windowClass.lpszClassName = CLASS_NAME;
    RegisterClassExW(&windowClass);

    int screenWidth  = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int left = (screenWidth - WINDOW_WIDTH) / 2;
    int top  = (screenHeight - WINDOW_HEIGHT) / 2;
    if (left < 0) left = 0;
    if (top < 0) top = 0;

    g_hWnd = CreateWindowExW(0, CLASS_NAME, WINDOW_TITLE, WS_OVERLAPPEDWINDOW,
                             left, top, WINDOW_WIDTH, WINDOW_HEIGHT,
                             NULL, NULL, hInstance, NULL);
    if (!g_hWnd)
    {
        Log("CreateWindow failed");
        WSACleanup();
        return 1;
    }

    // WebView2 requires COM to be initialised on the calling thread (STA).
    HRESULT comResult = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    Log("CoInitializeEx: " + HexString(comResult));

    g_permissionHandler = new PermissionHandler();
    InitWebView();

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    MSG message;
    while (GetMessage(&message, NULL, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessage(&message);
    }

    g_running = false;
    WSACleanup();
    if (SUCCEEDED(comResult)) CoUninitialize();
    Log("=== WeatherWidget C++ Exit ===");
    return (int)message.wParam;
}
