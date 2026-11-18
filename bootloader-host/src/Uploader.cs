// Uploader.cs - transport, framing and the upload session. Same sequence as tools/serial_upload.py:
// SYNC (or soft entry + SYNC flood) -> geometry check -> ERASE_APP -> WRITE_ROW... -> READ_CRC -> COMMIT -> JUMP.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO.Ports;
using System.Threading;

namespace NanoBootloader
{
    public interface ITransport : IDisposable
    {
        string Name { get; }
        void SetBaud(int baud);
        void Purge();
        void Write(byte[] data, int off, int len);
        int Read(byte[] buf, int off, int count, int timeoutMs);   // returns bytes read; less only on timeout
    }

    public class SerialTransport : ITransport
    {
        readonly SerialPort port;
        public string Name { get { return port.PortName; } }

        public SerialTransport(string name)
        {
            port = new SerialPort(name, Proto.Baud, Parity.None, 8, StopBits.One);
            port.Handshake = Handshake.None;
            port.ReadTimeout = 50;
            port.WriteTimeout = 3000;
            port.ReadBufferSize = 8192;
            try { port.Open(); }
            catch (UnauthorizedAccessException) { throw new BlException(name + " is in use by another program (close the serial monitor / terminal)"); }
            catch (Exception e) { throw new BlException("cannot open " + name + ": " + e.Message); }
        }

        public void SetBaud(int baud) { port.BaudRate = baud; }
        public void Purge() { port.DiscardInBuffer(); port.DiscardOutBuffer(); }
        public void Write(byte[] d, int off, int len)
        {
            try { port.Write(d, off, len); }
            catch (Exception e) { throw new BlException("write to " + Name + " failed: " + e.Message); }
        }

        public int Read(byte[] buf, int off, int count, int timeoutMs)
        {
            int got = 0;
            Stopwatch sw = Stopwatch.StartNew();
            while (got < count && sw.ElapsedMilliseconds < timeoutMs)
            {
                try
                {
                    if (port.BytesToRead == 0) { Thread.Sleep(1); continue; }
                    got += port.Read(buf, off + got, count - got);
                }
                catch (TimeoutException) { }
                catch (Exception e) { throw new BlException("read from " + Name + " failed: " + e.Message); }
            }
            return got;
        }

        public void Dispose() { try { if (port.IsOpen) port.Close(); } catch (Exception) { } port.Dispose(); }
    }

    public class DeviceInfo
    {
        public int Version; public uint DevId, DevRev, EraseStep, AppBase, SigBase, AppEnd; public int BlockWords;
        public override string ToString()
        {
            return string.Format("DEVID 0x{0:X6} rev 0x{1:X6}, protocol {2}, application 0x{3:X6}-0x{4:X6}, erase step 0x{5:X}",
                DevId, DevRev, Version, AppBase, SigBase, EraseStep);
        }
    }

    public class Link
    {
        public const int Retries = 3;
        readonly ITransport t;
        public Action<string> Verbose;
        public Link(ITransport transport) { t = transport; }

        static byte[] Frame(byte cmd, byte[] payload)
        {
            int n = payload == null ? 0 : payload.Length;
            byte[] f = new byte[1 + 3 + n + 2];
            f[0] = Proto.SOH; f[1] = cmd; f[2] = (byte)n; f[3] = (byte)(n >> 8);
            if (n > 0) Buffer.BlockCopy(payload, 0, f, 4, n);
            ushort crc = Proto.Crc16(f, 1, 3 + n);
            f[4 + n] = (byte)crc; f[5 + n] = (byte)(crc >> 8);
            return f;
        }

        // Returns the payload on ACK; null + error text otherwise. refused = the board NAKed (do not retry).
        byte[] Response(int timeoutMs, out string err, out bool refused)
        {
            refused = false;
            byte[] head = new byte[3];
            if (t.Read(head, 0, 3, timeoutMs) < 3) { err = "no response"; return null; }
            int len = head[1] | (head[2] << 8);
            if (len > 512) { err = "response length " + len + " is implausible"; return null; }
            byte[] rest = new byte[len + 2];
            int got = t.Read(rest, 0, len + 2, timeoutMs);
            if (got < len + 2) { err = "response truncated after " + got + " of " + (len + 2) + " bytes"; return null; }
            byte[] all = new byte[3 + len];
            Buffer.BlockCopy(head, 0, all, 0, 3); Buffer.BlockCopy(rest, 0, all, 3, len);
            if ((rest[len] | (rest[len + 1] << 8)) != Proto.Crc16(all, 0, all.Length)) { err = "response CRC mismatch"; return null; }
            byte[] p = new byte[len];
            Buffer.BlockCopy(rest, 0, p, 0, len);
            if (head[0] == Proto.ACK) { err = null; return p; }
            if (head[0] == Proto.NAK)
            {
                int code = len > 0 ? p[0] : 0;
                refused = true;
                err = "the board refused it: " + Proto.ErrorText(code) + " (error " + code + ")";
                return null;
            }
            err = string.Format("unexpected status byte 0x{0:X2}", head[0]);
            return null;
        }

