"""
WeatherWidget - Python Edition
A lightweight desktop weather widget using pywebview (WebView2) + local HTTP proxy.
"""

import os
import sys
import json
import threading
import socket
import time
import urllib.request
import urllib.error
import urllib.parse
import ssl
from http.server import HTTPServer, BaseHTTPRequestHandler
from pathlib import Path

import webview


SERVER_PORT = 0
HTML_PATH = ""
EXE_DIR = ""


HTML_FILE = "weather-widget.html"


def get_exe_dir():
    if getattr(sys, 'frozen', False):
        return os.path.dirname(sys.executable)
    return os.path.dirname(os.path.abspath(__file__))


def resolve_html_path(base_dir):
    """The frontend UI is shared by all implementations and lives at the
    project root (WeatherWidget/weather-widget.html)."""
    base_dir = os.path.normpath(base_dir)
    candidates = [
        os.path.join(base_dir, HTML_FILE),
        os.path.join(os.path.dirname(base_dir), HTML_FILE),
        os.path.join(os.getcwd(), HTML_FILE),
    ]
    for candidate in candidates:
        if os.path.isfile(candidate):
            return candidate
    return candidates[0]


def log(msg):
    try:
        log_path = os.path.join(EXE_DIR, "debug.log")
        with open(log_path, "a", encoding="utf-8") as f:
            f.write(f"{time.strftime('%H:%M:%S')} {msg}\n")
    except Exception:
        pass


def http_get(url, timeout=15):
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    req = urllib.request.Request(url, headers={"User-Agent": "WeatherWidget-Python/1.0"})
    try:
        with urllib.request.urlopen(req, timeout=timeout, context=ctx) as resp:
            return resp.read().decode("utf-8", errors="replace")
    except Exception as e:
        log(f"HTTP GET error ({url}): {e}")
        return json.dumps({"error": str(e)})


class ProxyHandler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        pass

    def _set_cors(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Cache-Control", "no-cache")

    def do_OPTIONS(self):
        self.send_response(200)
        self._set_cors()
        self.send_header("Access-Control-Allow-Methods", "GET, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "*")
        self.end_headers()

    def do_GET(self):
        path = self.path

        if path.startswith("/api/"):
            self._handle_api(path[4:])
        elif path == "/diag":
            msg = self.path.split("?msg=")[-1] if "?msg=" in self.path else ""
            msg = urllib.parse.unquote(msg)
            log(f"DIAG: {msg}")
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self._set_cors()
            self.end_headers()
            self.wfile.write(b"ok")
        else:
            self._serve_html()

    def _handle_api(self, api_path):
        query = ""
        if "?" in api_path:
            api_path, query = api_path.split("?", 1)
            query = "?" + query

        if api_path.startswith("/search"):
            url = "https://geocoding-api.open-meteo.com/v1" + api_path + query
        elif api_path == "/ipgeo":
            url = "https://ipinfo.io/json"
        elif api_path == "/ipgeo2":
            url = "http://ip-api.com/json/?fields=status,message,country,regionName,city,lat,lon,query"
        else:
            url = "https://api.open-meteo.com/v1" + api_path + query

        log(f"Proxy: {url}")
        body = http_get(url)

        self.send_response(200)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self._set_cors()
        self.end_headers()
        self.wfile.write(body.encode("utf-8"))

    def _serve_html(self):
        try:
            with open(HTML_PATH, "r", encoding="utf-8") as f:
                content = f.read()
        except FileNotFoundError:
            content = (
                "<html><body style='background:#fef9e7;color:#333;"
                "text-align:center;padding:40px;font-family:Microsoft YaHei'>"
                "<h2>weather-widget.html not found</h2></body></html>"
            )

        data = content.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self._set_cors()
        self.end_headers()
        self.wfile.write(data)


def find_free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def start_server(port):
    server = HTTPServer(("127.0.0.1", port), ProxyHandler)
    log(f"HTTP server started on port {port}")
    server.serve_forever()


class Api:
    def set_topmost(self, flag):
        if window:
            window.on_top = flag

    def minimize(self):
        if window:
            window.minimize()

    def close(self):
        if window:
            window.destroy()


window = None


def on_loaded():
    log("Window loaded")


def main():
    global window, HTML_PATH, EXE_DIR, SERVER_PORT

    EXE_DIR = get_exe_dir()
    HTML_PATH = resolve_html_path(EXE_DIR)
    log("=== WeatherWidget Python Start ===")
    log(f"ExeDir: {EXE_DIR}")
    log(f"HTML: {HTML_PATH} exists={os.path.exists(HTML_PATH)}")

    SERVER_PORT = find_free_port()
    log(f"Port allocated: {SERVER_PORT}")

    server_thread = threading.Thread(target=start_server, args=(SERVER_PORT,), daemon=True)
    server_thread.start()
    time.sleep(0.2)

    url = f"http://127.0.0.1:{SERVER_PORT}/weather.html"
    log(f"Navigating to: {url}")

    window = webview.create_window(
        "天气",
        url,
        width=440,
        height=860,
        min_size=(380, 700),
        resizable=True,
        text_select=False,
        js_api=Api(),
    )

    window.events.loaded += on_loaded

    webview.start(debug=False, gui="edgechromium")


if __name__ == "__main__":
    main()
