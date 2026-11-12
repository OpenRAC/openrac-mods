# SPDX-License-Identifier: GPL-3.0-or-later
"""tools/binary_scan.py on small libraries made here, byte by byte, in each format."""
from __future__ import annotations

import io
import struct
import sys
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import binary_scan as s  # noqa: E402


def elf(needed: list[str], undefined: list[str], extra: bytes = b"") -> bytes:
    """A 64-bit little-endian ELF with .dynstr, .dynsym and .dynamic, nothing else."""
    strtab = b"\0"
    offsets = {}
    for n in needed + undefined:
        offsets[n] = len(strtab)
        strtab += n.encode() + b"\0"
    syms = b"\0" * 24 + b"".join(struct.pack("<IBBHQQ", offsets[n], 0x12, 0, 0, 0, 0) for n in undefined)
    dyn = b"".join(struct.pack("<qQ", 1, offsets[n]) for n in needed) + struct.pack("<qQ", 0, 0)
    body = strtab + syms + dyn + extra
    str_off = 64
    sym_off = str_off + len(strtab)
    dyn_off = sym_off + len(syms)
    shoff = 64 + len(body)
    head = b"\x7fELF" + bytes([2, 1, 1]) + b"\0" * 9
    head += struct.pack("<HHIQQQIHHHHHH", 3, 62, 1, 0, 0, shoff, 0, 64, 0, 0, 64, 4, 0)

    def sh(kind, off, size, link, entsize):
        return struct.pack("<IIQQQQIIQQ", 0, kind, 0, 0, off, size, link, 0, 8, entsize)

    shdrs = sh(0, 0, 0, 0, 0) + sh(3, str_off, len(strtab), 0, 0) + sh(11, sym_off, len(syms), 1, 24) \
        + sh(6, dyn_off, len(dyn), 1, 16)
    return head + body + shdrs


def pe(imports: dict[str, list[str]], extra: bytes = b"") -> bytes:
    """A PE32+ with one section holding its import table."""
    rva = 0x1000
    names = b""
    name_at = {}
    for dll, funcs in imports.items():
        for f in funcs:
            name_at[f] = len(names)
            names += b"\0\0" + f.encode() + b"\0"
            names += b"\0" * (len(names) % 2)
        name_at[dll] = len(names)
        names += dll.encode() + b"\0"
    desc_size = 20 * (len(imports) + 1)
    thunks = b""
    thunk_at = {}
    for dll, funcs in imports.items():
        thunk_at[dll] = len(thunks)
        thunks += b"".join(struct.pack("<Q", 0) for _ in funcs) + struct.pack("<Q", 0)
    thunk_base = rva + desc_size
    name_base = thunk_base + len(thunks)
    thunks = b""
    for dll, funcs in imports.items():
        thunks += b"".join(struct.pack("<Q", name_base + name_at[f]) for f in funcs) + struct.pack("<Q", 0)
    descs = b"".join(struct.pack("<IIIII", thunk_base + thunk_at[d], 0, 0, name_base + name_at[d], thunk_base + thunk_at[d])
                     for d in imports) + b"\0" * 20
    section = descs + thunks + names + extra
    pe_off = 0x40
    opt_size = 240
    headers_end = pe_off + 24 + opt_size + 40
    raw = (headers_end + 0x1FF) & ~0x1FF
    dos = b"MZ" + b"\0" * 58 + struct.pack("<I", pe_off)
    coff = b"PE\0\0" + struct.pack("<HHIIIHH", 0x8664, 1, 0, 0, 0, opt_size, 0x2022)
    opt = struct.pack("<H", 0x20B) + b"\0" * 110
    dirs = bytearray(16 * 8)
    struct.pack_into("<II", dirs, 8, rva, len(descs))
    opt += bytes(dirs)
    sect = b".idata\0\0" + struct.pack("<IIIIIIHHI", len(section), rva, len(section), raw, 0, 0, 0, 0, 0)
    head = dos + coff + opt + sect
    return head + b"\0" * (raw - len(head)) + section


def macho(dylibs: list[str], undefined: list[str]) -> bytes:
    """A 64-bit Mach-O with LC_LOAD_DYLIB commands and an LC_SYMTAB."""
    cmds = b""
    for d in dylibs:
        name = d.encode() + b"\0"
        size = (24 + len(name) + 7) & ~7
        cmds += struct.pack("<IIIIII", 0xC, size, 24, 0, 0, 0) + name.ljust(size - 24, b"\0")
    strtab = b"\0"
    offs = []
    for u in undefined:
        offs.append(len(strtab))
        strtab += b"_" + u.encode() + b"\0"
    head_size = 32 + len(cmds) + 24
    symoff = head_size
    stroff = symoff + 16 * len(undefined)
    cmds += struct.pack("<IIIIII", 0x2, 24, symoff, len(undefined), stroff, len(strtab))
    syms = b"".join(struct.pack("<IBBHQ", o, 0x01, 0, 0, 0) for o in offs)
    head = struct.pack("<IIIIIIII", 0xFEEDFACF, 0x0100000C, 0, 6, len(dylibs) + 1, len(cmds), 0, 0)
    return head + cmds + syms + strtab


def zipped(files: dict[str, bytes]) -> bytes:
    out = io.BytesIO()
    with zipfile.ZipFile(out, "w") as z:
        for name, data in files.items():
            z.writestr(name, data)
    return out.getvalue()