        public byte[] Command(byte cmd, byte[] payload, int timeoutMs, int retries)
        {
            byte[] f = Frame(cmd, payload);
            string last = "not attempted";
            for (int attempt = 0; attempt < retries; attempt++)
            {
                t.Purge();
                t.Write(f, 0, f.Length);
                string err; bool refused;
                byte[] p = Response(timeoutMs, out err, out refused);
                if (err == null) return p;
                last = err;
                if (refused) break;                    // a NAK is the board's answer, not a lost byte
                if (Verbose != null && attempt + 1 < retries)
                    Verbose(string.Format("cmd 0x{0:X2}: {1}, retrying ({2}/{3})", cmd, err, attempt + 2, retries));
            }
            throw new BlException(string.Format("command 0x{0:X2} failed: {1}", cmd, last));
        }
    }

    public class UploadOptions
    {
        public bool SoftEntry = true;
        public int[] SoftEntryBauds = Proto.DefaultSoftEntryBauds;
        public int SoftEntryWindowMs = 1500;     // SYNC flood after each magic
        public int ManualWaitSeconds = 0;        // >0: keep knocking this long for USER+RESET manual entry
        public bool Run = true;
    }

    public class Uploader
    {
        readonly ITransport t;
        readonly Link link;
        public Action<string> Log = delegate { };
        public Action<string> Detail = delegate { };
        public Action<int, int> Progress = delegate { };
        public volatile bool CancelRequested;
        const int KnockMs = 60;                  // ~5 attempts inside the bootloader's 300 ms window

        public Uploader(ITransport transport)
        {
            t = transport;
            link = new Link(t);
            link.Verbose = delegate (string s) { Detail(s); };
        }

        void CheckCancel() { if (CancelRequested) throw new BlException("cancelled"); }

        public DeviceInfo Sync(int timeoutMs)
        {
            byte[] p = link.Command(Proto.CmdSync, null, timeoutMs, 1);
            if (p.Length != 31 || p[0] != '3' || p[1] != '3' || p[2] != 'C' || p[3] != 'K')
                throw new BlException("SYNC answered with something that is not the dsPIC33CK Nano bootloader");
            DeviceInfo d = new DeviceInfo();
            d.Version = p[4]; d.DevId = Proto.Get32(p, 5); d.DevRev = Proto.Get32(p, 9); d.EraseStep = Proto.Get32(p, 13);
            d.BlockWords = p[17] | (p[18] << 8); d.AppBase = Proto.Get32(p, 19); d.SigBase = Proto.Get32(p, 23); d.AppEnd = Proto.Get32(p, 27);
            return d;
        }

        DeviceInfo Knock(int timeoutMs)
        {
            try { return Sync(timeoutMs); } catch (BlException) { return null; }
        }

        static void CheckGeometry(DeviceInfo d)
        {
            if (d.Version != Proto.Version)
                throw new BlException("the board speaks bootloader protocol " + d.Version + ", this tool speaks " + Proto.Version +
                                      ". Use the matching tool or re-burn the bootloader.");
            if (d.AppBase != Proto.AppBase || d.SigBase != Proto.SigBase || d.AppEnd != Proto.AppEnd || d.BlockWords != Proto.BlockWords)
                throw new BlException("the board reports a different memory map (" + d + "); this tool and the linker script assume " +
                                      "application 0x001800-0x02B700");
        }

