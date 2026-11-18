// Protocol.cs - wire protocol, memory map and checksums of the dsPIC33CK Nano serial bootloader.
//
// Mirror of bootloaders/dspic33ck_nano/bl_config.h (protocol version 1). The geometry here is only a
// cross-check: SYNC returns the board's own numbers and the uploader refuses to continue if they differ.
// C# 5 (the compiler that ships with .NET Framework 4.x), so no newer language features are used.
using System;

namespace NanoBootloader
{
    public static class Proto
    {
        public const int Version = 1;

        // Program-memory addresses (2 address units per 24-bit instruction word).
        public const uint AppBase = 0x001800;      // application entry GOTO + interrupt trampoline
        public const uint SigBase = 0x02B700;      // signature row, written by COMMIT only
        public const uint AppEnd = 0x02B800;       // first address the bootloader never erases
        public const uint CfgBase = 0x02BF00;      // config words: owned by the bootloader, never written
        public const uint DevIdSpace = 0x800000;   // DEVID / OTP / executive: never programmed
        public const int BlockWords = 128;         // instruction words per WRITE_ROW frame
        public const uint Blank = 0xFFFFFF;        // erased flash
        public const int Baud = 115200;            // the only rate the bootloader speaks

        public const byte SOH = 0x01, ACK = 0x06, NAK = 0x15;
        public const byte CmdSync = 0x10, CmdErase = 0x20, CmdWrite = 0x30, CmdReadCrc = 0x40,
                          CmdCommit = 0x50, CmdJump = 0x60;

        public static string ErrorText(int code)
        {
            switch (code)
            {
                case 0x01: return "frame CRC mismatch";
                case 0x02: return "unknown command";
                case 0x03: return "wrong payload length";
                case 0x04: return "address outside the application region";
                case 0x05: return "address not double-word aligned";
                case 0x06: return "the flash write or erase did not take (WRERR)";
                case 0x07: return "no valid application: signature missing or CRC mismatch";
                default: return "unknown error";
            }
        }

        // Sent to a RUNNING application to make it reset into the bootloader's 300 ms window.
        // Eight improbable bytes + their CRC-16 (checked at start-up by SelfCheck()).
        public static readonly byte[] SoftEntryMagic = { 0x1B, 0xF0, 0x33, 0x43, 0x4B, 0x21, 0x9E, 0x57, 0xE8, 0x3B };
        public static readonly int[] DefaultSoftEntryBauds = { 115200, 9600, 57600, 38400, 19200, 230400, 250000 };

        // Config words the bootloader programs (from bootloader-dspic33ck-nano-115200-v1.hex).
        // Every other config word is left erased (0xFFFFFF).
        public static uint BootloaderConfig(uint addr)
        {
            switch (addr)
            {
                case 0x2BF18: return 0xFFFF78;   // FOSCSEL: FNOSC=FRC, IESO=OFF
                case 0x2BF1C: return 0xFFFF7B;   // FOSC   : POSCMD=NONE, OSCIOFNC=ON, FCKSM=CSECMD
                case 0x2BF20: return 0xFF7FEF;   // FWDT   : RWDTPS=PS32768, WINDIS=ON, FWDTEN=ON_SW
                case 0x2BF28: return 0xFFFFDD;   // FICD   : ICS=PGD3, JTAGEN=OFF
                default: return Blank;
            }
        }

        public static string ConfigName(uint addr)
        {
            switch (addr)
            {
                case 0x2BF00: return "FSEC";     case 0x2BF10: return "FBSLIM";   case 0x2BF18: return "FOSCSEL";
                case 0x2BF1C: return "FOSC";     case 0x2BF20: return "FWDT";     case 0x2BF24: return "FPOR";
                case 0x2BF28: return "FICD";     case 0x2BF2C: return "FDMTIVTL"; case 0x2BF30: return "FDMTIVTH";
                case 0x2BF34: return "FDMTCNTL"; case 0x2BF38: return "FDMTCNTH"; case 0x2BF3C: return "FDMT";
                case 0x2BF40: return "FDEVOPT";  case 0x2BF44: return "FALTREG";
                default: return "0x" + addr.ToString("X6");
            }
        }

