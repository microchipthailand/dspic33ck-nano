// Emulator.cs - an in-process model of the dsPIC33CK Nano bootloader (bl_main.c, protocol 1) behind ITransport,
// so the host's framing, soft entry, retries and upload sequence can be tested without a board.
// Flash is modelled as real NOR flash: programming can only clear bits (AND), so a missing erase is caught.
using System;
using System.Collections.Generic;

namespace NanoBootloader
{
    public class Emulator : ITransport
    {
        public uint[] Flash = new uint[0x2C000 / 2];
        public bool InBootloader;           // false = an application is running
        public int AppBaud = 115200;        // baud rate the running application listens at
        public bool AppSniffsMagic = true;  // the application feeds UART bytes to the soft-entry matcher
        public int Baud = 115200;
        public int CorruptResponses;        // corrupt this many upcoming responses (one byte flipped)
        public int DropResponses;           // swallow this many upcoming responses
        public int FaultsFromFrame;         // faults apply only once this many frames have been received
        public int DropRequests;            // ignore this many upcoming (armed) request frames entirely
        public int Frames, Naks, Resets, Jumps;
        public bool WriteOnUnerased;        // set if a program operation hit a non-erased word

        readonly Queue<byte> outq = new Queue<byte>();
        readonly List<byte> inbuf = new List<byte>();
        int magicPos;

        public string Name { get { return "EMU"; } }

        public Emulator()
        {
            for (int i = 0; i < Flash.Length; i++) Flash[i] = 0xFFFFFF;
            for (uint a = 0; a < 0x1600; a += 2) Flash[a / 2] = 0x123456 ^ a;   // stand-in bootloader code
            Flash[0x2BF18 / 2] = 0xFFFF78; Flash[0x2BF1C / 2] = 0xFFFF7B; Flash[0x2BF20 / 2] = 0xFF7FEF; Flash[0x2BF28 / 2] = 0xFFFFDD;
        }

        public void SetBaud(int b) { Baud = b; }
        public void Purge() { outq.Clear(); }
        public void Dispose() { }

        public int Read(byte[] buf, int off, int count, int timeoutMs)
        {
            int n = 0;
            while (n < count && outq.Count > 0) buf[off + n++] = outq.Dequeue();
            return n;
        }

        public void Write(byte[] d, int off, int len)
        {
            for (int i = 0; i < len; i++)
            {
                byte b = d[off + i];
                if (!InBootloader)
                {
                    // The application only understands bytes at its own baud rate; anything else is noise to it.
                    if (!AppSniffsMagic || Baud != AppBaud) continue;
                    if (b == Proto.SoftEntryMagic[magicPos]) { if (++magicPos == Proto.SoftEntryMagic.Length) { magicPos = 0; Reset(); } }
                    else magicPos = (b == Proto.SoftEntryMagic[0]) ? 1 : 0;
                    continue;
                }
                if (Baud != Proto.Baud) continue;           // the bootloader only speaks 115200
                inbuf.Add(b);
                TryFrame();
            }
        }

        void Reset() { Resets++; InBootloader = true; inbuf.Clear(); }

        void TryFrame()
        {
            while (inbuf.Count > 0 && inbuf[0] != Proto.SOH) inbuf.RemoveAt(0);   // resync silently
            if (inbuf.Count < 4) return;
            int len = inbuf[2] | (inbuf[3] << 8);
            if (len > 4 + 128 * 3) { inbuf.Clear(); Nak(0x01); return; }
            if (inbuf.Count < 4 + len + 2) return;
            byte[] f = inbuf.GetRange(1, 3 + len).ToArray();
            int crc = inbuf[4 + len] | (inbuf[5 + len] << 8);
            inbuf.RemoveRange(0, 4 + len + 2);
            Frames++;
            if (Frames > FaultsFromFrame && DropRequests > 0) { DropRequests--; return; }   // never executed
            if (crc != Proto.Crc16(f, 0, f.Length)) { Nak(0x01); return; }
            byte[] p = new byte[len];
            Array.Copy(f, 3, p, 0, len);
            Dispatch(f[0], p);
        }

        void Respond(byte status, byte[] payload)
        {
            bool armed = Frames > FaultsFromFrame;
            if (armed && DropResponses > 0) { DropResponses--; return; }
            int n = payload == null ? 0 : payload.Length;
            byte[] r = new byte[3 + n + 2];
            r[0] = status; r[1] = (byte)n; r[2] = (byte)(n >> 8);
            if (n > 0) Array.Copy(payload, 0, r, 3, n);
            ushort c = Proto.Crc16(r, 0, 3 + n);
            r[3 + n] = (byte)c; r[4 + n] = (byte)(c >> 8);
            if (armed && CorruptResponses > 0) { CorruptResponses--; r[r.Length - 1] ^= 0x5A; }
            foreach (byte b in r) outq.Enqueue(b);
        }
        void Ack(byte[] p) { Respond(Proto.ACK, p); }
        void Nak(byte code) { Naks++; Respond(Proto.NAK, new byte[] { code }); }

