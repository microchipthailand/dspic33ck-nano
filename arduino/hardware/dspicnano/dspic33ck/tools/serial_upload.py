#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
serial_upload.py - upload a sketch to a dsPIC33CK serial bootloader.

Standard library only. The serial port is opened through the Win32 API with
ctypes rather than pyserial, because the platform is Windows-only anyway and the
first-run experience should not depend on `pip install`. Nothing else here is
platform-specific: only the SerialPort class at the bottom of the I/O section.

Two things in this file have historically been got wrong in dsPIC host tools and
are therefore spelled out:

  * HEX ENCODING. A dsPIC Intel HEX record holds FOUR bytes per 24-bit
    instruction word - low, middle, high, and a fourth "phantom" byte that is
    always 0x00 - and the record address is TWICE the program-memory address.
    A tool that walks the record bytewise, as the deleted tools/upload_uart.py
    did, silently programs a quarter of the image as padding. Every phantom byte
    is asserted to be 0x00 here; if one is not, the assumption is wrong and the
    tool stops rather than guessing.

  * CRC-32 DEFINITION. The device checksums FLASH, not the HEX file, so the host
    has to build the identical byte stream: three bytes per instruction word,
    little-endian within the word, phantom byte excluded, and every gap inside
    the image span filled with 0xFFFFFF because that is what erased flash reads
    as. bl_crc.h carries the same paragraph on the firmware side.

Usage:
    serial_upload.py --port COM7 --hex sketch.hex
    serial_upload.py --hex sketch.hex --dry-run      (parse and check, no board)

Exit status is 0 only if the image was written, verified by the device's own
CRC-32, and committed.
"""

from __future__ import print_function

import argparse
import os
import sys
import time

# ============================================================================
# Protocol and geometry -- the mirror of bl_config.h
#
# These are asked for, not assumed: SYNC returns the device's own view of the
# geometry and check_geometry() below refuses to continue if it disagrees with
# what is written here. The constants exist so that a --dry-run has something to
# check against without a board attached.
# ============================================================================

PROTO_VERSION = 1

APP_BASE = 0x001800
SIG_BASE = 0x02B700
APP_END  = 0x02B800
CFGMEM   = 0x02BF00          # config words: never written over serial
DEVID_SPACE = 0x800000       # DEVID/OTP/debug space in the HEX, never programmed

WIRE_BLOCK_WORDS = 128       # instruction words per WRITE_ROW frame
DWORD_ALIGN = 4              # WRITE_ROW addresses must be double-word aligned
BLANK = 0xFFFFFF             # what erased flash reads as

SOH = 0x01
ACK = 0x06
NAK = 0x15

CMD_SYNC      = 0x10
CMD_ERASE_APP = 0x20
CMD_WRITE_ROW = 0x30
CMD_READ_CRC  = 0x40
CMD_COMMIT    = 0x50
CMD_JUMP      = 0x60

ERRORS = {
    0x01: "frame CRC mismatch",
    0x02: "unknown command",
    0x03: "wrong payload length",
    0x04: "address outside the application region",
    0x05: "address not double-word aligned",
    0x06: "the flash write or erase did not take (WRERR)",
    0x07: "no valid application: signature missing or CRC mismatch",
}

# See BL_SOFT_ENTRY_MAGIC in bl_config.h. The last two bytes are the CRC-16 of
# the first eight; assert_soft_entry_magic() checks that below, so editing the
# sequence here without recomputing the CRC fails immediately instead of silently
# never matching.
SOFT_ENTRY_MAGIC = bytearray([0x1B, 0xF0, 0x33, 0x43, 0x4B, 0x21,
                              0x9E, 0x57, 0xE8, 0x3B])

BAUD = 115200

# Baud rates tried for soft entry, in order. The bootloader itself only ever
# speaks 115200, but the MAGIC has to be understood by the RUNNING SKETCH, which
# is at whatever rate its Serial.begin() chose. Trying a handful costs about a
# second in the case where the first guess was right, and turns "upload only
# works if your sketch happens to use 115200" into a non-issue.
SOFT_ENTRY_BAUDS = [115200, 9600, 57600, 38400, 19200, 250000]


# ============================================================================
# Checksums
# ============================================================================

def crc16(data):
    """CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no final xor."""
    crc = 0xFFFF
    for b in bytearray(data):
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


_CRC32_TABLE = []
for _i in range(256):
    _c = _i
    for _ in range(8):
        _c = (_c >> 1) ^ 0xEDB88320 if _c & 1 else _c >> 1
    _CRC32_TABLE.append(_c)


def crc32(data):
    """The IEEE/zip CRC-32, matching bl_crc.c's nibble-table implementation."""
    crc = 0xFFFFFFFF
    for b in bytearray(data):
        crc = (crc >> 8) ^ _CRC32_TABLE[(crc ^ b) & 0xFF]
    return crc ^ 0xFFFFFFFF


