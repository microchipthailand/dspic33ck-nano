// Image.cs - load a dsPIC application (Intel HEX, raw BIN or ELF-with-sibling-HEX) into the exact byte
// stream the bootloader will hold in flash, and write .bin / combined factory .hex files.
//
// dsPIC Intel HEX: FOUR bytes per 24-bit instruction word (low, middle, high, phantom 0x00) and the record
// address is TWICE the program-memory address. The phantom byte is asserted, not skipped.
// Device CRC-32 stream: three bytes per word, little-endian, gaps inside the span filled with 0xFFFFFF.
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace NanoBootloader
{
    public enum InputFormat { Auto, Hex, BinPacked, BinWord4 }

    public class AppImage
    {
        public string SourcePath;
        public string FormatUsed;
        public Dictionary<uint, uint> Words = new Dictionary<uint, uint>();      // program address -> 24-bit word
        public Dictionary<uint, uint> Skipped = new Dictionary<uint, uint>();    // config / DEVID words not programmed
        public uint Length;            // address units from AppBase
        public byte[] Flash;           // 3 bytes per word, AppBase .. AppBase+Length
        public uint Crc;
        public List<string> Warnings = new List<string>();
        public List<string> Notes = new List<string>();

        public uint LowAddress { get { uint m = uint.MaxValue; foreach (uint a in Words.Keys) if (a < m) m = a; return m; } }
        public uint HighAddress { get { uint m = 0; foreach (uint a in Words.Keys) if (a > m) m = a; return m; } }
        public int UsedPercent { get { return (int)((ulong)Length * 100 / (Proto.SigBase - Proto.AppBase)); } }

        // ------------------------------------------------------------------ loading

        public static AppImage Load(string path, InputFormat fmt, uint binBase)
        {
            if (!File.Exists(path)) throw new BlException("no such file: " + path);
            string ext = Path.GetExtension(path).ToLowerInvariant();

            if (ext == ".elf" || ext == ".cof")
            {
                // MPLAB X post-build macros point at the ELF; bin2hex writes the .hex right next to it.
                string hex = Path.ChangeExtension(path, ".hex");
                if (!File.Exists(hex))
                    throw new BlException("an ELF was given and there is no HEX next to it (" + hex + "). " +
                                          "Build the production image, or pass the .hex file.");
                path = hex; ext = ".hex";
            }

            AppImage img = new AppImage();
            img.SourcePath = path;
            if (fmt == InputFormat.Auto) fmt = (ext == ".bin") ? DetectBin(path) : InputFormat.Hex;

            if (fmt == InputFormat.Hex) { img.ParseHex(path); img.FormatUsed = "Intel HEX"; }
            else
            {
                img.ParseBin(path, fmt == InputFormat.BinWord4, binBase);
                img.FormatUsed = fmt == InputFormat.BinWord4 ? "BIN, 4 bytes/word" : "BIN, 3 bytes/word (packed)";
                if (binBase != Proto.AppBase) img.Notes.Add("BIN base address 0x" + binBase.ToString("X6"));
            }
            img.Build();
            img.CheckSkipped();
            return img;
        }

        // .bin with a zero every 4th byte is the phantom-byte layout; anything else must be packed.
        static InputFormat DetectBin(string path)
        {
            byte[] b = File.ReadAllBytes(path);
            if (b.Length == 0) throw new BlException(path + " is empty");
            if (b.Length % 4 == 0)
            {
                bool phantom = true;
                for (int i = 3; i < b.Length; i += 4) if (b[i] != 0) { phantom = false; break; }
                if (phantom && b.Length % 3 != 0) return InputFormat.BinWord4;
                if (phantom && b.Length >= 64) return InputFormat.BinWord4;
            }
            if (b.Length % 3 != 0)
                throw new BlException(path + ": " + b.Length + " bytes is neither whole 3-byte nor 4-byte words; " +
                                      "choose the BIN format explicitly");
            return InputFormat.BinPacked;
        }

        void ParseHex(string path)
        {
            uint baseAddr = 0;
            int lineno = 0;
            foreach (string raw in File.ReadAllLines(path))
            {
                lineno++;
                string line = raw.Trim();
                if (line.Length == 0) continue;
                if (line[0] != ':') throw new BlException(path + ":" + lineno + ": not an Intel HEX record");
                if ((line.Length - 1) % 2 != 0) throw new BlException(path + ":" + lineno + ": odd number of hex digits");
                byte[] rec = new byte[(line.Length - 1) / 2];
                try { for (int i = 0; i < rec.Length; i++) rec[i] = Convert.ToByte(line.Substring(1 + 2 * i, 2), 16); }
                catch (FormatException) { throw new BlException(path + ":" + lineno + ": record is not valid hex"); }
                if (rec.Length < 5) throw new BlException(path + ":" + lineno + ": record is too short");
                int count = rec[0], offset = (rec[1] << 8) | rec[2], type = rec[3];
                if (rec.Length != 5 + count) throw new BlException(path + ":" + lineno + ": record length does not match its contents");
                int sum = 0; foreach (byte x in rec) sum += x;
                if ((sum & 0xFF) != 0) throw new BlException(path + ":" + lineno + ": record checksum is wrong");

                if (type == 0x01) break;
                if (type == 0x04) { baseAddr = (uint)((rec[4] << 8) | rec[5]) << 16; continue; }
                if (type == 0x02) { baseAddr = (uint)((rec[4] << 8) | rec[5]) << 4; continue; }
                if (type != 0x00) continue;
                if (count % 4 != 0)
                    throw new BlException(path + ":" + lineno + ": " + count + " data bytes is not a whole number of instruction words");

                uint byteAddr = baseAddr + (uint)offset;
                for (int i = 0; i < count; i += 4)
                {
                    if (rec[4 + i + 3] != 0)
                        throw new BlException(string.Format("{0}:{1}: phantom byte is 0x{2:X2} at HEX address 0x{3:X6} - " +
                            "this is not a dsPIC HEX file", path, lineno, rec[4 + i + 3], byteAddr + i));
                    uint prog = (uint)((byteAddr + i) / 2);
                    uint val = (uint)(rec[4 + i] | (rec[5 + i] << 8) | (rec[6 + i] << 16));
                    Dictionary<uint, uint> target = (prog >= Proto.CfgBase) ? Skipped : Words;
                    uint old;
                    if (target.TryGetValue(prog, out old) && old != val)
                        throw new BlException(string.Format("{0}:{1}: address 0x{2:X6} is given twice with different values", path, lineno, prog));
                    target[prog] = val;
                }
            }
            if (Words.Count == 0) throw new BlException(path + " contains no program data");
        }

        void ParseBin(string path, bool word4, uint binBase)
        {
            if ((binBase & 1) != 0) throw new BlException("BIN base address must be even");
            byte[] b = File.ReadAllBytes(path);
            int step = word4 ? 4 : 3;
            if (b.Length == 0 || b.Length % step != 0)
                throw new BlException(path + ": " + b.Length + " bytes is not a whole number of " + step + "-byte words");
            for (int i = 0, n = 0; i < b.Length; i += step, n++)
            {
                if (word4 && b[i + 3] != 0)
                    throw new BlException(string.Format("{0}: byte {1} (phantom) is 0x{2:X2}, not 0 - this is not a 4-byte/word BIN", path, i + 3, b[i + 3]));
                uint val = (uint)(b[i] | (b[i + 1] << 8) | (b[i + 2] << 16));
                uint prog = binBase + (uint)(2 * n);
                if (val == Proto.Blank) continue;          // erased: the erase already left it so
                if (prog >= Proto.CfgBase) Skipped[prog] = val; else Words[prog] = val;
            }
            if (Words.Count == 0) throw new BlException(path + " contains only erased (0xFFFFFF) words");
        }

        void Build()
        {
            uint lo = LowAddress, hi = HighAddress;
            if (lo < Proto.AppBase)
                throw new BlException(string.Format(
                    "the application places code at 0x{0:X6}, below the application base 0x{1:X6}.\n" +
                    "It was linked with the stock linker script. For the bootloader it must be linked with\n" +
                    "p33CK256MC005-app.gld and the linker option --no-ivt (MPLAB X: add the .gld to 'Linker Files';\n" +
                    "Arduino: Tools > Bootloader > Serial).", lo, Proto.AppBase));
            if (hi + 2 > Proto.SigBase)
                throw new BlException(string.Format("the application reaches 0x{0:X6}, at or past the signature row 0x{1:X6} " +
                    "(the last 0x700 addresses below the config page are reserved)", hi + 2, Proto.SigBase));
            if (lo != Proto.AppBase)
                Warnings.Add(string.Format("lowest word is 0x{0:X6}, not the entry point 0x{1:X6}: the image has no entry GOTO " +
                    "and the bootloader would jump into erased flash", lo, Proto.AppBase));
            else
            {
                uint w0, w1;   // the entry must be a GOTO (opcode 0x04) followed by the high address word
                if (!Words.TryGetValue(Proto.AppBase, out w0) || !Words.TryGetValue(Proto.AppBase + 2, out w1) ||
                    (w0 >> 16) != 0x04 || (w1 & 0xFFFF80) != 0)
                    Warnings.Add("the word at 0x001800 is not a GOTO: was the application linked with p33CK256MC005-app.gld?");
            }

            Length = (hi + 2) - Proto.AppBase;
            Flash = new byte[Length / 2 * 3];
            for (uint a = Proto.AppBase, i = 0; a < Proto.AppBase + Length; a += 2, i += 3)
            {
                uint w;
                if (!Words.TryGetValue(a, out w)) w = Proto.Blank;
                Flash[i] = (byte)w; Flash[i + 1] = (byte)(w >> 8); Flash[i + 2] = (byte)(w >> 16);
            }
            Crc = Proto.Crc32(Flash, 0, Flash.Length);
        }

        // Config words belong to the bootloader. Report every one that differs, decoded, because an application
        // that expects e.g. FNOSC=FRCPLL at reset will instead start on the bootloader's FRC.
        void CheckSkipped()
        {
            List<uint> cfg = new List<uint>();
            int devid = 0;
            foreach (uint a in Skipped.Keys) if (a < Proto.DevIdSpace) cfg.Add(a); else devid++;
            cfg.Sort();
            if (devid > 0) Notes.Add(devid + " word(s) in the DEVID/OTP space are not programmed");
            if (cfg.Count == 0) return;
            int diff = 0;
            foreach (uint a in cfg)
            {
                uint mine = Skipped[a], bl = Proto.BootloaderConfig(a);
                if (mine == bl) continue;
                diff++;
                string d1 = Proto.DecodeConfig(a, mine), d2 = Proto.DecodeConfig(a, bl);
                Warnings.Add(string.Format("config {0}: project sets 0x{1:X6}{2}, the board keeps the bootloader's 0x{3:X6}{4}",
                    Proto.ConfigName(a), mine, d1 == "" ? "" : " (" + d1 + ")", bl, d2 == "" ? "" : " (" + d2 + ")"));
            }
            Notes.Add(cfg.Count + " config word(s) in the file are not written (the bootloader owns them)" +
                      (diff == 0 ? "; all match the bootloader's" : "; " + diff + " differ - see warnings"));
        }

        // ------------------------------------------------------------------ blocks for WRITE_ROW

        public struct Block { public uint Addr; public byte[] Data; }

        public List<Block> Blocks()
        {
            List<Block> list = new List<Block>();
            int step = Proto.BlockWords * 2;               // address units per block (multiple of 4: aligned)
            for (uint off = 0; off < Length; off += (uint)step)
            {
                uint n = Math.Min((uint)step, Length - off);
                int b0 = (int)(off / 2 * 3), len = (int)(n / 2 * 3);
                bool blank = true;
                for (int i = 0; i < len; i++) if (Flash[b0 + i] != 0xFF) { blank = false; break; }
                if (blank) continue;                       // erase already left it so; READ_CRC checks it anyway
                Block blk = new Block();
                blk.Addr = Proto.AppBase + off;
                blk.Data = new byte[len];
                Buffer.BlockCopy(Flash, b0, blk.Data, 0, len);
                list.Add(blk);
            }
            return list;
        }

        // ------------------------------------------------------------------ output files

        // Packed BIN: exactly the bytes the device CRC covers, starting at 0x001800.
        public void WriteBin(string path, bool word4)
        {
            if (!word4) { File.WriteAllBytes(path, Flash); return; }
            byte[] o = new byte[Flash.Length / 3 * 4];
            for (int i = 0, j = 0; i < Flash.Length; i += 3, j += 4)
            { o[j] = Flash[i]; o[j + 1] = Flash[i + 1]; o[j + 2] = Flash[i + 2]; }
            File.WriteAllBytes(path, o);
        }

        // The six signature words COMMIT writes at 0x02B700 (layout of bl_config.h).
        public Dictionary<uint, uint> SignatureWords()
        {
            Dictionary<uint, uint> s = new Dictionary<uint, uint>();
            s[Proto.SigBase + 0x0] = Length;
            s[Proto.SigBase + 0x2] = Crc & 0xFFFFFF;
            s[Proto.SigBase + 0x4] = (Crc >> 24) & 0xFF;
            s[Proto.SigBase + 0x6] = 0x000001;
            s[Proto.SigBase + 0x8] = 0x4B4333;
            s[Proto.SigBase + 0xA] = ~0x4B4333u & 0xFFFFFF;
            return s;
        }

        // Factory image for a PICkit/ICD: bootloader + application + signature, so the board boots straight into
        // the application and can still be updated over serial afterwards.
        public void WriteCombinedHex(string bootloaderHex, string outPath)
        {
            AppImage bl = new AppImage();
            bl.ParseHexAll(bootloaderHex);
            SortedDictionary<uint, uint> all = new SortedDictionary<uint, uint>();
            foreach (KeyValuePair<uint, uint> kv in bl.Words)
            {
                if (kv.Key >= Proto.AppBase && kv.Key < Proto.AppEnd)
                    throw new BlException("the bootloader HEX has data inside the application region at 0x" + kv.Key.ToString("X6"));
                all[kv.Key] = kv.Value;
            }
            if (!all.ContainsKey(0x000000)) throw new BlException(bootloaderHex + " has no reset vector - is it the bootloader?");
            foreach (KeyValuePair<uint, uint> kv in Words) all[kv.Key] = kv.Value;
            foreach (KeyValuePair<uint, uint> kv in SignatureWords()) all[kv.Key] = kv.Value;
            WriteHex(outPath, all);
        }

        void ParseHexAll(string path)       // every word, config included (for the bootloader file)
        {
            ParseHex(path);
            foreach (KeyValuePair<uint, uint> kv in Skipped) Words[kv.Key] = kv.Value;
            Skipped.Clear();
        }

        static void WriteHex(string path, SortedDictionary<uint, uint> words)
        {
            StringBuilder sb = new StringBuilder();
            uint curUpper = 0xFFFFFFFF;
            List<uint> keys = new List<uint>(words.Keys);
            int k = 0;
            while (k < keys.Count)
            {
                uint start = keys[k];
                int n = 1;                                    // up to 4 consecutive words = 16 bytes per record
                while (n < 4 && k + n < keys.Count && keys[k + n] == start + 2 * (uint)n &&
                       ((start + 2 * (uint)n) * 2 >> 16) == (start * 2 >> 16)) n++;
                uint byteAddr = start * 2;
                if ((byteAddr >> 16) != curUpper)
                {
                    curUpper = byteAddr >> 16;
                    sb.Append(Record(0x04, 0, new byte[] { (byte)(curUpper >> 8), (byte)curUpper }));
                }
                byte[] d = new byte[n * 4];
                for (int i = 0; i < n; i++)
                {
                    uint w = words[keys[k + i]];
                    d[4 * i] = (byte)w; d[4 * i + 1] = (byte)(w >> 8); d[4 * i + 2] = (byte)(w >> 16); d[4 * i + 3] = 0;
                }
                sb.Append(Record(0x00, (int)(byteAddr & 0xFFFF), d));
                k += n;
            }
            sb.Append(":00000001FF\r\n");
            File.WriteAllText(path, sb.ToString(), Encoding.ASCII);
        }

        static string Record(int type, int offset, byte[] d)
        {
            int sum = d.Length + (offset >> 8) + (offset & 0xFF) + type;
            StringBuilder s = new StringBuilder();
            s.AppendFormat(":{0:X2}{1:X4}{2:X2}", d.Length, offset, type);
            foreach (byte b in d) { s.AppendFormat("{0:X2}", b); sum += b; }
            s.AppendFormat("{0:X2}\r\n", (-sum) & 0xFF);
            return s.ToString();
        }

        public string Summary()
        {
            return string.Format("{0}: {1}, image 0x{2:X6}-0x{3:X6}, {4} instruction words ({5}% of the application area), " +
                "{6} block(s) to write, CRC-32 0x{7:X8}", Path.GetFileName(SourcePath), FormatUsed, Proto.AppBase,
                Proto.AppBase + Length, Length / 2, UsedPercent, Blocks().Count, Crc);
        }
    }
}