        // Short decode of the fields that change how an application behaves (DS70005349, Section 21).
        public static string DecodeConfig(uint addr, uint v)
        {
            if (addr == 0x2BF18)
            {
                string[] osc = { "FRC", "FRCPLL", "PRI", "PRIPLL", "SOSC", "LPRC", "BFRC", "FRCDIVN" };
                return "FNOSC=" + osc[v & 7] + ((v & 0x80) != 0 ? " IESO=ON" : " IESO=OFF");
            }
            if (addr == 0x2BF1C)
            {
                string[] posc = { "EC", "XT", "HS", "NONE" };
                string[] cksm = { "CSECME", "CSECMD", "CSDCMD", "CSDCMD" };
                return "POSCMD=" + posc[v & 3] + " OSCIOFNC=" + ((v & 4) != 0 ? "OFF" : "ON") + " FCKSM=" + cksm[(v >> 6) & 3];
            }
            if (addr == 0x2BF20)
                return "FWDTEN=" + ((v & 0x8000) != 0 ? "ON (hardware)" : "ON_SW") + " RWDTPS=PS" + (1u << (int)(v & 0x1F)) +
                       ((v & 0x80) != 0 ? " WINDIS=ON" : " WINDIS=OFF (window mode)");
            if (addr == 0x2BF28)
            {
                string[] ics = { "reserved", "PGD3", "PGD2", "PGD1" };
                return "ICS=" + ics[v & 3] + ((v & 0x20) != 0 ? " JTAGEN=ON" : " JTAGEN=OFF");
            }
            return "";
        }

        // CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF. Over CMD/STATUS, length and payload.
        public static ushort Crc16(byte[] d, int off, int len)
        {
            int crc = 0xFFFF;
            for (int i = 0; i < len; i++)
            {
                crc ^= d[off + i] << 8;
                for (int b = 0; b < 8; b++)
                    crc = (crc & 0x8000) != 0 ? ((crc << 1) ^ 0x1021) & 0xFFFF : (crc << 1) & 0xFFFF;
            }
            return (ushort)crc;
        }

        static readonly uint[] Crc32Table = MakeCrc32Table();
        static uint[] MakeCrc32Table()
        {
            uint[] t = new uint[256];
            for (uint i = 0; i < 256; i++)
            {
                uint c = i;
                for (int k = 0; k < 8; k++) c = (c & 1) != 0 ? (c >> 1) ^ 0xEDB88320u : c >> 1;
                t[i] = c;
            }
            return t;
        }

        // IEEE CRC-32 over the flash byte stream: 3 bytes per instruction word, little-endian, no phantom byte.
        public static uint Crc32(byte[] d, int off, int len)
        {
            uint crc = 0xFFFFFFFFu;
            for (int i = 0; i < len; i++) crc = (crc >> 8) ^ Crc32Table[(crc ^ d[off + i]) & 0xFF];
            return crc ^ 0xFFFFFFFFu;
        }

        public static void SelfCheck()
        {
            ushort want = Crc16(SoftEntryMagic, 0, 8);
            int got = SoftEntryMagic[8] | (SoftEntryMagic[9] << 8);
            if (got != want) throw new BlException("internal: soft-entry magic CRC is wrong");
            byte[] check = System.Text.Encoding.ASCII.GetBytes("123456789");
            if (Crc32(check, 0, 9) != 0xCBF43926u || Crc16(check, 0, 9) != 0x29B1)
                throw new BlException("internal: CRC implementation self-test failed");
        }

        public static void Put32(byte[] b, int off, uint v)
        {
            b[off] = (byte)v; b[off + 1] = (byte)(v >> 8); b[off + 2] = (byte)(v >> 16); b[off + 3] = (byte)(v >> 24);
        }

        public static uint Get32(byte[] b, int off)
        {
            return (uint)(b[off] | (b[off + 1] << 8) | (b[off + 2] << 16) | (b[off + 3] << 24));
        }
    }

    // An error that ends the operation with a message, never a stack trace.
    public class BlException : Exception
    {
        public BlException(string msg) : base(msg) { }
    }
}