def assert_soft_entry_magic():
    body, tail = SOFT_ENTRY_MAGIC[:8], SOFT_ENTRY_MAGIC[8:]
    want = crc16(body)
    got = tail[0] | (tail[1] << 8)
    if got != want:
        raise Fault("the soft-entry magic's own CRC-16 is wrong: have 0x%04X, "
                    "computed 0x%04X. Fix the constant here and the matching one "
                    "in bl_config.h and HardwareSerial.c." % (got, want))


class Fault(Exception):
    """Anything that should stop the upload with a message and no traceback."""


# ============================================================================
# Intel HEX
# ============================================================================

def parse_hex(path):
    """
    Read a dsPIC Intel HEX file.

    Returns (words, skipped) where `words` maps a PROGRAM-memory address (even,
    two address units per instruction word) to a 24-bit value, and `skipped` maps
    the same for every record deliberately not programmed - config words and the
    DEVID/OTP space. The caller reports those rather than discarding them
    silently: a config word whose value is not what the bootloader already
    programmed is a real problem, just not one to fix by writing it.
    """
    words = {}
    skipped = {}
    base = 0
    lineno = 0

    with open(path, "r") as fh:
        for raw in fh:
            lineno += 1
            line = raw.strip()
            if not line:
                continue
            if not line.startswith(":"):
                raise Fault("%s:%d: not an Intel HEX record" % (path, lineno))

            try:
                rec = bytearray.fromhex(line[1:])
            except ValueError:
                raise Fault("%s:%d: record is not valid hex" % (path, lineno))
            if len(rec) < 5:
                raise Fault("%s:%d: record is too short" % (path, lineno))

            count, offset, rtype = rec[0], (rec[1] << 8) | rec[2], rec[3]
            data = rec[4:4 + count]
            if len(data) != count or len(rec) != 5 + count:
                raise Fault("%s:%d: record length %d does not match its contents"
                            % (path, lineno, count))
            if (sum(rec) & 0xFF) != 0:
                raise Fault("%s:%d: record checksum is wrong" % (path, lineno))

            if rtype == 0x01:                       # end of file
                break
            if rtype == 0x04:                       # extended linear address
                if count != 2:
                    raise Fault("%s:%d: malformed type 04 record" % (path, lineno))
                base = ((data[0] << 8) | data[1]) << 16
                continue
            if rtype == 0x02:                       # extended segment address
                if count != 2:
                    raise Fault("%s:%d: malformed type 02 record" % (path, lineno))
                base = ((data[0] << 8) | data[1]) << 4
                continue
            if rtype != 0x00:
                continue                            # 03/05: start address, no data

            if count % 4 != 0:
                raise Fault("%s:%d: %d data bytes is not a whole number of "
                            "instruction words (4 bytes each)" % (path, lineno, count))

            byte_addr = base + offset
            for i in range(0, count, 4):
                # The fourth byte of every instruction word is the phantom byte.
                # Asserting it rather than skipping it is the whole point: if it
                # is ever non-zero, this file is not what this parser thinks it
                # is, and the correct response is to stop.
                if data[i + 3] != 0x00:
                    raise Fault("%s:%d: phantom byte is 0x%02X, not 0x00, at HEX "
                                "address 0x%06X -- this is not a dsPIC HEX file, "
                                "or its encoding has changed"
                                % (path, lineno, data[i + 3], byte_addr + i))

                prog = (byte_addr + i) // 2
                value = data[i] | (data[i + 1] << 8) | (data[i + 2] << 16)
                target = skipped if (prog >= CFGMEM or prog >= DEVID_SPACE) else words

                if prog in target and target[prog] != value:
                    raise Fault("%s:%d: program address 0x%06X is given twice with "
                                "different values (0x%06X then 0x%06X)"
                                % (path, lineno, prog, target[prog], value))
                target[prog] = value

    if not words:
        raise Fault("%s contains no program data" % path)
    return words, skipped


