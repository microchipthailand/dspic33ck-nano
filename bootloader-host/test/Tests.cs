// Tests.cs - host-side tests against the bootloader emulator and real HEX files.
// usage: NanoBootloaderTests.exe <app.hex> <bootloader.hex> <workdir>
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace NanoBootloader
{
    public static class Tests
    {
        static int pass, fail;
        static void Check(string name, bool ok, string detail)
        {
            if (ok) pass++; else fail++;
            Console.WriteLine((ok ? "PASS  " : "FAIL  ") + name + (detail == null || detail.Length == 0 ? "" : "  -- " + detail));
        }
        static void Check(string name, bool ok) { Check(name, ok, null); }

        static string Expect(Action a)        // returns the BlException message, or null if none was thrown
        {
            try { a(); return null; } catch (BlException e) { return e.Message; }
        }

        static bool FlashMatches(Emulator emu, AppImage img)
        {
            for (uint a = Proto.AppBase, i = 0; a < Proto.AppBase + img.Length; a += 2, i += 3)
            {
                uint w = (uint)(img.Flash[i] | (img.Flash[i + 1] << 8) | (img.Flash[i + 2] << 16));
                if (emu.Flash[a / 2] != w) return false;
            }
            for (uint a = Proto.AppBase + img.Length; a < Proto.SigBase; a += 2) if (emu.Flash[a / 2] != 0xFFFFFF) return false;
            return true;
        }

        static UploadOptions Fast()
        {
            UploadOptions o = new UploadOptions(); o.SoftEntryWindowMs = 50; return o;
        }

        // Synthetic dsPIC HEX: words at program addresses, 4 bytes per word, byte address = 2 x program address.
        static string HexOf(Dictionary<uint, uint> words, string path)
        {
            StringBuilder sb = new StringBuilder();
            uint upper = 0xFFFFFFFF;
            foreach (KeyValuePair<uint, uint> kv in words)
            {
                uint ba = kv.Key * 2;
                if ((ba >> 16) != upper)
                {
                    upper = ba >> 16;
                    byte[] u = { 2, 0, 0, 4, (byte)(upper >> 8), (byte)upper };
                    sb.Append(Rec(u));
                }
                byte[] r = { 4, (byte)(ba >> 8), (byte)ba, 0, (byte)kv.Value, (byte)(kv.Value >> 8), (byte)(kv.Value >> 16), 0 };
                sb.Append(Rec(r));
            }
            sb.Append(":00000001FF\r\n");
            File.WriteAllText(path, sb.ToString());
            return path;
        }
        static string Rec(byte[] r)
        {
            int s = 0; StringBuilder b = new StringBuilder(":");
            foreach (byte x in r) { b.AppendFormat("{0:X2}", x); s += x; }
            b.AppendFormat("{0:X2}\r\n", (-s) & 0xFF);
            return b.ToString();
        }

        public static int Main(string[] args)
        {
            string appHex = args[0], blHex = args[1], work = args[2];
            Directory.CreateDirectory(work);

            Check("CRC self-test and soft-entry magic", Expect(delegate { Proto.SelfCheck(); }) == null);

            AppImage img = AppImage.Load(appHex, InputFormat.Auto, Proto.AppBase);
            Console.WriteLine("      " + img.Summary());
            foreach (string w in img.Warnings) Console.WriteLine("      warning: " + w);
            foreach (string n in img.Notes) Console.WriteLine("      note: " + n);
            Check("real HEX: entry GOTO at 0x1800, no warnings", img.LowAddress == Proto.AppBase && img.Warnings.Count == 0);

            // 1. full upload from a running application, soft entry at the app's 9600 baud
            Emulator emu = new Emulator(); emu.AppBaud = 9600;
            Uploader up = new Uploader(emu);
            string err = Expect(delegate { up.Program(img, Fast()); });
            Check("upload via soft entry at 9600 baud", err == null, err);
            Check("  flash equals the image, rest of the area erased", FlashMatches(emu, img));
            Check("  signature valid, application started", emu.AppValid() && !emu.InBootloader && emu.Jumps == 1 && emu.Resets == 1);
            Check("  no programming on un-erased flash", !emu.WriteOnUnerased);

            // 2. second upload over an existing application (erase must happen first)
            err = Expect(delegate { new Uploader(emu).Program(img, Fast()); });
            Check("re-upload over a valid application", err == null && !emu.WriteOnUnerased && emu.AppValid(), err);

            // 3. lost and corrupted responses are retried
            Emulator e3 = new Emulator(); e3.InBootloader = true;
            e3.FaultsFromFrame = 4;                 // after SYNC, ERASE and two writes
            e3.CorruptResponses = 2; e3.DropResponses = 1;
            Uploader u3 = new Uploader(e3);
            err = Expect(delegate { UploadOptions o = Fast(); o.SoftEntry = false; u3.Program(img, o); });
            Check("lost/corrupted write answers: no flash word programmed twice", err == null && FlashMatches(e3, img) && e3.AppValid()
                  && !e3.WriteOnUnerased, err);

            // 3a. lost REQUEST (never executed): the block reads back erased and must be sent again
            Emulator e3a = new Emulator(); e3a.InBootloader = true;
            e3a.FaultsFromFrame = 5; e3a.DropRequests = 1;
            err = Expect(delegate { UploadOptions o = Fast(); o.SoftEntry = false; new Uploader(e3a).Program(img, o); });
            Check("lost write request: block found erased and re-sent", err == null && FlashMatches(e3a, img) && e3a.AppValid() && !e3a.WriteOnUnerased, err);

            // 3b. lost COMMIT answer: the signature row must not be written twice either
            Emulator e3b = new Emulator(); e3b.InBootloader = true;
            Uploader u3b = new Uploader(e3b);
            e3b.FaultsFromFrame = 2 + img.Blocks().Count + 1;     // SYNC, ERASE, writes, READ_CRC -> next is COMMIT
            e3b.DropResponses = 1;
            err = Expect(delegate { UploadOptions o = Fast(); o.SoftEntry = false; u3b.Program(img, o); });
            Check("lost COMMIT answer: detected as done, signature written once", err == null && e3b.AppValid() && !e3b.WriteOnUnerased, err);

            // 4. verify: equal, then one flipped word -> mismatch, nothing written
            Emulator e4 = new Emulator(); e4.InBootloader = true;
            new Uploader(e4).Program(img, Fast());
            bool same = false, diff = true;
            err = Expect(delegate { same = new Uploader(e4).Verify(img, Fast()); });
            uint mid = Proto.AppBase + (img.Length / 4 & ~3u);
            e4.Flash[mid / 2] ^= 0x000100; e4.InBootloader = true;
            string err2 = Expect(delegate { diff = new Uploader(e4).Verify(img, Fast()); });
            Check("verify: match, then mismatch after one flipped bit", err == null && err2 == null && same && !diff,
                  "err=" + err + " err2=" + err2 + " same=" + same + " diff=" + diff);

            // 5. app that does not sniff the magic: no answer, clear message mentioning manual entry
            Emulator e5 = new Emulator(); e5.AppSniffsMagic = false;
            err = Expect(delegate { new Uploader(e5).Program(img, Fast()); });
            Check("no soft entry possible -> helpful error", err != null && err.Contains("USER") && e5.Frames == 0, err);

            // 6. soft entry disabled but board waiting in the bootloader (fresh board)
            Emulator e6 = new Emulator(); e6.InBootloader = true;
            UploadOptions o6 = Fast(); o6.SoftEntry = false; o6.Run = false;
            err = Expect(delegate { new Uploader(e6).Program(img, o6); });
            Check("--no-soft-entry --no-run on a waiting board", err == null && e6.AppValid() && e6.InBootloader && e6.Jumps == 0, err);

            // 7. BIN round trips (packed and 4-byte), auto-detected, upload gives the same flash
            string b3 = Path.Combine(work, "app3.bin"), b4 = Path.Combine(work, "app4.bin");
            img.WriteBin(b3, false); img.WriteBin(b4, true);
            AppImage i3 = AppImage.Load(b3, InputFormat.Auto, Proto.AppBase), i4 = AppImage.Load(b4, InputFormat.Auto, Proto.AppBase);
            Check("BIN 3-byte round trip (auto-detected)", i3.Crc == img.Crc && i3.Length == img.Length && i3.FormatUsed.Contains("3 bytes"), i3.FormatUsed);
            Check("BIN 4-byte round trip (auto-detected)", i4.Crc == img.Crc && i4.Length == img.Length && i4.FormatUsed.Contains("4 bytes"), i4.FormatUsed);
            Emulator e7 = new Emulator(); e7.InBootloader = true;
            err = Expect(delegate { new Uploader(e7).Program(i3, Fast()); });
            Check("upload from BIN", err == null && FlashMatches(e7, img), err);

            // 8. factory HEX: bootloader + app + signature; re-parse and check the signature the bootloader will test
            string fac = Path.Combine(work, "factory.hex");
            img.WriteCombinedHex(blHex, fac);
            Emulator e8 = new Emulator();
            for (int i = 0; i < e8.Flash.Length; i++) e8.Flash[i] = 0xFFFFFF;
            int cfgCount = 0;
            foreach (string line in File.ReadAllLines(fac)) { }
            Dictionary<uint, uint> facWords = ReadAll(fac);
            foreach (KeyValuePair<uint, uint> kv in facWords) { if (kv.Key < 0x2C000) e8.Flash[kv.Key / 2] = kv.Value; if (kv.Key >= Proto.CfgBase) cfgCount++; }
            Dictionary<uint, uint> blWords = ReadAll(blHex);
            bool blKept = true;
            foreach (KeyValuePair<uint, uint> kv in blWords) { uint v; if (!facWords.TryGetValue(kv.Key, out v) || v != kv.Value) blKept = false; }
            Check("factory HEX: bootloader words and config kept, signature valid", blKept && e8.AppValid() && cfgCount == 4,
                  "config words " + cfgCount);

            // 9. config words that differ from the bootloader's are reported, decoded
            Dictionary<uint, uint> sw = new Dictionary<uint, uint>();
            sw[0x1800] = 0x040000 | 0x1B24; sw[0x1802] = 0; sw[0x1B24] = 0x000000;   // GOTO 0x1B24 ; nop
            sw[0x2BF18] = 0xFFFF79;                                                   // FNOSC = FRCPLL
            sw[0x2BF20] = 0xFFFFEF;                                                   // FWDTEN = hardware
            AppImage i9 = AppImage.Load(HexOf(sw, Path.Combine(work, "cfg.hex")), InputFormat.Auto, Proto.AppBase);
            string all = string.Join(" | ", i9.Warnings.ToArray());
            Check("differing config words are warned about, decoded", all.Contains("FNOSC=FRCPLL") && all.Contains("FWDTEN=ON (hardware)") && i9.Warnings.Count == 2, all);

            // 10. an image linked with the stock script (code at 0x200) is refused with a pointer to the fix
            Dictionary<uint, uint> st = new Dictionary<uint, uint>(); st[0x0000] = 0x040200; st[0x0002] = 0; st[0x0200] = 0;
            err = Expect(delegate { AppImage.Load(HexOf(st, Path.Combine(work, "stock.hex")), InputFormat.Auto, Proto.AppBase); });
            Check("stock-linked image refused, message names the app linker script", err != null && err.Contains("p33CK256MC005-app.gld"), err);

            // 11. image that runs into the signature row is refused
            Dictionary<uint, uint> big = new Dictionary<uint, uint>(); big[0x1800] = 0x041B24; big[0x1802] = 0; big[0x2B700] = 0;
            err = Expect(delegate { AppImage.Load(HexOf(big, Path.Combine(work, "big.hex")), InputFormat.Auto, Proto.AppBase); });
            Check("image overlapping the signature row refused", err != null && err.Contains("signature"), err);

            // 12. corrupt HEX record checksum is refused
            string bad = Path.Combine(work, "bad.hex");
            string[] lines = File.ReadAllLines(appHex);
            lines[3] = lines[3].Substring(0, lines[3].Length - 2) + (lines[3].EndsWith("00") ? "01" : "00");
            File.WriteAllLines(bad, lines);
            err = Expect(delegate { AppImage.Load(bad, InputFormat.Auto, Proto.AppBase); });
            Check("HEX record checksum error detected", err != null && err.Contains("checksum"), err);

            // 13. ELF path resolves to the sibling HEX (MPLAB X ${ImagePath})
            string elf = Path.Combine(work, "proj.production.elf"), hexSib = Path.Combine(work, "proj.production.hex");
            File.WriteAllText(elf, "not really an elf");
            File.Copy(appHex, hexSib, true);
            AppImage i13 = AppImage.Load(elf, InputFormat.Auto, Proto.AppBase);
            Check("ELF path uses the .hex next to it", i13.Crc == img.Crc && i13.SourcePath.EndsWith(".hex"));

            Console.WriteLine();
            Console.WriteLine(pass + " passed, " + fail + " failed");
            return fail == 0 ? 0 : 1;
        }

        static Dictionary<uint, uint> ReadAll(string hex)
        {
            Dictionary<uint, uint> d = new Dictionary<uint, uint>();
            uint b = 0;
            foreach (string l in File.ReadAllLines(hex))
            {
                if (l.Length < 11) continue;
                byte[] r = new byte[(l.Length - 1) / 2];
                for (int i = 0; i < r.Length; i++) r[i] = Convert.ToByte(l.Substring(1 + 2 * i, 2), 16);
                if (r[3] == 4) { b = (uint)((r[4] << 8) | r[5]) << 16; continue; }
                if (r[3] != 0) continue;
                uint ba = b + (uint)((r[1] << 8) | r[2]);
                for (int i = 0; i < r[0]; i += 4) d[(ba + (uint)i) / 2] = (uint)(r[4 + i] | (r[5 + i] << 8) | (r[6 + i] << 16));
            }
            return d;
        }
    }
}
