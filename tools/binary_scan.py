#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""What a native mod's libraries can do, read from the files alone, for its review issue.

A native mod runs with the game's rights, so a reviewer wants to know what reaches past the game:
the network, other programs, other processes' memory, the keyboard, saved passwords. This reads
the import tables of each library of a release archive (ELF, PE and Mach-O, by hand: no tool to
install), the addresses written into it (URLs, IP addresses) and a few telling strings, and says
what is new against the version players have now. It never runs the code. A library that loads
what it needs by name at run time (dlopen, GetProcAddress) hides its imports: that is itself a
finding.

    python3 tools/binary_scan.py ARCHIVE.zip [OLD.zip]
"""
from __future__ import annotations

import io
import ipaddress
import re
import struct
import sys
import zipfile

# What an import says a library can do, by the symbol's name (a C library's, Win32's, a common
# library's). Matched on the whole name; Win32's A/W pairs both listed by their stem with a *.
CATEGORIES: dict[str, list[str]] = {
    "network": [
        "socket", "connect", "bind", "listen", "accept", "send", "sendto", "recv", "recvfrom",
        "getaddrinfo", "gethostbyname", "WSAStartup", "WSASocket*", "WSAConnect", "InternetOpen*",
        "InternetConnect*", "InternetOpenUrl*", "HttpOpenRequest*", "HttpSendRequest*", "WinHttp*",
        "URLDownloadToFile*", "URLOpenStream*", "curl_easy_*", "SDLNet_*", "CFSocket*", "CFStream*",
        "nw_*", "NSURLSession*",
    ],
    "starts programs": [
        "system", "popen", "_popen", "execl", "execle", "execlp", "execv", "execve", "execvp", "execvpe",
        "fork", "vfork", "posix_spawn", "posix_spawnp", "CreateProcess*", "CreateProcessAsUser*",
        "ShellExecute*", "WinExec", "_wsystem", "_spawn*", "_wspawn*", "NSTask*",
    ],
    "loads code at run time": [
        "dlopen", "dlsym", "LoadLibrary*", "LoadLibraryEx*", "GetProcAddress", "LdrLoadDll",
        "NSCreateObjectFileImageFromMemory", "NSLinkModule",
    ],
    "makes memory executable": [
        "mprotect", "VirtualProtect", "VirtualProtectEx", "VirtualAlloc", "VirtualAllocEx", "mmap",
    ],
    "reaches other processes": [
        "OpenProcess", "ReadProcessMemory", "WriteProcessMemory", "CreateRemoteThread*",
        "NtWriteVirtualMemory", "ptrace", "process_vm_readv", "process_vm_writev", "task_for_pid",
        "mach_vm_write", "SetThreadContext",
    ],
    "watches the keyboard or screen": [
        "SetWindowsHookEx*", "GetAsyncKeyState", "GetKeyboardState", "RegisterRawInputDevices",
        "BitBlt", "GetDC", "CGWindowListCreateImage", "CGEventTapCreate", "XQueryKeymap",
    ],
    "reads the clipboard": ["OpenClipboard", "GetClipboardData", "SDL_GetClipboardText"],
    "reads saved secrets": [
        "CryptUnprotectData", "CredRead*", "CredEnumerate*", "SecItemCopyMatching",
        "SecKeychainFindGenericPassword", "secret_password_lookup*",
    ],
    "changes system settings": [
        "RegSetValue*", "RegCreateKey*", "RegDeleteKey*", "RegDeleteValue*", "SHSetValue*",
        "CreateService*", "ChangeServiceConfig*", "SetFileAttributes*",
    ],
}

# System libraries that by themselves say a library talks to the network.
NETWORK_LIBRARIES = {"ws2_32.dll", "wsock32.dll", "wininet.dll", "winhttp.dll", "urlmon.dll",
                     "libcurl.so", "libcurl.so.4", "libcurl.dll", "libssl.so", "libssl.so.3"}

# Strings worth a reviewer's look wherever they are.
TELLING = re.compile(
    rb"(cmd\.exe|powershell|/bin/sh|/bin/bash|\\AppData\\|Login Data|Local State|"
    rb"discord(?:app)?\.com/api|api\.telegram\.org|pastebin\.com|\.onion\b|wallet\.dat|"
    rb"\.ssh/|id_rsa|keychain|stratum\+tcp)",
    re.IGNORECASE,
)
URL = re.compile(rb"\b(?:https?|ftp|wss?)://[A-Za-z0-9._~:/?#\[\]@!$&'()*+,;=%-]{3,200}")
IPV4 = re.compile(rb"(?<![\d.vV])(?:\d{1,3}\.){3}\d{1,3}(?![\d.])")
MOST = 40  # of each list shown
TITLE = "### What the libraries can do"
READ_LIMIT = 1024 * 1024 * 1024  # the most an archive's files may unpack to, all together


class Unreadable(Exception):
    pass


# ---- the formats ----------------------------------------------------------------------------------

def _cstr(data: bytes, at: int) -> str:
    end = data.find(b"\0", at)
    return data[at:end if end >= 0 else len(data)].decode("utf-8", "replace")


def elf_imports(data: bytes) -> tuple[list[str], list[str]]:
    """(needed libraries, undefined symbols) of a 64-bit little-endian ELF."""
    if data[4] != 2 or data[5] != 1:
        raise Unreadable("an ELF that is not 64-bit little-endian")
    shoff, = struct.unpack_from("<Q", data, 0x28)
    shentsize, shnum = struct.unpack_from("<HH", data, 0x3A)
    sections = []
    for i in range(shnum):
        sh = struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * shentsize)
        sections.append(sh)
    symbols, needed = set(), []
    for name, kind, _flags, _addr, off, size, link, _info, _align, entsize in sections:
        if kind == 11 and entsize:  # SHT_DYNSYM
            strtab = sections[link][4]
            for k in range(1, size // entsize):
                st_name, _info, _other, st_shndx = struct.unpack_from("<IBBH", data, off + k * entsize)
                if st_shndx == 0 and st_name:
                    symbols.add(_cstr(data, strtab + st_name).split("@")[0])
        if kind == 6 and entsize:  # SHT_DYNAMIC
            strtab = sections[link][4]
            for k in range(size // entsize):
                tag, val = struct.unpack_from("<qQ", data, off + k * entsize)
                if tag == 1:  # DT_NEEDED
                    needed.append(_cstr(data, strtab + val))
                if tag == 0:
                    break
    if not any(sh[1] == 11 for sh in sections):
        # Without section headers (stripped by sstrip and the like) the imports cannot be read here.
        raise Unreadable("an ELF without a .dynsym section")
    return needed, sorted(symbols)


def pe_imports(data: bytes) -> tuple[list[str], list[str]]:
    """(imported DLLs, imported functions) of a PE32+ (normal and delay-loaded imports)."""
    pe, = struct.unpack_from("<I", data, 0x3C)
    if data[pe:pe + 4] != b"PE\0\0":
        raise Unreadable("not a PE file")
    nsections, = struct.unpack_from("<H", data, pe + 6)
    opt_size, = struct.unpack_from("<H", data, pe + 20)
    opt = pe + 24
    if struct.unpack_from("<H", data, opt)[0] != 0x20B:
        raise Unreadable("a PE that is not 64-bit")
    dirs = opt + 112
    sections = [struct.unpack_from("<IIII", data, opt + opt_size + i * 40 + 8) for i in range(nsections)]

    def at(rva: int) -> int:
        for vsize, vaddr, rawsize, rawptr in sections:
            if vaddr <= rva < vaddr + max(vsize, rawsize):
                return rawptr + rva - vaddr
        raise Unreadable("an address outside the file")

    dlls, funcs = [], set()

    def thunks(rva: int) -> None:
        p = at(rva)
        while True:
            v, = struct.unpack_from("<Q", data, p)
            if v == 0:
                return
            if not v >> 63:
                funcs.add(_cstr(data, at(v & 0x7FFFFFFF) + 2))
            p += 8

    imp_rva, _ = struct.unpack_from("<II", data, dirs + 8)
    if imp_rva:
        p = at(imp_rva)
        while True:
            ilt, _t, _f, name, iat = struct.unpack_from("<IIIII", data, p)
            if name == 0:
                break
            dlls.append(_cstr(data, at(name)))
            thunks(ilt or iat)
            p += 20
    delay_rva, _ = struct.unpack_from("<II", data, dirs + 13 * 8)
    if delay_rva:
        p = at(delay_rva)
        while True:
            _attr, name, _h, _iat, int_rva = struct.unpack_from("<IIIII", data, p)[:5]
            if name == 0:
                break
            dlls.append(_cstr(data, at(name)))
            thunks(int_rva)
            p += 32
    return dlls, sorted(funcs)


def macho_imports(data: bytes, slice_of_fat: bool = False) -> tuple[list[str], list[str]]:
    """(linked dylibs, undefined symbols) of a 64-bit Mach-O, or of each slice of a universal one."""
    magic, = struct.unpack_from(">I", data, 0)
    if magic in (0xCAFEBABE, 0xCAFEBABF):
        if slice_of_fat:
            raise Unreadable("a universal Mach-O inside another")
        count, = struct.unpack_from(">I", data, 4)
        libs, syms = [], set()
        for i in range(count):
            if magic == 0xCAFEBABE:
                _cpu, _sub, off, size, _align = struct.unpack_from(">IIIII", data, 8 + i * 20)
            else:
                _cpu, _sub, off, size, _align, _r = struct.unpack_from(">IIQQII", data, 8 + i * 32)
            l, s = macho_imports(data[off:off + size], True)
            libs += [x for x in l if x not in libs]
            syms |= set(s)
        return libs, sorted(syms)
    if struct.unpack_from("<I", data, 0)[0] != 0xFEEDFACF:
        raise Unreadable("a Mach-O that is not 64-bit")
    ncmds, = struct.unpack_from("<I", data, 16)
    p, libs, syms = 32, [], set()
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", data, p)
        if size < 8:
            raise Unreadable("a load command of no size")
        if cmd in (0xC, 0x80000018, 0x18, 0x8000001F):  # LC_LOAD_DYLIB, _WEAK_, _LAZY_, _REEXPORT_
            off, = struct.unpack_from("<I", data, p + 8)
            libs.append(_cstr(data, p + off))
        if cmd == 0x2:  # LC_SYMTAB
            symoff, nsyms, stroff, _ = struct.unpack_from("<IIII", data, p + 8)
            for k in range(nsyms):
                strx, ntype, _sect, _desc, _value = struct.unpack_from("<IBBHQ", data, symoff + k * 16)
                if ntype & 0x01 and (ntype & 0x0E) == 0 and strx:  # external, undefined
                    name = _cstr(data, stroff + strx)
                    syms.add(name[1:] if name.startswith("_") else name)
        p += size
    return libs, sorted(syms)


def library_kind(data: bytes) -> str | None:
    if data[:4] == b"\x7fELF":
        return "elf"
    if data[:2] == b"MZ":
        return "pe"
    if data[:4] in (b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe", b"\xca\xfe\xba\xbf"):
        return "macho"
    return None


# ---- what it means --------------------------------------------------------------------------------

def _matches(symbol: str, pattern: str) -> bool:
    if pattern.endswith("*"):
        return symbol.startswith(pattern[:-1])
    return symbol == pattern or symbol in (pattern + "A", pattern + "W")


def categorize(symbols: list[str], libraries: list[str]) -> dict[str, list[str]]:
    found: dict[str, list[str]] = {}
    for cat, patterns in CATEGORIES.items():
        hits = sorted({s for s in symbols if any(_matches(s, p) for p in patterns)})
        if hits:
            found[cat] = hits
    nets = sorted({l for l in libraries if l.lower() in NETWORK_LIBRARIES})
    if nets:
        found.setdefault("network", [])
        found["network"] = sorted(set(found["network"]) | {f"[{l}]" for l in nets})
    return found


ASCII_RUN = re.compile(rb"[\x20-\x7e]{6,}")
UTF16_RUN = re.compile(rb"(?:[\x20-\x7e]\x00){6,}")


def _strings(data: bytes) -> tuple[set[str], set[str], set[str]]:
    # The printable runs first (one fast pass each), then the patterns in those alone. PE strings
    # are often UTF-16.
    texts = [b"\n".join(ASCII_RUN.findall(data)), b"\n".join(m[::2] for m in UTF16_RUN.findall(data))]
    urls, ips, telling = set(), set(), set()
    for t in texts:
        urls |= {m.decode("ascii", "replace").rstrip(".,);'") for m in URL.findall(t)}
        for m in IPV4.findall(t):
            try:
                ip = ipaddress.ip_address(m.decode())
            except ValueError:
                continue
            # Version numbers (v1.2.3.4, 1.0.0.0) left out; a host's address does not end in .0.
            if not (ip.is_unspecified or ip.is_loopback or ip.is_multicast or str(ip).startswith(("0.", "255."))
                    or str(ip).endswith((".0", ".255"))):
                ips.add(str(ip))
        telling |= {m.decode("ascii", "replace").lower() for m in TELLING.findall(t)}
    return urls, ips, telling


def scan_archive(blob: bytes) -> dict:
    """{"libraries": {path: {"kind", "libraries", "capabilities"}}, "urls", "ips", "telling", "problems"}
    of a release archive (a zip)."""
    out = {"libraries": {}, "urls": set(), "ips": set(), "telling": set(), "problems": []}
    try:
        z = zipfile.ZipFile(io.BytesIO(blob))
    except zipfile.BadZipFile:
        out["problems"].append("the archive is not a zip")
        return out
    budget = READ_LIMIT
    for info in z.infolist():
        if info.is_dir():
            continue
        try:
            with z.open(info) as f:
                kind = library_kind(f.read(4))
            if kind is None:
                continue
            if info.file_size > min(256 * 1024 * 1024, budget):
                out["problems"].append(f"{info.filename}: too large to read")
                continue
            budget -= info.file_size
            data = z.read(info)
        except (zipfile.BadZipFile, NotImplementedError, RuntimeError, OSError, EOFError, ValueError) as err:
            out["problems"].append(f"{info.filename}: cannot be unpacked ({err.__class__.__name__})")
            continue
        try:
            libs, syms = {"elf": elf_imports, "pe": pe_imports, "macho": macho_imports}[kind](data)
        except (Unreadable, struct.error, IndexError, ValueError, KeyError, RecursionError) as err:
            out["problems"].append(f"{info.filename}: cannot be read ({err or 'a broken header'})")
            libs, syms = [], []
        urls, ips, telling = _strings(data)
        out["urls"] |= urls
        out["ips"] |= ips
        out["telling"] |= telling
        out["libraries"][info.filename] = {"kind": kind, "libraries": libs, "capabilities": categorize(syms, libs)}
    return out


def capabilities(scan: dict) -> dict[str, set[str]]:
    caps: dict[str, set[str]] = {}
    for lib in scan["libraries"].values():
        for cat, syms in lib["capabilities"].items():
            caps.setdefault(cat, set()).update(syms)
    return caps


def _code(text: str) -> str:
    """The author's text (a symbol's or a file's name, a string) as inline code that cannot end
    early, split a table's cell or run long."""
    text = "".join(c if c.isprintable() else "?" for c in text).replace("`", "'").replace("|", "\\|")
    return f"`{text[:100] + '...' if len(text) > 100 else text}`"