def build_image(words):
    """
    Flatten the parsed words into the contiguous byte stream the device will hold.

    The span runs from APP_BASE to the highest word present, and any gap inside it
    is filled with 0xFFFFFF - the value the erase left behind. That makes the
    host's CRC-32 a statement about the whole region rather than about the words
    the HEX happened to mention, which is what lets READ_CRC verify the upload
    end to end.
    """
    lo, hi = min(words), max(words)

    if lo < APP_BASE:
        raise Fault("the sketch places code at 0x%06X, below the application "
                    "base 0x%06X. It was almost certainly linked with the stock "
                    "linker script instead of p33CK256MC005-app.gld -- select "
                    "Bootloader: \"Serial (UART)\" and rebuild." % (lo, APP_BASE))
    if hi + 2 > SIG_BASE:
        raise Fault("the sketch reaches 0x%06X, at or past the signature row "
                    "0x%06X" % (hi + 2, SIG_BASE))
    if lo != APP_BASE:
        warn("the sketch's lowest word is 0x%06X, not the application entry "
             "point 0x%06X; the gap will be left erased" % (lo, APP_BASE))

    length = (hi + 2) - APP_BASE                    # in address units
    image = bytearray()
    for addr in range(APP_BASE, APP_BASE + length, 2):
        w = words.get(addr, BLANK)
        image += bytearray([w & 0xFF, (w >> 8) & 0xFF, (w >> 16) & 0xFF])

    return length, image


def check_skipped(skipped):
    """
    Report what was not programmed.

    Config words are the bootloader's, not the sketch's: the application region
    deliberately stops below their erase page, so they cannot be touched over
    serial. They are also identical in every sketch this platform builds --
    _build/build_bootloader.sh asserts the bootloader emits exactly the ones
    cores/arduino/system_config.c does -- so skipping them is correct. Saying so
    out loud is what makes a future core change visible instead of mysterious.
    """
    cfg = sorted(a for a in skipped if CFGMEM <= a < DEVID_SPACE)
    other = sorted(a for a in skipped if a >= DEVID_SPACE)
    if cfg:
        info("not writing %d config word(s) at 0x%06X-0x%06X: they belong to the "
             "bootloader and are the same in every sketch"
             % (len(cfg), cfg[0], cfg[-1]))
    if other:
        info("not writing %d word(s) in the DEVID/OTP space at 0x%06X-0x%06X"
             % (len(other), other[0], other[-1]))


# ============================================================================
# Serial I/O (Win32)
# ============================================================================

