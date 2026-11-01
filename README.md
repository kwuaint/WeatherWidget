# WeatherWidget

A lightweight desktop weather widget for Windows, with a hand-drawn pencil-sketch UI.

![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11-blue)
![License](https://img.shields.io/badge/license-MIT-yellow)

## Features

- **Pencil-sketch UI** with paper texture, hand-drawn style SVG icons
- **Real-time weather** data from [Open-Meteo](https://open-meteo.com/) (no API key required)
- **Auto geolocation**: Browser GPS -> ipinfo.io -> ip-api.com -> Beijing fallback
- **City search**: Chinese name + pinyin initial support for 55+ Chinese cities
- **City flowers**: Displays local city flower badges inline with city name
- **24-hour & 7-day forecast** with temperature bar visualization
- **Detail panels**: Feels-like temp, humidity, UV index, wind compass, pressure, visibility
- **Sunrise/sunset arc** with real-time progress indicator
- **Custom title bar**: Drag to move, minimize, close, always-on-top toggle
- **Rounded corners** with paper-colored background
- **Local HTTP proxy**: Bypasses CORS restrictions for weather API calls
- **Auto-refresh** every 10 minutes

## Implementations

This project provides three implementations with identical functionality:

| | C# (`csharp/`) | C++ (`cplus/`) | Python (`python/`) |
|---|---|---|---|
| **Window framework** | WinForms | Win32 API | pywebview |
| **Web engine** | WebView2 (.NET) | WebView2 (COM) | WebView2 (Edge) |
| **HTTP proxy** | HttpListener | WinSock2 + WinHTTP | http.server (stdlib) |
| **Compiler/Runtime** | csc.exe (.NET 4.6.2) | cl.exe / g++ | Python 3.10+ |
| **Binary size** | ~800 KB | ~500 KB | N/A (script) |
| **Dependencies** | .NET Framework | None (static) | pywebview |

## Project Structure

```
WeatherWidget/
  README.md              - This file
  LICENSE                - MIT License
  csharp/                - C# implementation (WinForms + WebView2)
    WeatherWidget.cs     - Main application source
    weather-widget.html  - Frontend UI
    launch.bat           - Launch script
    *.dll                - WebView2 runtime libraries
  cplus/                 - C++ implementation (Win32 + WebView2)
    WeatherWidget.cpp    - Main application source
    weather-widget.html  - Frontend UI
    CMakeLists.txt       - CMake build configuration
    build.bat            - Build script (MSVC)
    build-mingw.bat      - Build script (MinGW)
    launch.bat           - Launch script
    sdk/                 - WebView2 SDK headers and static libs
  python/                - Python implementation (pywebview + WebView2)
    WeatherWidget.py     - Main application source
    weather-widget.html  - Frontend UI
    requirements.txt     - Python dependencies
    launch.bat           - Launch script
```

## Prerequisites

- **Windows 10/11** (64-bit)
- **Microsoft Edge WebView2 Runtime** (pre-installed on most systems)
  - Download: https://developer.microsoft.com/en-us/microsoft-edge/webview2/

### Build Tools

**C# version:**
- .NET Framework 4.6.2+ (included with Windows)
- csc.exe compiler (included with .NET Framework)

**C++ version (choose one):**
- Visual Studio 2019/2022 with "Desktop development with C++" workload + CMake
- MinGW-w64 (GCC 10+)

**Python version:**
- Python 3.10+
- pywebview (`pip install pywebview`)

## Building

### C# Version

```bat
cd csharp
C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe ^
  /target:winexe /langversion:5 ^
  /reference:Microsoft.Web.WebView2.Core.dll ^
  /reference:Microsoft.Web.WebView2.WinForms.dll ^
  /reference:System.Drawing.dll ^
  /reference:System.Windows.Forms.dll ^
  /out:WeatherWidget.exe WeatherWidget.cs
launch.bat
```

### C++ Version (MSVC)

```bat
cd cplus
build.bat
launch.bat
```

### C++ Version (MinGW)

```bat
cd cplus
build-mingw.bat
launch.bat
```

### Python Version

```bat
cd python
pip install -r requirements.txt
launch.bat
```

Or directly:
```bat
python python\WeatherWidget.py
```

## How It Works

1. **Native Window**: Creates a borderless, rounded-corner window with a custom title bar
2. **WebView2 Host**: Embeds a Chromium-based web view to render the HTML/CSS/JS frontend
3. **HTTP Proxy**: A local server on a random port handles API requests:
   - `/api/forecast` -> Open-Meteo weather API
   - `/api/search` -> Open-Meteo geocoding API
   - `/api/ipgeo` -> ipinfo.io (primary IP geolocation)
   - `/api/ipgeo2` -> ip-api.com (fallback IP geolocation)
   - All other paths -> serves `weather-widget.html`
4. **Geolocation Chain**: Browser GPS (3s) -> ipinfo.io (5s) -> ip-api.com (5s) -> Beijing fallback

## API Credits

- Weather data: [Open-Meteo](https://open-meteo.com/) (free, no API key)
- IP geolocation: [ipinfo.io](https://ipinfo.io/) + [ip-api.com](http://ip-api.com/)

## License

MIT License. See [LICENSE](LICENSE).