class Formats(unittest.TestCase):
    def test_elf(self):
        libs, syms = s.elf_imports(elf(["libc.so.6"], ["socket", "memcpy"]))
        self.assertEqual(libs, ["libc.so.6"])
        self.assertEqual(syms, ["memcpy", "socket"])

    def test_pe(self):
        dlls, funcs = s.pe_imports(pe({"KERNEL32.dll": ["CreateProcessW", "GetTickCount"], "WS2_32.dll": ["connect"]}))
        self.assertEqual(dlls, ["KERNEL32.dll", "WS2_32.dll"])
        self.assertEqual(funcs, ["CreateProcessW", "GetTickCount", "connect"])

    def test_macho(self):
        libs, syms = s.macho_imports(macho(["/usr/lib/libSystem.B.dylib"], ["dlopen", "printf"]))
        self.assertEqual(libs, ["/usr/lib/libSystem.B.dylib"])
        self.assertEqual(syms, ["dlopen", "printf"])

    def test_a_broken_library_is_said_not_read(self):
        scan = s.scan_archive(zipped({"lib/x.so": b"\x7fELF" + b"\x01" * 12}))
        self.assertTrue(scan["problems"])
        self.assertIn("Not read", s.report(scan, None))


class Report(unittest.TestCase):
    def test_a_quiet_mod(self):
        scan = s.scan_archive(zipped({"lib/linux-x86_64/m.so": elf(["libc.so.6"], ["memcpy", "sinf"])}))
        self.assertIn("Nothing beyond the game", s.report(scan, None))

    def test_what_is_new_since_the_published_version(self):
        old = s.scan_archive(zipped({"m.dll": pe({"KERNEL32.dll": ["GetTickCount"]})}))
        new = s.scan_archive(zipped({"m.dll": pe({"KERNEL32.dll": ["GetTickCount", "CreateProcessW"],
                                                  "WS2_32.dll": ["connect"]},
                                                 extra=b"http://203.0.113.9/up\0Login Data\0")}))
        text = s.report(new, old)
        self.assertIn("| network |", text)
        self.assertIn("**yes: `[WS2_32.dll]`, `connect`**", text)
        self.assertIn("| starts programs | `CreateProcessW` | **yes", text)
        self.assertIn("`http://203.0.113.9/up`", text)
        self.assertIn("`203.0.113.9`", text)
        self.assertIn("`login data`", text)

    def test_nothing_new(self):
        a = s.scan_archive(zipped({"m.so": elf([], ["dlopen"])}))
        self.assertIn("| loads code at run time | `dlopen` | no |", s.report(a, a))

    def test_gone(self):
        old = s.scan_archive(zipped({"m.so": elf([], ["system"])}))
        new = s.scan_archive(zipped({"m.so": elf([], ["memcpy"])}))
        self.assertIn("No longer: starts programs.", s.report(new, old))

    def test_version_numbers_are_not_addresses(self):
        scan = s.scan_archive(zipped({"m.so": elf([], [], extra=b"v1.2.3.4\0 10.0.0.1 \0")}))
        self.assertEqual(scan["ips"], {"10.0.0.1"})

    def test_file_names_cannot_be_markdown(self):
        scan = s.scan_archive(zipped({"`@team` [x](y).so": b"\x7fELF" + b"\x01" * 12}))
        text = s.report(scan, None)
        self.assertNotIn("`@team`", text)


class Hostile(unittest.TestCase):
    def test_a_universal_mach_o_inside_itself(self):
        fat = struct.pack(">II", 0xCAFEBABE, 1) + struct.pack(">IIIII", 0, 0, 0, 28, 0)
        scan = s.scan_archive(zipped({"m.dylib": fat}))
        self.assertTrue(scan["problems"])

    def test_a_load_command_of_no_size_does_not_hang(self):
        head = struct.pack("<IIIIIIII", 0xFEEDFACF, 0x0100000C, 0, 6, 0xFFFFFFFF, 0, 0, 0)
        scan = s.scan_archive(zipped({"m.dylib": head + struct.pack("<II", 0x19, 0)}))
        self.assertTrue(scan["problems"])

    def test_an_elf_without_section_headers_is_said_not_read(self):
        data = bytearray(elf([], ["socket"]))
        struct.pack_into("<HH", data, 0x3A, 64, 0)  # e_shnum = 0
        scan = s.scan_archive(zipped({"m.so": bytes(data)}))
        self.assertIn("Not read", s.report(scan, None))

    def test_a_symbol_cannot_be_markdown(self):
        scan = s.scan_archive(zipped({"m.so": elf([], ["socket", "WinHttp`|@team [x](y)"])}))
        text = s.report(scan, None)
        self.assertNotIn("`|", text)
        self.assertNotIn("`@team", text)
        self.assertIn("\\|", text)

    def test_a_windows_path_is_telling(self):
        scan = s.scan_archive(zipped({"m.dll": pe({"KERNEL32.dll": ["GetTickCount"]},
                                                  extra=b"C:\\Users\\x\\AppData\\Local\0")}))
        self.assertIn("\\appdata\\", scan["telling"])

    def test_public_resolvers_are_addresses(self):
        scan = s.scan_archive(zipped({"m.so": elf([], [], extra=b" 8.8.8.8 \0 1.0.0.0 \0")}))
        self.assertEqual(scan["ips"], {"8.8.8.8"})

    def test_other_files_are_not_unpacked_whole(self):
        scan = s.scan_archive(zipped({"big.dat": b"\0" * (300 * 1024 * 1024), "m.so": elf([], ["socket"])}))
        self.assertIn("network", s.capabilities(scan))
        self.assertEqual(scan["problems"], [])


if __name__ == "__main__":
    unittest.main()