if os.name == "nt":
    import ctypes
    from ctypes import wintypes

    _k32 = ctypes.WinDLL("kernel32", use_last_error=True)

    GENERIC_READ = 0x80000000
    GENERIC_WRITE = 0x40000000
    OPEN_EXISTING = 3
    INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value
    PURGE_RXCLEAR = 0x0008
    PURGE_TXCLEAR = 0x0004
    NOPARITY = 0
    ONESTOPBIT = 0
    DTR_CONTROL_ENABLE = 1
    RTS_CONTROL_ENABLE = 1

    class DCB(ctypes.Structure):
        _fields_ = [
            ("DCBlength", wintypes.DWORD),
            ("BaudRate", wintypes.DWORD),
            ("fBinary", wintypes.DWORD, 1),
            ("fParity", wintypes.DWORD, 1),
            ("fOutxCtsFlow", wintypes.DWORD, 1),
            ("fOutxDsrFlow", wintypes.DWORD, 1),
            ("fDtrControl", wintypes.DWORD, 2),
            ("fDsrSensitivity", wintypes.DWORD, 1),
            ("fTXContinueOnXoff", wintypes.DWORD, 1),
            ("fOutX", wintypes.DWORD, 1),
            ("fInX", wintypes.DWORD, 1),
            ("fErrorChar", wintypes.DWORD, 1),
            ("fNull", wintypes.DWORD, 1),
            ("fRtsControl", wintypes.DWORD, 2),
            ("fAbortOnError", wintypes.DWORD, 1),
            ("fDummy2", wintypes.DWORD, 17),
            ("wReserved", wintypes.WORD),
            ("XonLim", wintypes.WORD),
            ("XoffLim", wintypes.WORD),
            ("ByteSize", ctypes.c_ubyte),
            ("Parity", ctypes.c_ubyte),
            ("StopBits", ctypes.c_ubyte),
            ("XonChar", ctypes.c_char),
            ("XoffChar", ctypes.c_char),
            ("ErrorChar", ctypes.c_char),
            ("EofChar", ctypes.c_char),
            ("EvtChar", ctypes.c_char),
            ("wReserved1", wintypes.WORD),
        ]

    class COMMTIMEOUTS(ctypes.Structure):
        _fields_ = [
            ("ReadIntervalTimeout", wintypes.DWORD),
            ("ReadTotalTimeoutMultiplier", wintypes.DWORD),
            ("ReadTotalTimeoutConstant", wintypes.DWORD),
            ("WriteTotalTimeoutMultiplier", wintypes.DWORD),
            ("WriteTotalTimeoutConstant", wintypes.DWORD),
        ]


class SerialPort(object):
    """A blocking 8N1 serial port, opened through CreateFile/SetCommState."""

    # Read timeouts are set once, to a short constant, and every timed read loops
    # until its own deadline. Pushing the whole timeout into COMMTIMEOUTS instead
    # would make a read of N bytes wait the full time for the LAST byte, which
    # turns a 2 s command timeout into 2 s per byte on a silent port.
    _READ_SLICE_MS = 40

    def __init__(self, name, baud):
        if os.name != "nt":
            raise Fault("serial upload is implemented for Windows only; this is "
                        "os.name = %r" % os.name)

        self.name = name
        path = name if name.startswith("\\\\.\\") else "\\\\.\\" + name

        self.handle = _k32.CreateFileW(
            wintypes.LPCWSTR(path),
            wintypes.DWORD(GENERIC_READ | GENERIC_WRITE),
            wintypes.DWORD(0), None,
            wintypes.DWORD(OPEN_EXISTING),
            wintypes.DWORD(0), None)
        if self.handle == INVALID_HANDLE_VALUE or self.handle is None:
            err = ctypes.get_last_error()
            hint = ""
            if err == 5:
                hint = (" -- the port is open in something else; close the Serial "
                        "Monitor and try again")
            elif err == 2:
                hint = " -- no such port; check Tools > Port"
            raise Fault("cannot open %s: Windows error %d%s" % (name, err, hint))

        try:
            self.set_baud(baud)
            timeouts = COMMTIMEOUTS(self._READ_SLICE_MS, 0, self._READ_SLICE_MS, 0, 2000)
            if not _k32.SetCommTimeouts(self.handle, ctypes.byref(timeouts)):
                raise Fault("SetCommTimeouts failed on %s: Windows error %d"
                            % (name, ctypes.get_last_error()))
        except Exception:
            self.close()
            raise

    def set_baud(self, baud):
        dcb = DCB()
        dcb.DCBlength = ctypes.sizeof(DCB)
        if not _k32.GetCommState(self.handle, ctypes.byref(dcb)):
            raise Fault("GetCommState failed on %s: Windows error %d"
                        % (self.name, ctypes.get_last_error()))
        dcb.BaudRate = baud
        dcb.ByteSize = 8
        dcb.Parity = NOPARITY
        dcb.StopBits = ONESTOPBIT
        dcb.fBinary = 1
        dcb.fParity = 0
        dcb.fOutxCtsFlow = 0
        dcb.fOutxDsrFlow = 0
        dcb.fDsrSensitivity = 0
        dcb.fOutX = 0
        dcb.fInX = 0
        dcb.fErrorChar = 0
        dcb.fNull = 0
        dcb.fAbortOnError = 0
        # Neither line resets this target -- DBG3 drives MCLR and only the
        # debugger drives DBG3 -- but the CDC bridge expects to be asserted.
        dcb.fDtrControl = DTR_CONTROL_ENABLE
        dcb.fRtsControl = RTS_CONTROL_ENABLE
        if not _k32.SetCommState(self.handle, ctypes.byref(dcb)):
            raise Fault("SetCommState failed on %s at %d baud: Windows error %d"
                        % (self.name, baud, ctypes.get_last_error()))
        self.baud = baud

    def purge(self):
        _k32.PurgeComm(self.handle, wintypes.DWORD(PURGE_RXCLEAR | PURGE_TXCLEAR))

    def write(self, data):
        data = bytes(bytearray(data))
        written = wintypes.DWORD(0)
        if not _k32.WriteFile(self.handle, data, wintypes.DWORD(len(data)),
                              ctypes.byref(written), None):
            raise Fault("write to %s failed: Windows error %d"
                        % (self.name, ctypes.get_last_error()))
        if written.value != len(data):
            raise Fault("short write to %s: %d of %d bytes"
                        % (self.name, written.value, len(data)))

    def read(self, count, timeout_ms):
        """Read up to `count` bytes, returning early only when they all arrive."""
        out = bytearray()
        deadline = time.time() + (timeout_ms / 1000.0)
        buf = ctypes.create_string_buffer(count)
        got = wintypes.DWORD(0)

        while len(out) < count and time.time() < deadline:
            want = count - len(out)
            if not _k32.ReadFile(self.handle, buf, wintypes.DWORD(want),
                                 ctypes.byref(got), None):
                raise Fault("read from %s failed: Windows error %d"
                            % (self.name, ctypes.get_last_error()))
            if got.value:
                out += bytearray(buf.raw[:got.value])
        return out

    def close(self):
        if getattr(self, "handle", None) not in (None, INVALID_HANDLE_VALUE):
            _k32.CloseHandle(self.handle)
        self.handle = None

    def __enter__(self):
        return self

    def __exit__(self, *_exc):
        self.close()


