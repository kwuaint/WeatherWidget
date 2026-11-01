using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Net;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using Microsoft.Web.WebView2.WinForms;
using Microsoft.Web.WebView2.Core;

namespace WeatherWidget
{
    static class Program
    {
        static string logFile;

        static void Log(string msg)
        {
            try { File.AppendAllText(logFile, DateTime.Now.ToString("HH:mm:ss") + " " + msg + "\r\n"); } catch { }
        }

        // The frontend UI is shared by all implementations and lives at the project root:
        // WeatherWidget/weather-widget.html (next to this folder). If it is not present
        // next to the executable, look it up in the parent directory and then in the CWD.
        internal static string GetHtmlPath()
        {
            string fileName = "weather-widget.html";
            string baseDir = AppDomain.CurrentDomain.BaseDirectory.TrimEnd('\\');

            string[] candidates = new string[]
            {
                Path.Combine(baseDir, fileName),
                Path.Combine(Path.GetDirectoryName(baseDir), fileName),
                Path.Combine(Directory.GetCurrentDirectory(), fileName)
            };

            foreach (string candidate in candidates)
            {
                if (File.Exists(candidate)) return candidate;
            }
            return candidates[0];
        }

        [STAThread]
        static void Main()
        {
            string exeDir = AppDomain.CurrentDomain.BaseDirectory;
            logFile = Path.Combine(exeDir, "debug.log");
            File.WriteAllText(logFile, "=== WeatherWidget Start (WebView2) ===\r\n");
            Log("ExeDir: " + exeDir);
            Log("OS: " + Environment.OSVersion.ToString());
            Log("CLR: " + Environment.Version.ToString());

            try
            {
                ServicePointManager.SecurityProtocol = SecurityProtocolType.Tls12;
                Log("TLS 1.2 enabled");
            }
            catch (Exception ex) { Log("TLS error: " + ex.Message); }

            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);