        // Get the board into its command loop: already there -> soft entry -> wait for a manual USER+RESET.
        public DeviceInfo Enter(UploadOptions o)
        {
            t.SetBaud(Proto.Baud);
            Log("looking for the bootloader on " + t.Name);
            DeviceInfo d = Knock(500);
            if (d != null) { Log("the bootloader is already listening"); CheckGeometry(d); return d; }

            if (o.SoftEntry)
            {
                foreach (int baud in o.SoftEntryBauds)
                {
                    CheckCancel();
                    // The magic must be understood by the RUNNING APPLICATION at its own baud rate; the bootloader
                    // that follows the reset always speaks 115200.
                    t.SetBaud(baud);
                    t.Purge();
                    t.Write(Proto.SoftEntryMagic, 0, Proto.SoftEntryMagic.Length);
                    // Let the 10 bytes leave before the baud change; USB-serial adapters buffer, so allow for latency.
                    Thread.Sleep(Math.Max(20, 120000 / baud + 5));
                    t.SetBaud(Proto.Baud);
                    Stopwatch sw = Stopwatch.StartNew();
                    while (sw.ElapsedMilliseconds < o.SoftEntryWindowMs)
                    {
                        CheckCancel();
                        d = Knock(KnockMs);
                        if (d != null)
                        {
                            Log("the application reset itself into the bootloader (soft entry at " + baud + " baud)");
                            CheckGeometry(d);
                            return d;
                        }
                    }
                    Detail("no answer after soft entry at " + baud + " baud");
                }
            }

            if (o.ManualWaitSeconds > 0)
            {
                Log("waiting " + o.ManualWaitSeconds + " s for manual entry: hold USER, press and release RESET, keep USER held ~1 s");
                Stopwatch sw = Stopwatch.StartNew();
                while (sw.ElapsedMilliseconds < o.ManualWaitSeconds * 1000L)
                {
                    CheckCancel();
                    d = Knock(KnockMs);
                    if (d != null) { Log("bootloader found"); CheckGeometry(d); return d; }
                }
            }

            throw new BlException("no bootloader answered on " + t.Name + ".\n" +
                "  * Board never had the bootloader: program bootloader-dspic33ck-nano-115200-v1.hex once with a PICkit on J4.\n" +
                "  * The application does not feed received UART1 bytes to NanoBL_RxByte(), so it cannot be asked to reset:\n" +
                "    hold USER, press and release RESET, keep USER held ~1 s (LED stays on), then try again\n" +
                "    (or use --wait 20 / 'Wait for manual entry').\n" +
                "  * Check the port is the USB-serial adapter on J5 (adapter TX -> J5-3 RX, adapter RX -> J5-2 TX, GND -> J5-4)\n" +
                "    and that no terminal has the port open.");
        }

        static byte[] AddrLen(uint a, uint b)
        {
            byte[] p = new byte[8]; Proto.Put32(p, 0, a); Proto.Put32(p, 4, b); return p;
        }

        public uint ReadCrc(uint addr, uint len)
        {
            byte[] p = link.Command(Proto.CmdReadCrc, AddrLen(addr, len), 8000, Link.Retries);
            if (p.Length != 4) throw new BlException("READ_CRC answered " + p.Length + " bytes, expected 4");
            return Proto.Get32(p, 0);
        }

        // Thrown when a write may have been half done: the only safe continuation is a fresh ERASE.
        class PartialWrite : BlException { public PartialWrite(string m) : base(m) { } }

        static uint CrcOf(byte[] d) { return Proto.Crc32(d, 0, d.Length); }
        static byte[] Blank(int len) { byte[] b = new byte[len]; for (int i = 0; i < len; i++) b[i] = 0xFF; return b; }

        // Flash with ECC must not be programmed twice between erases, so a WRITE_ROW or COMMIT whose answer was lost is
        // NOT simply re-sent (the board may already have executed it). READ_CRC, which changes nothing, tells which:
        //   CRC == data   -> it was written, carry on;   CRC == erased -> it was not, send it again;
        //   anything else -> partly written: restart the whole upload from ERASE.
        void SafeWrite(byte cmd, byte[] payload, uint addr, byte[] expect, int timeoutMs)
        {
            uint want = CrcOf(expect), erased = CrcOf(Blank(expect.Length));
            uint len = (uint)(expect.Length / 3 * 2);
            for (int attempt = 1; ; attempt++)
            {
                string err;
                try { link.Command(cmd, payload, timeoutMs, 1); return; }
                catch (BlException e) { if (e.Message.Contains("refused")) throw; err = e.Message; }
                uint got = ReadCrc(addr, len);
                if (got == want) { Detail(string.Format("0x{0:X6}: answer lost but the data is on the board", addr)); return; }
                if (got != erased) throw new PartialWrite(string.Format("0x{0:X6} was partly written ({1})", addr, err));
                if (attempt >= Link.Retries) throw new BlException(string.Format("writing 0x{0:X6} failed: {1}", addr, err));
                Detail(string.Format("0x{0:X6}: {1}, not written yet, sending it again", addr, err));
            }
        }