# ============================================================================
# Framing
# ============================================================================

class Link(object):
    """One request/response exchange at a time, with retries."""

    RETRIES = 3

    def __init__(self, port, verbose=False):
        self.port = port
        self.verbose = verbose

    def _frame(self, cmd, payload):
        body = bytearray([cmd, len(payload) & 0xFF, (len(payload) >> 8) & 0xFF])
        body += bytearray(payload)
        crc = crc16(body)
        return bytearray([SOH]) + body + bytearray([crc & 0xFF, crc >> 8])

    def _read_response(self, timeout_ms):
        head = self.port.read(3, timeout_ms)
        if len(head) < 3:
            return None, "no response"

        status = head[0]
        length = head[1] | (head[2] << 8)
        rest = self.port.read(length + 2, timeout_ms)
        if len(rest) < length + 2:
            return None, "response truncated after %d of %d bytes" % (len(rest), length + 2)

        payload = rest[:length]
        got = rest[length] | (rest[length + 1] << 8)
        if got != crc16(head + payload):
            return None, "response CRC mismatch"
        if status == ACK:
            return payload, None
        if status == NAK:
            code = payload[0] if payload else 0
            return None, "the board refused it: %s (error %d)" % (
                ERRORS.get(code, "unknown error"), code)
        return None, "unexpected status byte 0x%02X" % status

    def command(self, cmd, payload=b"", timeout_ms=2000, retries=None):
        """Send one command, retrying transport failures. Raises Fault on refusal."""
        frame = self._frame(cmd, payload)
        last = "not attempted"
        attempts = self.RETRIES if retries is None else retries

        for attempt in range(attempts):
            self.port.purge()
            self.port.write(frame)
            payload_in, err = self._read_response(timeout_ms)
            if err is None:
                return payload_in
            last = err
            # A NAK is the board's considered answer, not a lost byte. Retrying
            # an out-of-range write or a failed erase just repeats it.
            if "refused it" in err:
                break
            if self.verbose and attempt + 1 < attempts:
                info("cmd 0x%02X: %s, retrying (%d/%d)"
                     % (cmd, err, attempt + 2, attempts))

        raise Fault("command 0x%02X failed: %s" % (cmd, last))