            try
            {
                Application.Run(new WeatherForm());
            }
            catch (Exception ex)
            {
                Log("CRASH: " + ex.ToString());
            }
        }
    }

    public class WeatherForm : Form
    {
        private WebView2 webView;
        private Panel titleBar;
        private Button closeBtn;
        private Button minBtn;
        private Button topMostBtn;
        private bool isTopMost = false;
        private bool isDragging = false;
        private Point dragOffset;
        private HttpListener httpServer;
        private int serverPort;

        void Log(string msg)
        {
            try { File.AppendAllText(
                Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "debug.log"),
                DateTime.Now.ToString("HH:mm:ss") + " " + msg + "\r\n"); } catch { }
        }

        public WeatherForm()
        {
            Log("WeatherForm constructor start");
            InitializeComponent();
            Log("WeatherForm constructor end");
        }

        void InitializeComponent()
        {
            this.SuspendLayout();
            this.Text = "\u5929\u6c14";
            this.Size = new Size(440, 860);
            this.MinimumSize = new Size(380, 700);
            this.FormBorderStyle = FormBorderStyle.None;
            this.BackColor = Color.FromArgb(254, 249, 231);
            this.StartPosition = FormStartPosition.CenterScreen;
            this.ShowInTaskbar = true;
            this.Region = CreateRoundedRegion(this.Size, 20);

            titleBar = new Panel();
            titleBar.Dock = DockStyle.Top;
            titleBar.Height = 40;
            titleBar.BackColor = Color.FromArgb(254, 249, 231);
            titleBar.MouseDown += TitleBar_MouseDown;
            titleBar.MouseMove += TitleBar_MouseMove;
            titleBar.MouseUp += TitleBar_MouseUp;

            var titleLabel = new Label();
            titleLabel.Text = "  \u5929\u6c14";
            titleLabel.Font = new Font("Microsoft YaHei UI", 10f, FontStyle.Bold);
            titleLabel.ForeColor = Color.FromArgb(51, 51, 51);
            titleLabel.Dock = DockStyle.Left;
            titleLabel.Width = 100;
            titleLabel.MouseDown += TitleBar_MouseDown;
            titleLabel.MouseMove += TitleBar_MouseMove;
            titleLabel.MouseUp += TitleBar_MouseUp;

            topMostBtn = CreateTitleButton("\u25B3", 200);
            topMostBtn.Click += delegate { ToggleTopMost(); };
            minBtn = CreateTitleButton("\u2013", 260);
            minBtn.Click += delegate { this.WindowState = FormWindowState.Minimized; };
            closeBtn = CreateTitleButton("\u00D7", 320);
            closeBtn.Click += delegate { this.Close(); };

            titleBar.Controls.Add(closeBtn);
            titleBar.Controls.Add(minBtn);
            titleBar.Controls.Add(topMostBtn);
            titleBar.Controls.Add(titleLabel);

            webView = new WebView2();
            webView.Dock = DockStyle.Fill;
            webView.Visible = true;
            webView.CreationProperties = null;

            this.Controls.Add(webView);
            this.Controls.Add(titleBar);
            this.ResumeLayout(false);

            Log("Controls created, initializing WebView2...");
            InitWebView();
        }

        async void InitWebView()
        {
            try
            {
                string exeDir = Path.GetDirectoryName(Application.ExecutablePath);
                string userDataFolder = Path.Combine(exeDir, "wv2data");

                Log("Creating WebView2 environment...");
                Log("UserDataFolder: " + userDataFolder);

                var env = await CoreWebView2Environment.CreateAsync(null, userDataFolder, null);
                Log("Environment created successfully");

                await webView.EnsureCoreWebView2Async(env);
                Log("WebView2 core initialized");

                webView.CoreWebView2.Settings.IsStatusBarEnabled = false;
                webView.CoreWebView2.Settings.AreDefaultContextMenusEnabled = false;
                webView.CoreWebView2.Settings.IsSwipeNavigationEnabled = false;

                webView.CoreWebView2.PermissionRequested += (s, e) =>
                {
                    if (e.PermissionKind == CoreWebView2PermissionKind.Geolocation)
                    {
                        e.State = CoreWebView2PermissionState.Allow;
                        e.Handled = true;
                        Log("Geolocation permission auto-granted");
                    }
                };

                Log("Settings configured, starting local server...");
                StartLocalServer();
            }
            catch (Exception ex)
            {
                Log("InitWebView ERROR: " + ex.ToString());
                MessageBox.Show(
                    "WebView2 \u521d\u59cb\u5316\u5931\u8d25:\r\n" + ex.Message +
                    "\r\n\r\n\u8bf7\u786e\u4fdd\u5df2\u5b89\u88c5 Microsoft Edge WebView2 Runtime\u3002" +
                    "\r\n\u4e0b\u8f7d\u5730\u5740: https://developer.microsoft.com/en-us/microsoft-edge/webview2/",
                    "\u9519\u8bef",
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Error);
                this.Close();
            }
        }

        Button CreateTitleButton(string text, int x)
        {
            var btn = new Button();
            btn.Text = text;
            btn.Font = new Font("Segoe UI", 11f);
            btn.ForeColor = Color.FromArgb(80, 80, 80);
            btn.BackColor = Color.Transparent;
            btn.FlatStyle = FlatStyle.Flat;
            btn.FlatAppearance.BorderSize = 0;
            btn.Size = new Size(40, 40);
            btn.Location = new Point(x, 0);
            btn.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            btn.Cursor = Cursors.Hand;
            btn.TabStop = false;
            btn.MouseDown += TitleBar_MouseDown;
            btn.MouseMove += TitleBar_MouseMove;
            btn.MouseUp += TitleBar_MouseUp;
            return btn;
        }

        void ToggleTopMost()
        {
            isTopMost = !isTopMost;
            this.TopMost = isTopMost;
            topMostBtn.ForeColor = isTopMost ? Color.FromArgb(91, 155, 213) : Color.FromArgb(80, 80, 80);
        }

        void TitleBar_MouseDown(object sender, MouseEventArgs e)
        {
            if (e.Button == MouseButtons.Left) { isDragging = true; dragOffset = new Point(e.X, e.Y); }
        }
        void TitleBar_MouseMove(object sender, MouseEventArgs e)
        {
            if (isDragging) this.Location = new Point(this.Location.X + e.X - dragOffset.X, this.Location.Y + e.Y - dragOffset.Y);
        }
        void TitleBar_MouseUp(object sender, MouseEventArgs e) { isDragging = false; }

        void StartLocalServer()
        {
            Log("StartLocalServer begin");

            string exeDir = Path.GetDirectoryName(Application.ExecutablePath);
            Log("ExeDir: " + exeDir);

            string htmlPath = Program.GetHtmlPath();
            Log("HTML path: " + htmlPath + " exists=" + File.Exists(htmlPath));

            if (!File.Exists(htmlPath))
            {
                Log("HTML not found, showing error");
                webView.CoreWebView2.NavigateToString(
                    "<html><body style='background:#1a1a2e;color:white;font-family:Microsoft YaHei;text-align:center;padding:40px'>" +
                    "<h2>\u672a\u627e\u5230 weather-widget.html</h2>" +
                    "<p>\u8bf7\u5c06\u5176\u653e\u5728\u7a0b\u5e8f\u540c\u76ee\u5f55\u4e0b</p>" +
                    "<p style='color:#888;font-size:12px'>" + exeDir + "</p></body></html>");
                return;
            }

            try
            {
                var tcpListener = new System.Net.Sockets.TcpListener(IPAddress.Loopback, 0);
                tcpListener.Start();
                serverPort = ((System.Net.IPEndPoint)tcpListener.LocalEndpoint).Port;
                tcpListener.Stop();
                Log("Port allocated: " + serverPort);
            }
            catch (Exception ex)
            {
                Log("Port allocation failed: " + ex.Message);
                return;
            }

            try
            {
                httpServer = new HttpListener();
                httpServer.Prefixes.Add("http://127.0.0.1:" + serverPort + "/");
                httpServer.Start();
                Log("HTTP server started on port " + serverPort);
            }
            catch (Exception ex)
            {
                Log("HTTP server start failed: " + ex.Message);
                return;
            }

            httpServer.BeginGetContext(new AsyncCallback(ServerLoop), httpServer);
            Log("HTTP server listening");

            string url = "http://127.0.0.1:" + serverPort + "/weather.html";
            Log("Navigating to: " + url);
            webView.CoreWebView2.Navigate(url);
            Log("StartLocalServer end");
        }

        void ServerLoop(IAsyncResult result)
        {
            if (httpServer == null) return;
            try
            {
                HttpListenerContext context = httpServer.EndGetContext(result);
                HttpListenerRequest request = context.Request;
                HttpListenerResponse response = context.Response;

                response.Headers.Add("Access-Control-Allow-Origin", "*");
                response.Headers.Add("Cache-Control", "no-cache");

                if (request.Url.AbsolutePath.StartsWith("/api/"))
                {
                    string apiPath = request.Url.AbsolutePath.Substring(4);
                    string apiQuery = request.Url.Query;
                    string apiUrl;
                    if (apiPath.StartsWith("/search"))
                        apiUrl = "https://geocoding-api.open-meteo.com/v1" + apiPath + apiQuery;
                    else if (apiPath.StartsWith("/ipgeo"))
                        apiUrl = "https://ipinfo.io/json";
                    else if (apiPath.StartsWith("/ipgeo2"))
                        apiUrl = "http://ip-api.com/json/?fields=status,message,country,regionName,city,lat,lon,query";
                    else
                        apiUrl = "https://api.open-meteo.com/v1" + apiPath + apiQuery;
                    Log("Proxy: " + apiUrl);

                    try
                    {
                        HttpWebRequest apiReq = (HttpWebRequest)WebRequest.Create(apiUrl);
                        apiReq.Method = "GET";
                        apiReq.Timeout = 15000;
                        using (HttpWebResponse apiResp = (HttpWebResponse)apiReq.GetResponse())
                        {
                            response.StatusCode = (int)apiResp.StatusCode;
                            response.ContentType = "application/json; charset=utf-8";
                            using (Stream apiStream = apiResp.GetResponseStream())
                            {
                                apiStream.CopyTo(response.OutputStream);
                            }
                            Log("Proxy OK, status=" + (int)apiResp.StatusCode);
                        }
                    }
                    catch (Exception ex)
                    {
                        Log("Proxy error: " + ex.Message);
                        response.StatusCode = 502;
                        byte[] errBuf = Encoding.UTF8.GetBytes("{\"error\":\"" + ex.Message.Replace("\"", "'") + "\"}");
                        response.OutputStream.Write(errBuf, 0, errBuf.Length);
                    }
                }
                else if (request.Url.AbsolutePath == "/diag")
                {
                    string msg = request.Url.Query.Replace("?msg=", "");
                    try { msg = Uri.UnescapeDataString(msg); } catch { }
                    Log("DIAG: " + msg);
                    response.StatusCode = 200;
                    response.ContentType = "text/plain";
                    byte[] okBuf = Encoding.UTF8.GetBytes("ok");
                    response.OutputStream.Write(okBuf, 0, okBuf.Length);
                }
                else
                {
                    response.StatusCode = 200;
                    response.ContentType = "text/html; charset=utf-8";

                    string htmlPath = Program.GetHtmlPath();
                    string htmlContent = File.ReadAllText(htmlPath, Encoding.UTF8);

                    byte[] buffer = Encoding.UTF8.GetBytes(htmlContent);
                    response.ContentLength64 = buffer.Length;
                    response.OutputStream.Write(buffer, 0, buffer.Length);
                    Log("Served HTML, size=" + buffer.Length);
                }

                response.OutputStream.Close();
                httpServer.BeginGetContext(new AsyncCallback(ServerLoop), httpServer);
            }
            catch (Exception ex)
            {
                Log("ServerLoop error: " + ex.Message);
            }
        }

        Region CreateRoundedRegion(Size size, int radius)
        {
            using (var path = new GraphicsPath())
            {
                int d = radius * 2;
                path.AddArc(0, 0, d, d, 180, 90);
                path.AddArc(size.Width - d, 0, d, d, 270, 90);
                path.AddArc(size.Width - d, size.Height - d, d, d, 0, 90);
                path.AddArc(0, size.Height - d, d, d, 90, 90);
                path.CloseFigure();
                return new Region(path);
            }
        }

        protected override void OnResize(EventArgs e)
        {
            base.OnResize(e);
            this.Region = CreateRoundedRegion(this.Size, 20);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            base.OnPaint(e);
            using (var pen = new Pen(Color.FromArgb(51, 51, 51), 2))
            {
                e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
                using (var path = new GraphicsPath())
                {
                    int r = 20;
                    int d = r * 2;
                    path.AddArc(0, 0, d, d, 180, 90);
                    path.AddArc(this.Width - d, 0, d, d, 270, 90);
                    path.AddArc(this.Width - d, this.Height - d, d, d, 0, 90);
                    path.AddArc(0, this.Height - d, d, d, 90, 90);
                    path.CloseFigure();
                    e.Graphics.DrawPath(pen, path);
                }
            }
        }

        protected override void OnFormClosing(FormClosingEventArgs e)
        {
            if (httpServer != null)
            {
                try { httpServer.Stop(); } catch { }
                httpServer.Close();
            }
            base.OnFormClosing(e);
        }
    }
}