        public void Program(AppImage img, UploadOptions o)
        {
            Enter(o);
            Log(string.Format("image 0x{0:X6}-0x{1:X6}, {2} words, CRC-32 0x{3:X8}", Proto.AppBase, Proto.AppBase + img.Length, img.Length / 2, img.Crc));
            for (int pass = 1; ; pass++)
            {
                try { ProgramOnce(img); break; }
                catch (PartialWrite e)
                {
                    if (pass >= 2) throw new BlException(e.Message + "; the link is too unreliable - check the wiring and try again");
                    Log(e.Message + " - starting again from the erase");
                }
            }
            Log(string.Format("{0} instruction words written, verified and committed", img.Length / 2));
            if (o.Run) Run();
            else Log("left in the bootloader (run not requested)");
        }

        void ProgramOnce(AppImage img)
        {
            // From ERASE until COMMIT the board has no valid application: an interrupted upload leaves it waiting in
            // the bootloader, recoverable over serial. ERASE itself is safe to repeat.
            Log("erasing the application area");
            CheckCancel();
            link.Command(Proto.CmdErase, null, 10000, Link.Retries);

            List<AppImage.Block> blocks = img.Blocks();
            Log("writing " + blocks.Count + " block(s)");
            for (int i = 0; i < blocks.Count; i++)
            {
                CheckCancel();
                byte[] p = new byte[4 + blocks[i].Data.Length];
                Proto.Put32(p, 0, blocks[i].Addr);
                Buffer.BlockCopy(blocks[i].Data, 0, p, 4, blocks[i].Data.Length);
                SafeWrite(Proto.CmdWrite, p, blocks[i].Addr, blocks[i].Data, 3000);
                Progress(i + 1, blocks.Count);
            }

            Log("verifying with the board's own CRC-32");
            uint got = ReadCrc(Proto.AppBase, img.Length);
            if (got != img.Crc)
                throw new BlException(string.Format("verify failed: the board computed 0x{0:X8}, the image is 0x{1:X8}. " +
                    "Nothing was committed; the board stays in the bootloader waiting for a good upload.", got, img.Crc));

            // COMMIT re-checks the CRC itself, then writes the 6-word signature row (also protected against re-sending).
            Dictionary<uint, uint> sw = img.SignatureWords();
            byte[] sig = new byte[18];
            for (int k = 0; k < 6; k++)
            {
                uint v = sw[Proto.SigBase + 2u * (uint)k];
                sig[3 * k] = (byte)v; sig[3 * k + 1] = (byte)(v >> 8); sig[3 * k + 2] = (byte)(v >> 16);
            }
            SafeWrite(Proto.CmdCommit, AddrLen(img.Length, img.Crc), Proto.SigBase, sig, 8000);
        }

        // Compare the board's flash with the file without writing anything.
        public bool Verify(AppImage img, UploadOptions o)
        {
            Enter(o);
            uint got = ReadCrc(Proto.AppBase, img.Length);
            bool same = got == img.Crc;
            Log(same ? string.Format("board matches the file (CRC-32 0x{0:X8})", got)
                     : string.Format("board DIFFERS from the file: board 0x{0:X8}, file 0x{1:X8}", got, img.Crc));
            if (o.Run && same) Run();
            else if (o.Run) Log("not starting it: the board does not hold this file");
            return same;
        }

        public void Run()
        {
            // JUMP acks, then runs the application: there is no second answer to wait for.
            try { link.Command(Proto.CmdJump, null, 1000, 1); Log("application started"); }
            catch (BlException e)
            {
                if (e.Message.Contains("refused")) throw new BlException("the board has no valid application to run (" + e.Message + ")");
                Log("the board did not confirm the start (" + e.Message + "); it runs the application at the next reset");
            }
        }

        public DeviceInfo Info(UploadOptions o)
        {
            DeviceInfo d = Enter(o);
            Log("board: " + d);
            return d;
        }
    }
}