# ============================================================================
# Output
# ============================================================================

_QUIET = False


def info(msg):
    if not _QUIET:
        print("serial_upload: %s" % msg)
        sys.stdout.flush()


def always(msg):
    """
    Printed even with --quiet, which is what the IDE passes when the Verbose
    output box is unticked.

    --quiet used to silence everything, so a plain upload from the IDE printed
    the size report and then nothing at all: no confirmation, no port, no way to
    tell a successful upload from a recipe that never ran. Every other Arduino
    upload tool says something. Keep this to the one line that answers "did it
    work", and leave the rest to --verbose.
    """
    print("serial_upload: %s" % msg)
    sys.stdout.flush()


def warn(msg):
    print("serial_upload: warning: %s" % msg, file=sys.stderr)
    sys.stderr.flush()


# ============================================================================
# Upload
# ============================================================================

def sync(link, timeout_ms=500):
    """Ask the board who it is. Nothing about geometry is assumed before this."""
    p = link.command(CMD_SYNC, timeout_ms=timeout_ms, retries=1)
    if len(p) != 31 or bytes(p[0:4]) != b"33CK":
        raise Fault("SYNC answered with something that is not this bootloader")

    def u32(off):
        return (p[off] | (p[off + 1] << 8) | (p[off + 2] << 16) | (p[off + 3] << 24))

    return {
        "version": p[4],
        "devid": u32(5),
        "devrev": u32(9),
        "erase_step": u32(13),
        "block_words": p[17] | (p[18] << 8),
        "app_base": u32(19),
        "sig_base": u32(23),
        "app_end": u32(27),
    }


def check_geometry(dev):
    """
    The device's numbers win; a mismatch is a stop, not an adjustment.

    Uploading against a bootloader with a different map would write a valid-
    looking image into the wrong place, so this is deliberately not tolerant.
    """
    if dev["version"] != PROTO_VERSION:
        raise Fault("the board speaks protocol version %d, this tool speaks %d. "
                    "Re-burn the bootloader from the version of the platform you "
                    "are uploading with." % (dev["version"], PROTO_VERSION))

    for key, want in (("app_base", APP_BASE), ("sig_base", SIG_BASE),
                      ("app_end", APP_END), ("block_words", WIRE_BLOCK_WORDS)):
        if dev[key] != want:
            raise Fault("the board reports %s = 0x%X, this tool and the linker "
                        "script assume 0x%X" % (key, dev[key], want))

    info("board: DEVID 0x%06X rev 0x%06X, protocol %d, erase step 0x%X"
         % (dev["devid"], dev["devrev"], dev["version"], dev["erase_step"]))


# One knock must cost much less than the bootloader's entry window, or a single
# missed frame throws the whole window away. The window is BL_WINDOW_MS = 300 ms;
# a knock that waits 500 ms for silence leaves exactly one attempt per reset, and
# the first attempt is the one most likely to be lost, because it can be sent
# while the target is still coming out of reset with its UART not yet enabled. On
# hardware that made soft entry a coin flip -- see enter_bootloader. 60 ms fits
# about five attempts inside the window and is still 15x the 3.6 ms it takes to
# put a SYNC request and its 36-byte answer on the wire at 115200 baud.
KNOCK_MS = 60


def knock(link, timeout_ms=500):
    """The device record if a bootloader answers SYNC right now, else None."""
    try:
        return sync(link, timeout_ms=timeout_ms)
    except Fault:
        return None


