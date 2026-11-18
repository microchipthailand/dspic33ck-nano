// Cli.cs - nanobl.exe, the command-line front end (MPLAB X post-build step, VS Code tasks, scripts).
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.IO.Ports;

namespace NanoBootloader
{
    public static class Cli
    {
        const string Usage =
@"nanobl - program a dsPIC33CK Nano through its serial bootloader (J5, 115200 baud)

  nanobl --port COM7 --file app.hex             program, verify, commit and run
  nanobl --port COM7 --file app.X.production.elf  (uses the .hex next to the ELF)
  nanobl --port COM7 --file app.bin [--format bin|bin4] [--base 0x1800]
  nanobl --port COM7 --verify app.hex           compare the board with the file, write nothing
  nanobl --port COM7 --run                      start the application already on the board
  nanobl --port COM7 --info                     show the bootloader / device identity
  nanobl --file app.hex --dry-run               check the file only (no board)
  nanobl --file app.hex --export-bin app.bin    write the packed image (3 bytes/word from 0x001800)
  nanobl --file app.hex --combine bootloader.hex --out factory.hex
                                                bootloader + application + signature, for a PICkit
  nanobl --list                                 list serial ports

options
  --port COMn | auto    'auto' = the only serial port present
  --format auto|hex|bin|bin4   input format (default: from the extension; .bin layout is detected)
  --base ADDR           program address of the first BIN word (default 0x1800)
  --no-run              leave the board in the bootloader after programming
  --no-soft-entry       do not send the reset request to the running application
  --soft-baud LIST      baud rates for the reset request, e.g. 115200,9600 (default: common rates)
  --wait SEC            if nothing answers, keep waiting SEC seconds for manual entry (USER + RESET)
  --quiet / --verbose
exit status: 0 = success, 1 = failure, 2 = usage error, 3 = verify mismatch";

        static bool quiet, verbose;
        static void Out(string s) { if (!quiet) Console.WriteLine("nanobl: " + s); }
        static void Warn(string s) { Console.Error.WriteLine("nanobl: warning: " + s); }

        public static int Main(string[] args)
        {
            try { Proto.SelfCheck(); } catch (BlException e) { Console.Error.WriteLine("nanobl: " + e.Message); return 1; }
            if (args.Length == 0 || Has(args, "--help") || Has(args, "-h") || Has(args, "/?")) { Console.WriteLine(Usage); return args.Length == 0 ? 2 : 0; }

            Dictionary<string, string> a = new Dictionary<string, string>();
            HashSet<string> flags = new HashSet<string>();
            string[] valued = { "--port", "--file", "--format", "--base", "--soft-baud", "--wait", "--export-bin", "--export-bin4",
                                "--combine", "--out", "--verify" };
            for (int i = 0; i < args.Length; i++)
            {
                string k = args[i];
                if (Array.IndexOf(valued, k) >= 0)
                {
                    if (i + 1 >= args.Length) { Console.Error.WriteLine("nanobl: " + k + " needs a value"); return 2; }
                    a[k] = args[++i];
                }
                else if (k.StartsWith("--")) flags.Add(k);
                else if (!a.ContainsKey("--file")) a["--file"] = k;   // bare argument = the file
                else { Console.Error.WriteLine("nanobl: unexpected argument " + k); return 2; }
            }
            string[] known = { "--no-run", "--no-soft-entry", "--quiet", "--verbose", "--dry-run", "--run", "--info", "--list" };
            foreach (string f in flags)
                if (Array.IndexOf(known, f) < 0) { Console.Error.WriteLine("nanobl: unknown option " + f + "\n\n" + Usage); return 2; }
            quiet = flags.Contains("--quiet"); verbose = flags.Contains("--verbose");

            try
            {
                if (flags.Contains("--list")) { ListPorts(); return 0; }

                UploadOptions o = new UploadOptions();
                o.Run = !flags.Contains("--no-run");
                o.SoftEntry = !flags.Contains("--no-soft-entry");
                if (a.ContainsKey("--soft-baud")) o.SoftEntryBauds = ParseBauds(a["--soft-baud"]);
                if (a.ContainsKey("--wait")) o.ManualWaitSeconds = int.Parse(a["--wait"], CultureInfo.InvariantCulture);

                string file = a.ContainsKey("--verify") ? a["--verify"] : (a.ContainsKey("--file") ? a["--file"] : null);
                AppImage img = null;
                if (file != null)
                {
                    InputFormat fmt = ParseFormat(a.ContainsKey("--format") ? a["--format"] : "auto");
                    uint bbase = a.ContainsKey("--base") ? ParseAddr(a["--base"]) : Proto.AppBase;
                    img = AppImage.Load(file, fmt, bbase);
                    Out(img.Summary());
                    foreach (string n in img.Notes) Out(n);
                    foreach (string w in img.Warnings) Warn(w);
                }

                bool offline = false;
                if (a.ContainsKey("--export-bin") || a.ContainsKey("--export-bin4"))
                {
                    Need(img, "--export-bin");
                    bool w4 = a.ContainsKey("--export-bin4");
                    string path = w4 ? a["--export-bin4"] : a["--export-bin"];
                    img.WriteBin(path, w4);
                    Out("wrote " + path + (w4 ? " (4 bytes/word" : " (3 bytes/word") + ", first word = 0x001800)");
                    offline = true;
                }
                if (a.ContainsKey("--combine"))
                {
                    Need(img, "--combine");
                    if (!a.ContainsKey("--out")) throw new BlException("--combine needs --out factory.hex");
                    img.WriteCombinedHex(a["--combine"], a["--out"]);
                    Out("wrote " + a["--out"] + " = bootloader + application + signature (program it with a PICkit/ICD)");
                    offline = true;
                }
                if (flags.Contains("--dry-run")) { Need(img, "--dry-run"); return 0; }
                if (offline && !a.ContainsKey("--port")) return 0;

                if (!a.ContainsKey("--port")) throw new BlException("--port is required (or --dry-run / --export-bin / --combine)");
                string port = a["--port"];
                if (port.Equals("auto", StringComparison.OrdinalIgnoreCase))
                {
                    string[] ports = SerialPort.GetPortNames();
                    if (ports.Length != 1)
                        throw new BlException("--port auto needs exactly one serial port, found " + ports.Length + ": " + string.Join(", ", ports));
                    port = ports[0];
                    Out("using " + port);
                }

                using (SerialTransport t = new SerialTransport(port))
                {
                    Uploader up = new Uploader(t);
                    up.Log = Out;
                    up.Detail = delegate (string s) { if (verbose) Out(s); };
                    int lastPct = -1;
                    up.Progress = delegate (int done, int total)
                    {
                        int pct = done * 100 / total;
                        if (pct / 10 != lastPct / 10 || done == total) { Out(string.Format("  {0}/{1} blocks ({2}%)", done, total, pct)); lastPct = pct; }
                    };

                    if (flags.Contains("--info")) { up.Info(o); return 0; }
                    if (flags.Contains("--run") && img == null) { up.Enter(o); up.Run(); return 0; }
                    Need(img, "programming");
                    if (a.ContainsKey("--verify")) return up.Verify(img, o) ? 0 : 3;
                    up.Program(img, o);
                    if (quiet) Console.WriteLine("nanobl: " + Path.GetFileName(img.SourcePath) + " programmed on " + port);
                }
                return 0;
            }
            catch (BlException e) { Console.Error.WriteLine("nanobl: error: " + e.Message); return 1; }
            catch (FormatException e) { Console.Error.WriteLine("nanobl: " + e.Message); return 2; }
        }

