// Gui.cs - NanoBootloader.exe, the Windows front end. Same engine as nanobl.exe (Image/Uploader).
using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.IO.Ports;
using System.Text;
using System.Threading;
using System.Windows.Forms;

namespace NanoBootloader
{
    public class MainForm : Form
    {
        ComboBox cbPort, cbFormat, cbTermBaud;
        TextBox tbFile, tbBase, tbTermOut, tbTermIn;
        Label lblImage;
        CheckBox chkRun, chkSoft, chkWait, chkWatch, chkTermAfter;
        NumericUpDown nudWait;
        Button btnProgram, btnVerify, btnRun, btnInfo, btnCancel, btnBrowse, btnRefresh, btnExportBin, btnFactory, btnTermConnect;
        ProgressBar progress;
        RichTextBox log;
        StatusStrip status; ToolStripStatusLabel statusText;
        TabControl tabs;

        AppImage image;
        volatile Uploader active;
        Thread worker;
        FileSystemWatcher watcher;
        System.Windows.Forms.Timer watchDebounce;
        SerialPort term;
        readonly Dictionary<string, string> settings = new Dictionary<string, string>();
        static readonly string SettingsPath = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                                                           "NanoBootloader", "settings.txt");

        [STAThread]
        public static void Main(string[] args)
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            MainForm f = new MainForm();
            if (args.Length > 0 && File.Exists(args[0])) f.tbFile.Text = Path.GetFullPath(args[0]);   // "Open with" / drag onto exe
            Application.Run(f);
        }

        public MainForm()
        {
            Text = "dsPIC33CK Nano Bootloader";
            Font = new Font("Segoe UI", 9F);
            ClientSize = new Size(760, 640);
            MinimumSize = new Size(640, 520);
            AllowDrop = true;
            DragEnter += delegate (object s, DragEventArgs e) { if (e.Data.GetDataPresent(DataFormats.FileDrop)) e.Effect = DragDropEffects.Copy; };
            DragDrop += delegate (object s, DragEventArgs e) { string[] f = (string[])e.Data.GetData(DataFormats.FileDrop); if (f.Length > 0) SetFile(f[0]); };

            tabs = new TabControl(); tabs.Dock = DockStyle.Fill;
            TabPage pProg = new TabPage("Program"), pTerm = new TabPage("Terminal");
            tabs.TabPages.Add(pProg); tabs.TabPages.Add(pTerm);
            status = new StatusStrip(); statusText = new ToolStripStatusLabel("Ready"); status.Items.Add(statusText);
            Controls.Add(tabs); Controls.Add(status);

            BuildProgramTab(pProg);
            BuildTerminalTab(pTerm);

            watchDebounce = new System.Windows.Forms.Timer(); watchDebounce.Interval = 1500;
            watchDebounce.Tick += delegate { watchDebounce.Stop(); OnWatchedFileChanged(); };

            LoadSettings();
            RefreshPorts();
            Shown += delegate { if (tbFile.Text.Length > 0) LoadImage(false); };
            FormClosing += delegate { SaveSettings(); CloseTerminal(); if (active != null) active.CancelRequested = true; };
        }

        // ------------------------------------------------------------------ layout

        static Label L(string t) { Label l = new Label(); l.Text = t; l.AutoSize = true; l.Anchor = AnchorStyles.Left; l.Margin = new Padding(3, 7, 3, 3); return l; }
        static Button B(string t, int w) { Button b = new Button(); b.Text = t; b.Width = w; b.Height = 27; return b; }

        void BuildProgramTab(TabPage page)
        {
            TableLayoutPanel g = new TableLayoutPanel();
            g.Dock = DockStyle.Fill; g.Padding = new Padding(8); g.ColumnCount = 4;
            g.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            g.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            g.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            g.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            page.Controls.Add(g);

            // row 0: port
            cbPort = new ComboBox(); cbPort.DropDownStyle = ComboBoxStyle.DropDownList; cbPort.Dock = DockStyle.Fill;
            btnRefresh = B("Refresh", 80); btnRefresh.Click += delegate { RefreshPorts(); };
            btnInfo = B("Board info", 90); btnInfo.Click += delegate { StartJob("info"); };
            g.Controls.Add(L("Serial port (J5)"), 0, 0); g.Controls.Add(cbPort, 1, 0); g.Controls.Add(btnRefresh, 2, 0); g.Controls.Add(btnInfo, 3, 0);

            // row 1: file
            tbFile = new TextBox(); tbFile.Dock = DockStyle.Fill;
            tbFile.Leave += delegate { LoadImage(false); };
            tbFile.KeyDown += delegate (object s, KeyEventArgs e) { if (e.KeyCode == Keys.Enter) { LoadImage(false); e.SuppressKeyPress = true; } };
            btnBrowse = B("Browse...", 80); btnBrowse.Click += delegate { Browse(); };
            Button btnReload = B("Reload", 90); btnReload.Click += delegate { LoadImage(false); };
            g.Controls.Add(L("Application file"), 0, 1); g.Controls.Add(tbFile, 1, 1); g.Controls.Add(btnBrowse, 2, 1); g.Controls.Add(btnReload, 3, 1);

            // row 2: format
            FlowLayoutPanel fmt = new FlowLayoutPanel(); fmt.AutoSize = true; fmt.Dock = DockStyle.Fill; fmt.Margin = new Padding(0);
            cbFormat = new ComboBox(); cbFormat.DropDownStyle = ComboBoxStyle.DropDownList; cbFormat.Width = 230;
            cbFormat.Items.AddRange(new object[] { "Auto (by extension)", "Intel HEX", "BIN - 3 bytes/word (packed)", "BIN - 4 bytes/word (phantom byte)" });
            cbFormat.SelectedIndex = 0; cbFormat.SelectedIndexChanged += delegate { LoadImage(false); };
            tbBase = new TextBox(); tbBase.Width = 80; tbBase.Text = "0x001800"; tbBase.Leave += delegate { LoadImage(false); };
            fmt.Controls.Add(cbFormat); fmt.Controls.Add(L("   BIN base address")); fmt.Controls.Add(tbBase);
            g.Controls.Add(L("Format"), 0, 2); g.Controls.Add(fmt, 1, 2); g.SetColumnSpan(fmt, 3);

            // row 3: image info
            lblImage = new Label(); lblImage.AutoSize = false; lblImage.Dock = DockStyle.Fill; lblImage.Height = 64;
            lblImage.BorderStyle = BorderStyle.FixedSingle; lblImage.Padding = new Padding(4); lblImage.Text = "No file loaded. Drop a .hex / .bin / .elf here.";
            g.Controls.Add(lblImage, 0, 3); g.SetColumnSpan(lblImage, 4);
            g.RowStyles.Add(new RowStyle(SizeType.AutoSize)); g.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            g.RowStyles.Add(new RowStyle(SizeType.AutoSize)); g.RowStyles.Add(new RowStyle(SizeType.Absolute, 74));

            // row 4: options
            FlowLayoutPanel opt = new FlowLayoutPanel(); opt.AutoSize = true; opt.Dock = DockStyle.Fill;
            chkRun = new CheckBox(); chkRun.Text = "Run application after programming"; chkRun.AutoSize = true; chkRun.Checked = true;
            chkSoft = new CheckBox(); chkSoft.Text = "Ask the running app to reset (soft entry)"; chkSoft.AutoSize = true; chkSoft.Checked = true;
            chkWait = new CheckBox(); chkWait.Text = "Wait for manual entry (USER+RESET) up to"; chkWait.AutoSize = true; chkWait.Checked = true;
            nudWait = new NumericUpDown(); nudWait.Minimum = 1; nudWait.Maximum = 120; nudWait.Value = 20; nudWait.Width = 50;
            chkWatch = new CheckBox(); chkWatch.Text = "Auto-program when the file changes (after each MPLAB X / VS Code build)"; chkWatch.AutoSize = true;
            chkWatch.CheckedChanged += delegate { UpdateWatcher(); };
            opt.Controls.Add(chkRun); opt.Controls.Add(chkSoft); opt.SetFlowBreak(chkSoft, true);
            opt.Controls.Add(chkWait); opt.Controls.Add(nudWait); opt.Controls.Add(L("s")); opt.SetFlowBreak(opt.Controls[opt.Controls.Count - 1], true);
            opt.Controls.Add(chkWatch);
            g.Controls.Add(opt, 0, 4); g.SetColumnSpan(opt, 4); g.RowStyles.Add(new RowStyle(SizeType.AutoSize));

            // row 5: actions
            FlowLayoutPanel act = new FlowLayoutPanel(); act.AutoSize = true; act.Dock = DockStyle.Fill;
            btnProgram = B("Program", 110); btnProgram.Font = new Font(Font, FontStyle.Bold); btnProgram.Click += delegate { StartJob("program"); };
            btnVerify = B("Verify", 80); btnVerify.Click += delegate { StartJob("verify"); };
            btnRun = B("Run app", 80); btnRun.Click += delegate { StartJob("run"); };
            btnCancel = B("Cancel", 80); btnCancel.Enabled = false; btnCancel.Click += delegate { if (active != null) active.CancelRequested = true; };
            btnExportBin = B("Export .bin...", 100); btnExportBin.Click += delegate { ExportBin(); };
            btnFactory = B("Factory HEX...", 105); btnFactory.Click += delegate { MakeFactory(); };
            act.Controls.AddRange(new Control[] { btnProgram, btnVerify, btnRun, btnCancel, btnExportBin, btnFactory });
            g.Controls.Add(act, 0, 5); g.SetColumnSpan(act, 4); g.RowStyles.Add(new RowStyle(SizeType.AutoSize));

            progress = new ProgressBar(); progress.Dock = DockStyle.Fill; progress.Height = 18;
            g.Controls.Add(progress, 0, 6); g.SetColumnSpan(progress, 4); g.RowStyles.Add(new RowStyle(SizeType.Absolute, 24));

            log = new RichTextBox(); log.Dock = DockStyle.Fill; log.ReadOnly = true; log.Font = new Font("Consolas", 9F);
            log.BackColor = Color.White; log.DetectUrls = false;
            g.Controls.Add(log, 0, 7); g.SetColumnSpan(log, 4); g.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        }

        void BuildTerminalTab(TabPage page)
        {
            TableLayoutPanel g = new TableLayoutPanel(); g.Dock = DockStyle.Fill; g.Padding = new Padding(8); g.ColumnCount = 1;
            page.Controls.Add(g);
            FlowLayoutPanel top = new FlowLayoutPanel(); top.AutoSize = true; top.Dock = DockStyle.Fill;
            cbTermBaud = new ComboBox(); cbTermBaud.Width = 90;
            cbTermBaud.Items.AddRange(new object[] { "9600", "19200", "38400", "57600", "115200", "230400", "250000", "460800" });
            cbTermBaud.Text = "115200";
            btnTermConnect = B("Connect", 90); btnTermConnect.Click += delegate { if (term == null) OpenTerminal(); else CloseTerminal(); };
            Button clr = B("Clear", 70); clr.Click += delegate { tbTermOut.Clear(); };
            chkTermAfter = new CheckBox(); chkTermAfter.Text = "Open the terminal after the app starts"; chkTermAfter.AutoSize = true; chkTermAfter.Checked = true;
            top.Controls.Add(L("Baud")); top.Controls.Add(cbTermBaud); top.Controls.Add(btnTermConnect); top.Controls.Add(clr); top.Controls.Add(chkTermAfter);
            tbTermOut = new TextBox(); tbTermOut.Multiline = true; tbTermOut.ReadOnly = true; tbTermOut.ScrollBars = ScrollBars.Both;
            tbTermOut.WordWrap = false; tbTermOut.Dock = DockStyle.Fill; tbTermOut.Font = new Font("Consolas", 9F); tbTermOut.BackColor = Color.White;
            FlowLayoutPanel bottom = new FlowLayoutPanel(); bottom.AutoSize = true; bottom.Dock = DockStyle.Fill;
            tbTermIn = new TextBox(); tbTermIn.Width = 520;
            Button send = B("Send", 70);
            CheckBox crlf = new CheckBox(); crlf.Text = "append CR LF"; crlf.Checked = true; crlf.AutoSize = true;
            EventHandler doSend = delegate
            {
                if (term == null) { Status("terminal is not connected"); return; }
                try { term.Write(tbTermIn.Text + (crlf.Checked ? "\r\n" : "")); tbTermIn.SelectAll(); }
                catch (Exception ex) { Status("send failed: " + ex.Message); }
            };
            send.Click += doSend;
            tbTermIn.KeyDown += delegate (object s, KeyEventArgs e) { if (e.KeyCode == Keys.Enter) { doSend(s, e); e.SuppressKeyPress = true; } };
            bottom.Controls.Add(tbTermIn); bottom.Controls.Add(send); bottom.Controls.Add(crlf);
            g.Controls.Add(top, 0, 0); g.Controls.Add(tbTermOut, 0, 1); g.Controls.Add(bottom, 0, 2);
            g.RowStyles.Add(new RowStyle(SizeType.AutoSize)); g.RowStyles.Add(new RowStyle(SizeType.Percent, 100)); g.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        }

        // ------------------------------------------------------------------ helpers

        void Status(string s) { statusText.Text = s; }

        void Log(string s, Color c)
        {
            if (InvokeRequired) { BeginInvoke((Action)delegate { Log(s, c); }); return; }
            log.SelectionStart = log.TextLength; log.SelectionLength = 0; log.SelectionColor = c;
            log.AppendText(DateTime.Now.ToString("HH:mm:ss") + "  " + s + "\n");
            log.SelectionColor = log.ForeColor; log.ScrollToCaret();
        }
        void Info(string s) { Log(s, Color.Black); }
        void Warn(string s) { Log("warning: " + s, Color.DarkOrange); }
        void Error(string s) { Log("error: " + s, Color.Red); }

        string SelectedPort()
        {
            string s = cbPort.SelectedItem as string;
            if (s == null) return null;
            int i = s.IndexOf(' ');
            return i > 0 ? s.Substring(0, i) : s;
        }

        void RefreshPorts()
        {
            string keep = SelectedPort();
            string wanted; settings.TryGetValue("port", out wanted);
            cbPort.Items.Clear();
            foreach (KeyValuePair<string, string> p in PortList.Get())
                cbPort.Items.Add(p.Value.Length > 0 ? p.Key + "  -  " + p.Value : p.Key);
            for (int i = 0; i < cbPort.Items.Count; i++)
            {
                string n = ((string)cbPort.Items[i]).Split(' ')[0];
                if (n == (keep ?? wanted)) { cbPort.SelectedIndex = i; break; }
            }
            if (cbPort.SelectedIndex < 0)                     // first run: prefer a USB-serial adapter over Bluetooth / modem ports
                for (int i = 0; i < cbPort.Items.Count; i++)
                {
                    string d = ((string)cbPort.Items[i]).ToUpperInvariant();
                    if (d.Contains("USB") || d.Contains("FTDI") || d.Contains("CH340") || d.Contains("CP210") || d.Contains("PL2303") || d.Contains("UART"))
                    { cbPort.SelectedIndex = i; break; }
                }
            if (cbPort.SelectedIndex < 0 && cbPort.Items.Count > 0) cbPort.SelectedIndex = cbPort.Items.Count - 1;
            Status(cbPort.Items.Count + " serial port(s)");
        }

        void Browse()
        {
            using (OpenFileDialog d = new OpenFileDialog())
            {
                d.Filter = "Application images (*.hex;*.bin;*.elf)|*.hex;*.bin;*.elf|Intel HEX (*.hex)|*.hex|Binary (*.bin)|*.bin|ELF (*.elf)|*.elf|All files|*.*";
                if (tbFile.Text.Length > 0) try { d.InitialDirectory = Path.GetDirectoryName(tbFile.Text); } catch (Exception) { }
                if (d.ShowDialog(this) == DialogResult.OK) SetFile(d.FileName);
            }
        }

        void SetFile(string f) { tbFile.Text = f; LoadImage(false); }

        InputFormat SelectedFormat()
        {
            switch (cbFormat.SelectedIndex) { case 1: return InputFormat.Hex; case 2: return InputFormat.BinPacked; case 3: return InputFormat.BinWord4; default: return InputFormat.Auto; }
        }

        bool LoadImage(bool quiet)
        {
            string f = tbFile.Text.Trim();
            image = null;
            if (f.Length == 0) { lblImage.Text = "No file loaded."; return false; }
            try
            {
                uint bbase = Cli.ParseAddr(tbBase.Text);
                image = AppImage.Load(f, SelectedFormat(), bbase);
                StringBuilder sb = new StringBuilder();
                sb.AppendLine(image.Summary());
                foreach (string n in image.Notes) sb.AppendLine(n);
                foreach (string w in image.Warnings) sb.AppendLine("WARNING: " + w);
                lblImage.Text = sb.ToString().TrimEnd();
                lblImage.ForeColor = image.Warnings.Count > 0 ? Color.DarkOrange : Color.DarkGreen;
                if (!quiet)
                {
                    Info("loaded " + image.Summary());
                    foreach (string w in image.Warnings) Warn(w);
                }
                UpdateWatcher();
                return true;
            }
            catch (Exception e)
            {
                lblImage.Text = "Cannot use this file:\n" + e.Message;
                lblImage.ForeColor = Color.Red;
                if (!quiet) Error(e.Message);
                return false;
            }
        }

        UploadOptions Options()
        {
            UploadOptions o = new UploadOptions();
            o.Run = chkRun.Checked;
            o.SoftEntry = chkSoft.Checked;
            o.ManualWaitSeconds = chkWait.Checked ? (int)nudWait.Value : 0;
            return o;
        }

        void SetBusy(bool busy)
        {
            foreach (Control c in new Control[] { btnProgram, btnVerify, btnRun, btnInfo, btnBrowse, btnRefresh, cbPort, cbFormat, btnExportBin, btnFactory })
                c.Enabled = !busy;
            btnCancel.Enabled = busy;
            UseWaitCursor = false;
        }

        // ------------------------------------------------------------------ jobs

        void StartJob(string job)
        {
            if (worker != null && worker.IsAlive) return;
            string port = SelectedPort();
            if (port == null) { Error("choose the serial port of the USB-serial adapter on J5"); return; }
            if (job == "program" || job == "verify") { if (!LoadImage(true)) { Error(lblImage.Text); return; } }
            bool reopen = term != null && term.PortName == port;
            CloseTerminal();                                   // the bootloader needs the port
            UploadOptions o = Options();
            AppImage img = image;
            SetBusy(true);
            progress.Value = 0;
            Status(job + " on " + port + "...");
            Info("---- " + job + " on " + port + (img != null && (job == "program" || job == "verify") ? " : " + Path.GetFileName(img.SourcePath) : ""));

            worker = new Thread(delegate ()
            {
                bool ok = false, ran = false;
                try
                {
                    using (SerialTransport t = new SerialTransport(port))
                    {
                        Uploader up = new Uploader(t);
                        active = up;
                        up.Log = Info;
                        up.Detail = delegate (string s) { Log(s, Color.Gray); };
                        up.Progress = delegate (int d, int n) { BeginInvoke((Action)delegate { progress.Maximum = n; progress.Value = d; }); };
                        if (job == "program") { up.Program(img, o); ran = o.Run; ok = true; }
                        else if (job == "verify") { ok = up.Verify(img, o); ran = o.Run; }
                        else if (job == "run") { up.Enter(o); up.Run(); ran = true; ok = true; }
                        else if (job == "info") { up.Info(o); ok = true; }
                    }
                }
                catch (BlException e) { Error(e.Message); }
                catch (Exception e) { Error(e.GetType().Name + ": " + e.Message); }
                finally { active = null; }
                BeginInvoke((Action)delegate
                {
                    SetBusy(false);
                    Status(ok ? job + " finished" : job + " FAILED");
                    if (ok && job == "program") Log("done", Color.DarkGreen);
                    if ((ran && chkTermAfter.Checked) || reopen)
                    {
                        cbTermBaud.Text = cbTermBaud.Text.Length > 0 ? cbTermBaud.Text : "115200";
                        OpenTerminal();
                        if (ran && chkTermAfter.Checked) tabs.SelectedIndex = 1;
                    }
                });
            });
            worker.IsBackground = true;
            worker.Start();
        }

        void ExportBin()
        {
            if (!LoadImage(true)) { Error("load a valid application file first"); return; }
            using (SaveFileDialog d = new SaveFileDialog())
            {
                d.Filter = "Packed binary, 3 bytes/word (*.bin)|*.bin|Binary, 4 bytes/word (*.bin)|*.bin";
                d.FileName = Path.GetFileNameWithoutExtension(image.SourcePath) + ".bin";
                if (d.ShowDialog(this) != DialogResult.OK) return;
                try { image.WriteBin(d.FileName, d.FilterIndex == 2); Info("wrote " + d.FileName + " (first word = 0x001800)"); }
                catch (Exception e) { Error(e.Message); }
            }
        }

        void MakeFactory()
        {
            if (!LoadImage(true)) { Error("load a valid application file first"); return; }
            string bl;
            settings.TryGetValue("bootloaderHex", out bl);
            if (bl == null || !File.Exists(bl))
            {
                string guess = Path.Combine(Path.GetDirectoryName(Application.ExecutablePath), "bootloader-dspic33ck-nano-115200-v1.hex");
                bl = File.Exists(guess) ? guess : null;
            }
            using (OpenFileDialog o = new OpenFileDialog())
            {
                o.Title = "Bootloader HEX (bootloader-dspic33ck-nano-115200-v1.hex)";
                o.Filter = "Intel HEX (*.hex)|*.hex";
                if (bl != null) { o.InitialDirectory = Path.GetDirectoryName(bl); o.FileName = Path.GetFileName(bl); }
                if (o.ShowDialog(this) != DialogResult.OK) return;
                bl = o.FileName;
            }
            settings["bootloaderHex"] = bl;
            using (SaveFileDialog d = new SaveFileDialog())
            {
                d.Filter = "Intel HEX (*.hex)|*.hex";
                d.FileName = Path.GetFileNameWithoutExtension(image.SourcePath) + "_factory.hex";
                if (d.ShowDialog(this) != DialogResult.OK) return;
                try
                {
                    image.WriteCombinedHex(bl, d.FileName);
                    Info("wrote " + d.FileName + " = bootloader + application + signature. Program it with a PICkit/ICD (MPLAB IPE); " +
                         "the board then boots the application and stays updatable over serial.");
                }
                catch (Exception e) { Error(e.Message); }
            }
        }

        // ------------------------------------------------------------------ auto-program on rebuild

        void UpdateWatcher()
        {
            if (watcher != null) { watcher.EnableRaisingEvents = false; watcher.Dispose(); watcher = null; }
            if (!chkWatch.Checked || image == null) return;
            string f = image.SourcePath;
            watcher = new FileSystemWatcher(Path.GetDirectoryName(f), Path.GetFileName(f));
            watcher.NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.Size | NotifyFilters.FileName;
            FileSystemEventHandler h = delegate { BeginInvoke((Action)delegate { watchDebounce.Stop(); watchDebounce.Start(); }); };
            watcher.Changed += h; watcher.Created += h;
            watcher.Renamed += delegate { BeginInvoke((Action)delegate { watchDebounce.Stop(); watchDebounce.Start(); }); };
            watcher.EnableRaisingEvents = true;
            Status("watching " + Path.GetFileName(f) + " - a rebuild programs the board");
        }

        void OnWatchedFileChanged()
        {
            if (worker != null && worker.IsAlive) { watchDebounce.Start(); return; }
            Info("file changed - reprogramming");
            StartJob("program");
        }

        // ------------------------------------------------------------------ terminal

        void OpenTerminal()
        {
            CloseTerminal();
            string port = SelectedPort();
            if (port == null) return;
            int baud;
            if (!int.TryParse(cbTermBaud.Text, out baud)) { Status("bad baud rate"); return; }
            try
            {
                term = new SerialPort(port, baud, Parity.None, 8, StopBits.One);
                term.Encoding = Encoding.GetEncoding(28591);           // bytes 0..255 as-is
                term.DataReceived += delegate
                {
                    SerialPort p = term;
                    if (p == null) return;
                    string s;
                    try { s = p.ReadExisting(); } catch (Exception) { return; }
                    BeginInvoke((Action)delegate
                    {
                        if (tbTermOut.TextLength > 200000) tbTermOut.Text = tbTermOut.Text.Substring(100000);
                        tbTermOut.AppendText(s.Replace("\r\n", "\n").Replace("\r", "\n").Replace("\n", "\r\n"));
                    });
                };
                term.Open();
                btnTermConnect.Text = "Disconnect";
                Status("terminal on " + port + " at " + baud);
            }
            catch (Exception e) { term = null; Status("terminal: " + e.Message); }
        }

        void CloseTerminal()
        {
            SerialPort p = term;
            term = null;
            if (p != null) { try { p.Close(); } catch (Exception) { } p.Dispose(); }
            if (btnTermConnect != null) btnTermConnect.Text = "Connect";
        }

        // ------------------------------------------------------------------ settings

        void LoadSettings()
        {
            try
            {
                if (File.Exists(SettingsPath))
                    foreach (string line in File.ReadAllLines(SettingsPath))
                    {
                        int i = line.IndexOf('=');
                        if (i > 0) settings[line.Substring(0, i)] = line.Substring(i + 1);
                    }
            }
            catch (Exception) { }
            string v;
            if (settings.TryGetValue("file", out v) && tbFile.Text.Length == 0) tbFile.Text = v;
            if (settings.TryGetValue("format", out v)) { int n; if (int.TryParse(v, out n) && n >= 0 && n < 4) cbFormat.SelectedIndex = n; }
            if (settings.TryGetValue("base", out v)) tbBase.Text = v;
            if (settings.TryGetValue("run", out v)) chkRun.Checked = v == "1";
            if (settings.TryGetValue("soft", out v)) chkSoft.Checked = v == "1";
            if (settings.TryGetValue("wait", out v)) chkWait.Checked = v == "1";
            if (settings.TryGetValue("waitSec", out v)) { int n; if (int.TryParse(v, out n) && n >= 1 && n <= 120) nudWait.Value = n; }
            if (settings.TryGetValue("termBaud", out v)) cbTermBaud.Text = v;
            if (settings.TryGetValue("termAfter", out v)) chkTermAfter.Checked = v == "1";
        }

        void SaveSettings()
        {
            settings["file"] = tbFile.Text; settings["format"] = cbFormat.SelectedIndex.ToString(); settings["base"] = tbBase.Text;
            settings["run"] = chkRun.Checked ? "1" : "0"; settings["soft"] = chkSoft.Checked ? "1" : "0";
            settings["wait"] = chkWait.Checked ? "1" : "0"; settings["waitSec"] = nudWait.Value.ToString();
            settings["termBaud"] = cbTermBaud.Text; settings["termAfter"] = chkTermAfter.Checked ? "1" : "0";
            string p = SelectedPort(); if (p != null) settings["port"] = p;
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(SettingsPath));
                List<string> lines = new List<string>();
                foreach (KeyValuePair<string, string> kv in settings) lines.Add(kv.Key + "=" + kv.Value);
                File.WriteAllLines(SettingsPath, lines.ToArray());
            }
            catch (Exception) { }
        }
    }
}