def enter_bootloader(port, link, soft_entry=True, window_ms=1500):
    """
    Get the board into its command loop.

    Three ways in, cheapest first:
      1. it is already there -- a fresh board with no valid application, or SW0
         was held at power-up;
      2. the running sketch is asked to reset itself, and SYNC frames are then
         flooded into the ~300 ms window the bootloader opens at every reset;
      3. nothing worked, and the user is told what to do by hand. There is no
         reset line to fall back on: on the Curiosity Nano only the debugger can
         drive MCLR, and the board has no reset button.
    """
    info("looking for the bootloader on %s" % port.name)
    dev = knock(link)
    if dev:
        info("the bootloader is already listening")
        return dev

    if not soft_entry:
        raise Fault("no bootloader answered and soft entry is disabled")

    for baud in SOFT_ENTRY_BAUDS:
        # The magic has to be understood by the SKETCH, at the sketch's baud
        # rate; the bootloader that follows the reset always speaks 115200.
        port.set_baud(baud)
        port.purge()
        port.write(SOFT_ENTRY_MAGIC)
        port.set_baud(BAUD)

        # Flood, do not poll politely. The board is only listening for the ~300 ms
        # after the reset the magic just caused, and how long that reset takes is
        # not observable from here, so the only reliable tactic is short knocks
        # repeated across a window several times longer than the board's own.
        deadline = time.time() + (window_ms / 1000.0)
        while time.time() < deadline:
            dev = knock(link, timeout_ms=KNOCK_MS)
            if dev:
                info("the sketch reset itself into the bootloader "
                     "(magic accepted at %d baud)" % baud)
                return dev

    raise Fault(
        "no bootloader answered on %s.\n"
        "  * If this board has never had the bootloader burned, use\n"
        "    Tools > Burn Bootloader with the debugger attached first.\n"
        "  * If the sketch on the board does not use Serial, it cannot be asked\n"
        "    to reset. Hold USER, press and release RESET, keep holding USER\n"
        "    for a second (the LED stays on), then upload again.\n"
        "  * Check that Tools > Port is the J5 USB-serial adapter, that its\n"
        "    TX goes to J5-3 and RX to J5-2, and that the Serial Monitor is\n"
        "    closed." % port.name)