        static void Need(AppImage img, string what) { if (img == null) throw new BlException(what + " needs --file"); }
        static bool Has(string[] a, string s) { return Array.IndexOf(a, s) >= 0; }

        public static InputFormat ParseFormat(string s)
        {
            switch (s.ToLowerInvariant())
            {
                case "auto": return InputFormat.Auto;
                case "hex": return InputFormat.Hex;
                case "bin": case "bin3": return InputFormat.BinPacked;
                case "bin4": return InputFormat.BinWord4;
                default: throw new FormatException("--format must be auto, hex, bin or bin4");
            }
        }

        public static uint ParseAddr(string s)
        {
            s = s.Trim();
            if (s.StartsWith("0x", StringComparison.OrdinalIgnoreCase)) return uint.Parse(s.Substring(2), NumberStyles.HexNumber, CultureInfo.InvariantCulture);
            return uint.Parse(s, CultureInfo.InvariantCulture);
        }

        public static int[] ParseBauds(string s)
        {
            List<int> l = new List<int>();
            foreach (string p in s.Split(',', ';', ' ')) if (p.Trim().Length > 0) l.Add(int.Parse(p.Trim(), CultureInfo.InvariantCulture));
            if (l.Count == 0) throw new FormatException("--soft-baud needs at least one rate");
            return l.ToArray();
        }

        static void ListPorts()
        {
            foreach (KeyValuePair<string, string> p in PortList.Get()) Console.WriteLine(p.Key + "\t" + p.Value);
        }
    }

    // Port names with friendly descriptions (WMI), falling back to plain names.
    public static class PortList
    {
        public static List<KeyValuePair<string, string>> Get()
        {
            List<KeyValuePair<string, string>> list = new List<KeyValuePair<string, string>>();
            Dictionary<string, string> names = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            try
            {
                using (System.Management.ManagementObjectSearcher s = new System.Management.ManagementObjectSearcher(
                    "SELECT Name FROM Win32_PnPEntity WHERE Name LIKE '%(COM%'"))
                    foreach (System.Management.ManagementBaseObject m in s.Get())
                    {
                        string n = (string)m["Name"];
                        int i = n.LastIndexOf("(COM");
                        if (i < 0) continue;
                        int j = n.IndexOf(')', i);
                        if (j > i) names[n.Substring(i + 1, j - i - 1)] = n.Substring(0, i).Trim();
                    }
            }
            catch (Exception) { }
            List<string> ports = new List<string>(SerialPort.GetPortNames());
            ports.Sort(delegate (string x, string y) { return PortNum(x).CompareTo(PortNum(y)); });
            foreach (string p in ports)
            {
                string d;
                list.Add(new KeyValuePair<string, string>(p, names.TryGetValue(p, out d) ? d : ""));
            }
            return list;
        }

        static int PortNum(string p) { int n; return int.TryParse(p.Substring(3), out n) ? n : 9999; }
    }
}