def _list(items, most: int = MOST) -> str:
    items = sorted(items)
    shown = ", ".join(_code(i) for i in items[:most])
    return shown + (f" and {len(items) - most} more" if len(items) > most else "")


def report(new: dict, old: dict | None) -> str:
    """The review issue's section: what the libraries can do, marked new against `old`."""
    lines = [TITLE, "",
             "Read from the files' import tables and strings (nothing was run). A mod may still do "
             "more through what it loads at run time."]
    caps = capabilities(new)
    was = capabilities(old) if old else {}
    if not new["libraries"]:
        lines += ["", "No library found in the archives."]
    elif not caps:
        lines += ["", "Nothing beyond the game: no network, programs, other processes, keyboard hooks "
                      "or saved secrets."]
    else:
        lines += ["", "| Can | Through | New since the published version |", "|---|---|---|"]
        for cat in CATEGORIES:
            if cat not in caps:
                continue
            fresh = caps[cat] - was.get(cat, set()) if old else set()
            mark = ("**yes: " + _list(fresh, 12) + "**") if fresh else ("no" if old else "(no published version)")
            lines.append(f"| {cat} | {_list(caps[cat], 12)} | {mark} |")
    for title, key in (("Addresses written in it", "urls"), ("IP addresses", "ips"),
                       ("Strings worth a look", "telling")):
        items = new[key]
        if not items:
            continue
        fresh = items - (old or {}).get(key, set()) if old else set()
        lines += ["", f"{title}: {_list(items)}" + (f" (**new**: {_list(fresh, 12)})" if fresh else "")]
    if old is not None:
        gone = {c for c in was if c not in caps}
        if gone:
            lines += ["", f"No longer: {', '.join(sorted(gone))}."]
    for p in new["problems"][:MOST]:
        # A file's name is the author's text: shown as code, never as Markdown.
        lines += ["", f"Not read: {_code(p)}."]
    return "\n".join(lines) + "\n"


def main(argv: list[str]) -> int:
    if len(argv) not in (2, 3):
        print(__doc__.strip().splitlines()[-1].strip(), file=sys.stderr)
        return 2
    new = scan_archive(open(argv[1], "rb").read())
    old = scan_archive(open(argv[2], "rb").read()) if len(argv) == 3 else None
    sys.stdout.write(report(new, old))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