def blocks(length, image):
    """
    Split the image into WRITE_ROW-sized, double-word-aligned chunks.

    Blocks that are entirely 0xFFFFFF are skipped: the erase already left them
    that way, so writing them would only cost time. Correctness does not rest on
    that reasoning -- READ_CRC checks the whole span afterwards, fill included.
    """
    step = WIRE_BLOCK_WORDS * 2                     # address units per block
    assert step % DWORD_ALIGN == 0
    assert APP_BASE % DWORD_ALIGN == 0

    for off in range(0, length, step):
        n = min(step, length - off)                 # address units in this block
        chunk = image[(off // 2) * 3:((off + n) // 2) * 3]
        if all(b == 0xFF for b in bytearray(chunk)):
            continue
        yield APP_BASE + off, chunk


def upload(port, link, length, image, do_jump=True, soft_entry=True):
    dev = enter_bootloader(port, link, soft_entry=soft_entry)
    check_geometry(dev)

    want_crc = crc32(image)
    info("image 0x%06X-0x%06X, %d instruction words, CRC-32 0x%08X"
         % (APP_BASE, APP_BASE + length, length // 2, want_crc))

    # Erasing is what makes the board un-runnable until COMMIT: the signature row
    # is inside the erased range, so an interrupted upload leaves a board that
    # refuses to jump and waits for the host again - recoverable over serial with
    # no debugger. That is the reason ERASE_APP is one command and not per-block.
    info("erasing the application region")
    link.command(CMD_ERASE_APP, timeout_ms=8000)

    chunks = list(blocks(length, image))
    info("writing %d block(s)" % len(chunks))

    # Plain whole lines, and few of them. The obvious thing here is a "\r"
    # progress line, and it was one, but the only place this output is ever read
    # is the Arduino IDE console, which is line-based and does not act on a
    # carriage return -- the updates ran together as
    # "1/18 blocksserial_upload: 17/18 blocks". A piped shell shows the same. So
    # report at most ten times regardless of image size: ~10 lines for a 246 KB
    # sketch, 2 for a blink.
    step = max(1, -(-len(chunks) // 10))     # ceiling, so 18 blocks is 9 lines not 18
    for i, (addr, chunk) in enumerate(chunks):
        payload = bytearray([addr & 0xFF, (addr >> 8) & 0xFF,
                             (addr >> 16) & 0xFF, (addr >> 24) & 0xFF]) + chunk
        link.command(CMD_WRITE_ROW, payload, timeout_ms=3000)
        if i % step == 0 or i + 1 == len(chunks):
            info("  %d/%d blocks (%d%%)"
                 % (i + 1, len(chunks), (i + 1) * 100 // len(chunks)))

    info("verifying with the board's own CRC-32")
    payload = bytearray()
    for v in (APP_BASE, length):
        payload += bytearray([v & 0xFF, (v >> 8) & 0xFF,
                              (v >> 16) & 0xFF, (v >> 24) & 0xFF])
    p = link.command(CMD_READ_CRC, payload, timeout_ms=8000)
    if len(p) != 4:
        raise Fault("READ_CRC answered %d bytes, expected 4" % len(p))
    got_crc = p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24)
    if got_crc != want_crc:
        raise Fault("verify failed: the board computed 0x%08X, the image is "
                    "0x%08X. Nothing was committed, so the board is still "
                    "waiting for a working upload." % (got_crc, want_crc))
    info("verified")

    # Only now does the board have a runnable application. COMMIT re-checks the
    # same CRC itself before writing the row, so the guarantee does not depend on
    # this tool having been honest about the verify above.
    payload = bytearray()
    for v in (length, want_crc):
        payload += bytearray([v & 0xFF, (v >> 8) & 0xFF,
                              (v >> 16) & 0xFF, (v >> 24) & 0xFF])
    link.command(CMD_COMMIT, payload, timeout_ms=8000)
    always("%d instruction words written and verified on %s"
           % (length // 2, port.name))

    if do_jump:
        # JUMP acks and then runs the sketch, so there is no second response to
        # wait for and a timeout here would be a false alarm.
        try:
            link.command(CMD_JUMP, timeout_ms=1000, retries=1)
            info("running the sketch")
        except Fault as exc:
            warn("the board did not confirm the jump (%s); it will run the "
                 "sketch at the next reset" % exc)


# ============================================================================
# Command line
# ============================================================================

def main(argv=None):
    global _QUIET

    ap = argparse.ArgumentParser(
        description="Upload a sketch to a dsPIC33CK serial bootloader.")
    ap.add_argument("--port", help="serial port, e.g. COM7")
    ap.add_argument("--hex", dest="hexfile", required=True, help="sketch .hex file")
    ap.add_argument("--dry-run", action="store_true",
                    help="parse and check the HEX file; do not open a port")
    ap.add_argument("--no-jump", action="store_true",
                    help="leave the board in the bootloader after committing")
    ap.add_argument("--no-soft-entry", action="store_true",
                    help="do not ask a running sketch to reset itself")
    ap.add_argument("--quiet", action="store_true", help="only report problems")
    ap.add_argument("--verbose", action="store_true", help="report retries")
    args = ap.parse_args(argv)

    _QUIET = args.quiet

    try:
        assert_soft_entry_magic()

        if not os.path.isfile(args.hexfile):
            raise Fault("no such file: %s" % args.hexfile)
        words, skipped = parse_hex(args.hexfile)
        length, image = build_image(words)
        check_skipped(skipped)

        if args.dry_run:
            info("%s: %d word(s) of program data, image 0x%06X-0x%06X "
                 "(%d words), CRC-32 0x%08X, %d block(s) to write"
                 % (os.path.basename(args.hexfile), len(words), APP_BASE,
                    APP_BASE + length, length // 2, crc32(image),
                    len(list(blocks(length, image)))))
            return 0

        if not args.port:
            raise Fault("--port is required unless --dry-run is given")

        with SerialPort(args.port, BAUD) as port:
            link = Link(port, verbose=args.verbose)
            upload(port, link, length, image,
                   do_jump=not args.no_jump,
                   soft_entry=not args.no_soft_entry)
        return 0

    except Fault as exc:
        print("serial_upload: error: %s" % exc, file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\nserial_upload: interrupted. If it stopped during the write, the "
              "board has no valid application and is waiting for another upload.",
              file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