        uint Word(uint a) { return Flash[a / 2]; }
        void Program(uint a, uint v)
        {
            uint old = Flash[a / 2];
            if (old != 0xFFFFFF && v != 0xFFFFFF) WriteOnUnerased = true;
            Flash[a / 2] = old & v;
        }

        uint FlashCrc(uint addr, uint len)
        {
            byte[] s = new byte[len / 2 * 3];
            for (uint i = 0, a = addr; a < addr + len; a += 2, i += 3)
            { uint w = Word(a); s[i] = (byte)w; s[i + 1] = (byte)(w >> 8); s[i + 2] = (byte)(w >> 16); }
            return Proto.Crc32(s, 0, s.Length);
        }

        public bool AppValid()
        {
            uint b = Proto.SigBase;
            if (Word(b + 8) != 0x4B4333 || Word(b + 0xA) != (~0x4B4333u & 0xFFFFFF) || Word(b + 6) != 1) return false;
            uint len = Word(b);
            if (len == 0 || (len & 1) != 0 || len > Proto.SigBase - Proto.AppBase) return false;
            uint crc = Word(b + 2) | (Word(b + 4) << 24);
            return FlashCrc(Proto.AppBase, len) == crc;
        }

        void Dispatch(byte cmd, byte[] p)
        {
            switch (cmd)
            {
                case Proto.CmdSync:
                    {
                        if (p.Length != 0) { Nak(0x03); return; }
                        byte[] r = new byte[31];
                        r[0] = (byte)'3'; r[1] = (byte)'3'; r[2] = (byte)'C'; r[3] = (byte)'K'; r[4] = 1;
                        Proto.Put32(r, 5, 0x7C14); Proto.Put32(r, 9, 0xA002); Proto.Put32(r, 13, 0x400);
                        r[17] = 128; r[18] = 0;
                        Proto.Put32(r, 19, Proto.AppBase); Proto.Put32(r, 23, Proto.SigBase); Proto.Put32(r, 27, Proto.AppEnd);
                        Ack(r); return;
                    }
                case Proto.CmdErase:
                    if (p.Length != 0) { Nak(0x03); return; }
                    for (uint a = Proto.AppBase; a < Proto.AppEnd; a += 2) Flash[a / 2] = 0xFFFFFF;
                    Ack(null); return;
                case Proto.CmdWrite:
                    {
                        if (p.Length < 7 || (p.Length - 4) % 3 != 0) { Nak(0x03); return; }
                        int words = (p.Length - 4) / 3;
                        if (words > 128) { Nak(0x03); return; }
                        uint addr = Proto.Get32(p, 0);
                        if ((addr & 3) != 0) { Nak(0x05); return; }
                        uint end = addr + 2u * (uint)(words + (words & 1));
                        if (addr < Proto.AppBase || end > Proto.SigBase) { Nak(0x04); return; }
                        for (int i = 0; i < words; i++)
                            Program(addr + 2u * (uint)i, (uint)(p[4 + 3 * i] | (p[5 + 3 * i] << 8) | (p[6 + 3 * i] << 16)));
                        Ack(null); return;
                    }
                case Proto.CmdReadCrc:
                    {
                        if (p.Length != 8) { Nak(0x03); return; }
                        uint addr = Proto.Get32(p, 0), len = Proto.Get32(p, 4);
                        if (((addr | len) & 1) != 0) { Nak(0x05); return; }
                        if (len == 0 || addr < Proto.AppBase || addr + len > Proto.AppEnd) { Nak(0x04); return; }
                        byte[] r = new byte[4]; Proto.Put32(r, 0, FlashCrc(addr, len)); Ack(r); return;
                    }
                case Proto.CmdCommit:
                    {
                        if (p.Length != 8) { Nak(0x03); return; }
                        uint len = Proto.Get32(p, 0), crc = Proto.Get32(p, 4);
                        if (len == 0 || (len & 1) != 0 || len > Proto.SigBase - Proto.AppBase) { Nak(0x04); return; }
                        if (FlashCrc(Proto.AppBase, len) != crc) { Nak(0x07); return; }
                        uint b = Proto.SigBase;
                        Program(b, len); Program(b + 2, crc & 0xFFFFFF); Program(b + 4, (crc >> 24) & 0xFF); Program(b + 6, 1);
                        Program(b + 8, 0x4B4333); Program(b + 0xA, ~0x4B4333u & 0xFFFFFF);
                        Ack(null); return;
                    }
                case Proto.CmdJump:
                    if (p.Length != 0) { Nak(0x03); return; }
                    if (!AppValid()) { Nak(0x07); return; }
                    Ack(null); Jumps++; InBootloader = false; magicPos = 0; return;
                default:
                    Nak(0x02); return;
            }
        }
    }
}
