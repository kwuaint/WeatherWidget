# WeatherWidget

A lightweight weather widget desktop application for Windows, built with **C# WinForms + WebView2**. Features a charming **pencil-sketch UI style** with childlike aesthetics.

![Platform](https://img.shields.io/badge/platform-Windows%2010/11-blue)
![.NET](https://img.shields.io/badge/.NET%20Framework-4.6.2+-purple)
![WebView2](https://img.shields.io/badge/WebView2-Chromium-green)
![License](https://img.shields.io/badge/license-MIT-orange)

## Features

- **Current weather** — temperature, feels-like, humidity, UV index, wind speed/direction, pressure, visibility
- **24-hour forecast** — hourly temperature and weather icons
- **7-day forecast** — daily high/low with temperature range bars
- **Sunrise & sunset** — animated sun arc showing day progress
- **City search** — search by Chinese name or pinyin initials (e.g. type "bj" for Beijing)
- **Auto geolocation** — browser GPS → ipinfo.io → ip-api.com → default city fallback chain
- **City flowers** — displays the official city flower badge when switching cities (55+ Chinese cities)
- **Pencil-sketch UI** — hand-drawn style with paper texture, doodle icons, and playful typography
- **Auto-refresh** — weather data refreshes every 10 minutes
- **Always on top** — toggle to keep the widget above other windows

## Prerequisites

- **Windows 10/11**
- **.NET Framework 4.6.2** or later (pre-installed on Windows 10+)
- **Microsoft Edge WebView2 Runtime** (pre-installed on most Windows 10/11 systems)
  - If not installed, download from: https://developer.microsoft.com/en-us/microsoft-edge/webview2/

## Build from Source

### 1. Clone the repository

```bash
git clone https://github.com/kwuaint/WeatherWidget.git
cd WeatherWidget
```

### 2. Compile

Use the .NET Framework C# compiler:

```bash
"C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe" ^
  /target:winexe ^
  /langversion:5 ^
  /reference:Microsoft.Web.WebView2.Core.dll ^
  /reference:Microsoft.Web.WebView2.WinForms.dll ^
  /reference:System.Drawing.dll ^
  /reference:System.Windows.Forms.dll ^
  /out:WeatherWidget.exe ^
  WeatherWidget.cs
```

### 3. Run

```bash
WeatherWidget.exe
```

Or double-click the included `launch.bat` launcher script.

## Project Structure

```
WeatherWidget/
├── WeatherWidget.cs              # Main C# WinForms app (WebView2 host + HTTP proxy)
├── weather-widget.html           # Frontend UI (HTML/CSS/JS, sketch-style)
├── Microsoft.Web.WebView2.Core.dll
├── Microsoft.Web.WebView2.WinForms.dll
├── WebView2Loader.dll
├── launch.bat                    # Windows launcher script
├── README.md
├── LICENSE
└── .gitignore
```

## Architecture

- **C# WinForms** hosts a **WebView2** (Chromium) control to render the UI
- A local **HttpListener** server serves the HTML file and proxies API requests to:
  - `api.open-meteo.com` — weather data
  - `geocoding-api.open-meteo.com` — city search
  - `ipinfo.io` / `ip-api.com` — IP-based geolocation (with fallback)
- All API calls go through the local proxy to avoid CORS issues

## Data Source

Weather data is provided by [Open-Meteo](https://open-meteo.com/) (free, no API key required).

## License

MIT License. See [LICENSE](LICENSE) for details.
