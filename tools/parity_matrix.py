#!/usr/bin/env python3
"""Generate docs/PARITY_MATRIX.md: desktop MTA Neon client vs the Android port.

Every row is derived from source text at run time. The only hand-written values
are three enum anchors (PLAYER_PURESYNC=32, VEHICLE_INOUT=58, PLAYER_BULLETSYNC=98)
that the script asserts, so a changed desktop enum fails loudly instead of
silently shifting every packet id.

Usage:
    python3 tools/parity_matrix.py [--desktop PATH]

The port side is located relative to this file. Only --desktop has a default
path (the desktop reference checkout).
"""

import argparse
import bisect
import datetime
import pathlib
import re
import sys
from collections import OrderedDict

DEFAULT_DESKTOP = "/home/samuel/projects/world-rpg-br-en-eu/mta_neon_source"

REPO = pathlib.Path(__file__).resolve().parent.parent
PORT_CPP = REPO / "app" / "src" / "main" / "cpp"
PORT_SHARED = PORT_CPP / "shared"
OUT = REPO / "docs" / "PARITY_MATRIX.md"

DESKTOP_CLIENT_REL = pathlib.PurePosixPath("Client/mods/deathmatch/logic")

ANCHORS = {
    "PACKET_ID_PLAYER_PURESYNC": 32,
    "PACKET_ID_VEHICLE_INOUT": 58,
    "PACKET_ID_PLAYER_BULLETSYNC": 98,
}

# ---------------------------------------------------------------------------
# Source preparation
# ---------------------------------------------------------------------------

_TOKEN_RE = re.compile(
    r'"(?:\\.|[^"\\\n])*"'
    r"|'(?:\\.|[^'\\\n])*'"
    r"|//[^\n]*"
    r"|/\*.*?\*/",
    re.S,
)


def _blank(text):
    return re.sub(r"[^\n]", " ", text)


class Src:
    """One source file in two forms with identical offsets.

    clean:  comments blanked, string literals intact (for reading names/numbers).
    masked: comments blanked, string contents replaced by '_' (for brace matching).
    """

    def __init__(self, path):
        raw = path.read_text(encoding="utf-8", errors="replace")
        self.clean = _TOKEN_RE.sub(
            lambda m: _blank(m.group(0)) if m.group(0)[:2] in ("//", "/*") else m.group(0), raw)
        self.masked = _TOKEN_RE.sub(lambda m: self._mask(m.group(0)), raw)
        self._nl = [m.start() for m in re.finditer("\n", self.clean)]
        self.fn_headers = self._function_headers()

    @staticmethod
    def _mask(tok):
        if tok[:2] in ("//", "/*"):
            return _blank(tok)
        if tok[0] in "\"'":
            return tok[0] + "_" * (len(tok) - 2) + tok[-1]
        return tok

    def line(self, pos):
        return bisect.bisect_left(self._nl, pos) + 1

    def block_end(self, open_idx):
        """Index of the brace closing the one at open_idx (on masked text)."""
        depth = 0
        for m in re.finditer(r"[{}]", self.masked[open_idx:]):
            depth += 1 if m.group(0) == "{" else -1
            if depth == 0:
                return open_idx + m.start()
        return len(self.masked) - 1

    def _function_headers(self):
        """(offset, name) for column-0 lines that open a function definition."""
        out = []
        for m in re.finditer(r"^[A-Za-z_][^\n]*?\b([A-Za-z_]\w*(?:::~?[A-Za-z_]\w*)*)\s*\(", self.clean, re.M):
            line_end = self.clean.find("\n", m.start())
            line_end = len(self.clean) if line_end < 0 else line_end
            if self.clean[m.start():line_end].rstrip().endswith(";"):
                continue
            out.append((m.start(), m.group(1)))
        return out

    def enclosing(self, pos):
        keys = [p for p, _ in self.fn_headers]
        i = bisect.bisect_right(keys, pos) - 1
        return self.fn_headers[i][1] if i >= 0 else "(file scope)"

    def top_level_labels(self, open_idx):
        """Case labels directly inside the switch body opened at open_idx.

        Returns [(label_text, label_start, label_end)] using the masked text, so
        braces inside strings or comments are ignored.
        """
        close = self.block_end(open_idx)
        depth = 0
        out = []
        token = re.compile(r"[{}]|\bcase\b(?:[^:;{}\n]|::)+:|\bdefault\s*:")
        for m in token.finditer(self.masked, open_idx, close + 1):
            tok = m.group(0)
            if tok == "{":
                depth += 1
            elif tok == "}":
                depth -= 1
            elif depth == 1:
                label = "default" if tok.startswith("default") else tok[4:-1].strip()
                out.append((label, m.start(), m.end()))
        return out


def case_bodies(src, labels, close):
    """Code each case label runs. A label with an empty body falls through to the next one."""
    bodies = []
    for i in range(len(labels)):
        j = i
        while True:
            nxt = labels[j + 1][1] if j + 1 < len(labels) else close
            text = src.masked[labels[j][2]:nxt]
            if text.strip() or j + 1 >= len(labels):
                break
            j += 1
        bodies.append(text)
    return bodies


def switch_blocks(src):
    """Yields the opening-brace index of every switch statement in a file."""
    for m in re.finditer(r"\bswitch\s*\(", src.masked):
        brace = src.masked.find("{", m.end())
        if brace >= 0:
            yield brace


def parse_enum(src, open_idx):
    """Ordered {name: value} for an enum body (C rules: implicit +1, explicit '= N')."""
    close = src.block_end(open_idx)
    body = src.clean[open_idx + 1:close]
    values = OrderedDict()
    nxt = 0
    for item in body.split(","):
        item = " ".join(item.split())
        if not item:
            continue
        m = re.match(r"^([A-Za-z_]\w*)\s*(?:=\s*(.+))?$", item)
        if not m:
            raise SystemExit("unparsed enum item: %r" % item)
        value = int(m.group(2).strip(), 0) if m.group(2) else nxt
        values[m.group(1)] = value
        nxt = value + 1
    return values


def find_enum(src, pattern):
    m = re.search(pattern, src.masked)
    if not m:
        return None
    return parse_enum(src, src.masked.find("{", m.start()))


def rel(path, base):
    return path.relative_to(base).as_posix()


def walk(root, suffixes, skip=()):
    for path in sorted(root.rglob("*")):
        if path.suffix not in suffixes or not path.is_file():
            continue
        parts = path.relative_to(root).parts
        if any(p in skip for p in parts):
            continue
        yield path


# ---------------------------------------------------------------------------
# A. Lua functions
# ---------------------------------------------------------------------------

LOADFN_RE = re.compile(r"\bLoadFunctions\s*\(\s*\)\s*\{")
LUA_ENTRY_RE = re.compile(r'\{\s*"(\w+)"\s*,\s*([^{}]+?)\s*\}')
LUA_REGISTER_RE = re.compile(r'\blua_register\(\s*\w+\s*,\s*"(\w+)"\s*,\s*([\w:]+)\s*\)')
LUA_CLASS_RE = re.compile(r'\blua_register(static)?class\(\s*\w+\s*,\s*"(\w+)"(?:\s*,\s*"(\w+)")?')

C_STUB_STATEMENTS = [
    re.compile(r"^\(void\)\s*\w+$"),
    re.compile(r"^lua_pushnil\(\s*\w+\s*\)$"),
    re.compile(r"^lua_pushboolean\(\s*\w+\s*,\s*(?:0|1|true|false)\s*\)$"),
    re.compile(r"^lua_pushinteger\(\s*\w+\s*,\s*-?\d+\s*\)$"),
    re.compile(r"^lua_pushnumber\(\s*\w+\s*,\s*-?[\d.]+f?\s*\)$"),
    re.compile(r'^lua_pushstring\(\s*\w+\s*,\s*""\s*\)$'),
    re.compile(r"^lua_newtable\(\s*\w+\s*\)$"),
    re.compile(r"^return\s+-?\d+$"),
]


DEF_RE = re.compile(r"\b(\w+)::(LoadFunctions|LoadCFunctions)\s*\(\s*\)\s*\{")
CALL_RE = re.compile(r"\b(\w+)::LoadFunctions\s*\(\s*\)\s*;")
START_LOADER = "CLuaManager::LoadCFunctions"


def desktop_lua_functions(client_root):
    """Lua functions the desktop client actually registers.

    Walks the load graph from CLuaManager::LoadCFunctions (its {"name", Fn} table, then
    every X::LoadFunctions() it calls, recursively). Client luadefs and the Shared luadefs
    reached through CLuaShared are both included. Definitions that are never reached
    are returned separately so the matrix can say what was excluded.
    """
    client_logic = client_root / DESKTOP_CLIENT_REL
    shared_logic = client_root / "Shared/mods/deathmatch/logic"
    defs = OrderedDict()
    for base in (client_logic, shared_logic):
        for path in walk(base, {".cpp"}):
            src = Src(path)
            for m in DEF_RE.finditer(src.masked):
                key = m.group(1) + "::" + m.group(2)
                open_idx = m.end() - 1
                defs.setdefault(key, (path, src, open_idx, src.block_end(open_idx)))

    if START_LOADER not in defs:
        raise SystemExit("%s not found in desktop client" % START_LOADER)
    order = []
    seen = {START_LOADER}
    queue = [START_LOADER]
    while queue:
        key = queue.pop(0)
        order.append(key)
        path, src, o, c = defs[key]
        for m in CALL_RE.finditer(src.masked, o, c):
            callee = m.group(1) + "::LoadFunctions"
            if callee in seen:
                continue
            seen.add(callee)
            if callee in defs:
                queue.append(callee)

    rows = []
    for key in order:
        path, src, o, c = defs[key]
        if key == START_LOADER:
            group = "CLuaManager (LoadCFunctions table)"
        else:
            group = key.split("::")[0]
        for e in LUA_ENTRY_RE.finditer(src.clean, o, c):
            rows.append((group, e.group(1), rel(path, client_root), src.line(e.start()), e.group(2).strip()))

    lua_register_rows = []
    for path in walk(client_logic, {".cpp"}):
        src = Src(path)
        for m in LUA_REGISTER_RE.finditer(src.clean):
            lua_register_rows.append(("%s (lua_register)" % path.stem, m.group(1), rel(path, client_root),
                                      src.line(m.start()), m.group(2)))
    rows.extend(lua_register_rows)

    reached_classes = {key.split("::")[0] for key in order} | {"CLuaMain"}
    classes = []
    for base in (client_logic, shared_logic):
        for path in walk(base, {".cpp"}):
            if path.stem not in reached_classes:
                continue
            src = Src(path)
            for m in LUA_CLASS_RE.finditer(src.clean):
                kind = "static class" if m.group(1) else "class"
                classes.append((m.group(2), kind, m.group(3) or "-", rel(path, client_root), src.line(m.start())))

    unreached = [k for k in defs if k not in set(order)]
    return rows, classes, order, unreached


def port_stub_helpers(shared_dir):
    """Every int f(lua_State*) whose body is only constant pushes and a constant return."""
    helpers = OrderedDict()
    fn_bodies = {}
    for path in walk(shared_dir, {".cpp"}):
        src = Src(path)
        for m in re.finditer(r"\bint\s+(\w+)\s*\(\s*lua_State\s*\*\s*\w+\s*\)\s*\{", src.masked):
            open_idx = m.end() - 1
            close = src.block_end(open_idx)
            body = " ".join(src.clean[open_idx + 1:close].split())
            fn_bodies[m.group(1)] = (rel(path, PORT_CPP), src.line(m.start()))
            stmts = [s.strip() for s in body.split(";") if s.strip()]
            if not stmts or not stmts[-1].startswith("return"):
                continue
            if all(any(p.match(s) for p in C_STUB_STATEMENTS) for s in stmts):
                pushes = [s for s in stmts if s.startswith("lua_push") or s.startswith("lua_newtable")]
                summary = pushes[0] if pushes else stmts[-1]
                helpers[m.group(1)] = summary
    return helpers, fn_bodies


def port_lua_bindings(shared_dir, helpers, fn_bodies):
    """Effective global bindings in the order the VM installs them.

    Order (LuaVMStub.cpp installDxBindings -> InstallExtraBindings):
      1. binds[] table                    (installDxBindings)
      2. InstallJsonBindings              (LuaJsonApi.cpp, called first in InstallExtraBindings)
      3. SetGlobal(L, name, fn) calls     (LuaApiExtra.cpp, in source order)
      4. defaults[] table via SetDefault  (only when the name is still undefined)
    Returns {name: {"kind": ..., "fn": ..., "where": "file:line"}}.
    """
    eff = OrderedDict()
    binds_srcs = []
    defaults_src = None
    files = {}
    for path in walk(shared_dir, {".cpp"}):
        files[path] = Src(path)

    for path, src in files.items():
        # Several files may own a binds[] table (LuaVMStub.cpp, LuaGuiApi.cpp).
        for m in re.finditer(r"\bbinds\[\]\s*=\s*\{", src.masked):
            binds_srcs.append((path, src, src.masked.find("{", m.start())))
        m = re.search(r"\bdefaults\[\]\s*=\s*\{", src.masked)
        if m:
            defaults_src = (path, src, src.masked.find("{", m.start()))

    for path, src, open_idx in binds_srcs:
        for e in re.finditer(r'\{\s*"(\w+)"\s*,\s*(\w+)\s*\}', src.clean, re.S):
            if open_idx < e.start() < src.block_end(open_idx):
                eff[e.group(1)] = {"kind": "bind", "fn": e.group(2),
                                   "where": "%s:%d" % (rel(path, PORT_CPP), src.line(e.start()))}

    for path, src in files.items():
        for m in re.finditer(r"lua_pushcfunction\(\s*L\s*,\s*(\w+)\s*\)\s*;\s*lua_setglobal\(\s*L\s*,\s*\"(\w+)\"\s*\)",
                             src.clean):
            eff[m.group(2)] = {"kind": "json", "fn": m.group(1),
                               "where": "%s:%d" % (rel(path, PORT_CPP), src.line(m.start()))}

    for path, src in files.items():
        for m in re.finditer(r'SetGlobal\(\s*L\s*,\s*"(\w+)"\s*,\s*(\w+)\s*\)', src.clean):
            eff[m.group(1)] = {"kind": "setglobal", "fn": m.group(2),
                               "where": "%s:%d" % (rel(path, PORT_CPP), src.line(m.start()))}

    if defaults_src:
        path, src, open_idx = defaults_src
        for e in re.finditer(r'\{\s*"(\w+)"\s*,\s*(k\w+)\s*\}', src.clean):
            if open_idx < e.start() < src.block_end(open_idx) and e.group(1) not in eff:
                eff[e.group(1)] = {"kind": "default", "fn": e.group(2),
                                   "where": "%s:%d" % (rel(path, PORT_CPP), src.line(e.start()))}
    return eff


DEFAULT_KIND_RE = re.compile(r"kTrue|kFalse|kNil|kZero3|kZero|kEmptyString|kEmptyTable|kAlpha255")


def classify_lua(name, eff, helpers):
    """Returns (status, detail). status: implemented | stub | missing."""
    if name not in eff:
        return "missing", ""
    b = eff[name]
    if b["kind"] == "default":
        return "stub", "DefaultValue(%s)" % b["fn"]
    if b["fn"] in helpers:
        return "stub", "%s: %s" % (b["fn"], helpers[b["fn"]])
    return "implemented", b["fn"]


# ---------------------------------------------------------------------------
# B. Client events
# ---------------------------------------------------------------------------

def desktop_events(client_root):
    out = OrderedDict()
    for path in walk(client_root / DESKTOP_CLIENT_REL, {".cpp", ".h"}):
        src = Src(path)
        for m in re.finditer(r'\bAddEvent\(\s*"(\w+)"', src.clean):
            out.setdefault(m.group(1), (rel(path, client_root), src.line(m.start())))
    return out


def port_fired_events():
    fired = OrderedDict()
    pattern = re.compile(r'(?:\bfireHandlers\(\s*|handlers\.find\(\s*|handlers\[\s*)"(on\w+)"')
    for path in walk(PORT_CPP, {".cpp", ".h"}, skip=("lua53", "opus")):
        src = Src(path)
        for m in pattern.finditer(src.clean):
            fired.setdefault(m.group(1), []).append("%s:%d" % (rel(path, REPO), src.line(m.start())))
    return fired


# ---------------------------------------------------------------------------
# C. Packets
# ---------------------------------------------------------------------------

def desktop_packet_enum(client_root):
    path = client_root / "Shared/sdk/net/Packets.h"
    src = Src(path)
    enum = find_enum(src, r"\benum\s+ePacketID\s*\{")
    if enum is None:
        raise SystemExit("ePacketID enum not found in %s" % path)
    enum.pop("NUM_PACKETS", None)
    for name, expected in ANCHORS.items():
        if enum.get(name) != expected:
            raise SystemExit("anchor mismatch: %s = %r, expected %d" % (name, enum.get(name), expected))
    return enum


def desktop_inbound(client_root):
    """name -> list of (file, line, owner function, Packet_ handler call). Client/ only."""
    out = OrderedDict()
    for path in walk(client_root / "Client", {".cpp"}):
        src = Src(path)
        for brace in switch_blocks(src):
            labels = src.top_level_labels(brace)
            if not any(label.startswith("PACKET_ID_") for label, _, _ in labels):
                continue
            bodies = case_bodies(src, labels, src.block_end(brace))
            for i, (label, start, end) in enumerate(labels):
                if not label.startswith("PACKET_ID_"):
                    continue
                call = re.search(r"\b(Packet_\w+)\s*\(", bodies[i])
                out.setdefault(label[len("PACKET_ID_"):], []).append(
                    (rel(path, client_root), src.line(start), src.enclosing(start),
                     call.group(1) if call else "(inline)"))
    for path in walk(client_root / DESKTOP_CLIENT_REL, {".cpp"}):
        src = Src(path)
        for m in re.finditer(r"\bucPacketID\s*==\s*PACKET_ID_(\w+)", src.clean):
            out.setdefault(m.group(1), []).append(
                (rel(path, client_root), src.line(m.start()), src.enclosing(m.start()), "(RPC dispatch)"))
    return out


def desktop_outbound(client_root):
    out = OrderedDict()
    for path in walk(client_root / "Client", {".cpp", ".h"}):
        src = Src(path)
        for m in re.finditer(r"\bSendPacket\s*\(\s*PACKET_ID_(\w+)", src.clean):
            out.setdefault(m.group(1), (rel(path, client_root), src.line(m.start())))
    return out


def desktop_other_refs(client_root, known):
    other = OrderedDict()
    for path in walk(client_root / "Client", {".cpp", ".h"}):
        src = Src(path)
        for m in re.finditer(r"\bPACKET_ID_(\w+)\b", src.clean):
            name = m.group(1)
            if name in known or name == "NUM_PACKETS":
                continue
            other.setdefault(name, (rel(path, client_root), src.line(m.start())))
    return other


def port_packet_enum(path, enum_name):
    src = Src(path)
    enum = find_enum(src, r"\benum\s+class\s+%s\b[^{]*\{" % enum_name)
    if enum is None:
        raise SystemExit("%s not found in %s" % (enum_name, path))
    return enum


def port_inbound():
    """(explicit {id: (handler, where)}, provisional {id: (name, where)})."""
    ph = PORT_SHARED / "PacketHandler.cpp"
    src = Src(ph)
    explicit = OrderedDict()
    for m in re.finditer(r"\bif\s*\(\s*gameId\s*==\s*(\d+)\s*\)(.*)", src.clean):
        rest = m.group(2)
        call = re.search(r"([\w:]+)\s*\(", rest)
        handler = call.group(1).split("::")[-1] if call else "(block)"
        explicit.setdefault(int(m.group(1)), (handler, "%s:%d" % (rel(ph, REPO), src.line(m.start()))))
    provisional = OrderedDict()
    mtapacket = port_packet_enum(PORT_SHARED / "PacketHandler.h", "MTAPacketID")
    for path in walk(PORT_CPP, {".cpp"}, skip=("lua53", "opus")):
        s = Src(path)
        for m in re.finditer(r"\bregisterHandler\(\s*MTAPacketID::(\w+)", s.clean):
            name = m.group(1)
            provisional.setdefault(mtapacket[name], (name, "%s:%d" % (rel(path, REPO), s.line(m.start()))))
    return explicit, provisional


def port_outbound():
    """{id: (where, note)} for the first BitStreamWriter write in each builder."""
    out = OrderedDict()
    for sub in ("net", "shared", "gtasa", "jni"):
        for path in walk(PORT_CPP / sub, {".cpp"}, skip=("lua53", "opus", "raknet")):
            src = Src(path)
            for m in re.finditer(r"\bBitStreamWriter\s+(\w+)\s*;", src.clean):
                var = m.group(1)
                first = re.search(r"\b%s\.Write\(\s*static_cast<uint8_t>\(\s*(\d+)\s*\)\s*\)" % re.escape(var),
                                  src.clean[m.end():])
                if not first:
                    continue
                pos = m.end() + first.start()
                out.setdefault(int(first.group(1)), ("%s:%d" % (rel(path, REPO), src.line(pos)), ""))
    return out


# ---------------------------------------------------------------------------
# D. Element types
# ---------------------------------------------------------------------------

def desktop_entity_types(client_root):
    path = client_root / DESKTOP_CLIENT_REL / "CClientGame.h"
    src = Src(path)
    enum = find_enum(src, r"\benum\s*\{\s*DUMMY\s*,")
    if enum is None:
        raise SystemExit("CClientGame element-type enum not found")
    fn_path = client_root / DESKTOP_CLIENT_REL / "CPacketHandler.cpp"
    fsrc = Src(fn_path)
    m = re.search(r"void\s+CPacketHandler::Packet_EntityAdd\s*\(", fsrc.clean)
    if not m:
        raise SystemExit("CPacketHandler::Packet_EntityAdd not found")
    sw = re.search(r"\bswitch\s*\(\s*ucEntityTypeID\s*\)", fsrc.masked[m.end():])
    if not sw:
        raise SystemExit("ucEntityTypeID switch not found")
    open_idx = fsrc.masked.find("{", m.end() + sw.start())
    labels = fsrc.top_level_labels(open_idx)
    close = fsrc.block_end(open_idx)
    bodies = case_bodies(fsrc, labels, close)
    rows = []
    for i, (label, start, end) in enumerate(labels):
        if label == "default":
            continue
        body = bodies[i]
        reads = bool(re.search(r"\bbitStream\.Read|\bReadCompressed\b|\bRead\(", body))
        rows.append((label.replace("CClientGame::", ""), reads, rel(fn_path, client_root), fsrc.line(start)))
    return enum, rows, any(lbl == "default" for lbl, _, _ in labels)


def port_entity_cases():
    path = PORT_SHARED / "entity" / "EntityAddTypes.h"
    types = {}
    for m in re.finditer(r"\b(kType\w+)\s*=\s*(\d+)", Src(path).clean):
        types[m.group(1)] = int(m.group(2))
    core = Src(PORT_SHARED / "entity" / "EntityAddCore.cpp")
    m = re.search(r"\bReadEntityBody\s*\(", core.clean)
    sw = re.search(r"\bswitch\s*\(\s*common\.type\s*\)", core.masked[m.end():])
    open_idx = core.masked.find("{", m.end() + sw.start())
    labels = core.top_level_labels(open_idx)
    close = core.block_end(open_idx)
    bodies = case_bodies(core, labels, close)
    parsers = {}
    for p in walk(PORT_SHARED / "entity", {".cpp"}):
        s = Src(p)
        for mm in re.finditer(r"^\s*bool\s+(ReadEntityBody\w+)\s*\(", s.clean, re.M):
            parsers[mm.group(1)] = rel(p, REPO)
    rows = []
    for i, (label, start, end) in enumerate(labels):
        if label == "default":
            continue
        body = " ".join(bodies[i].split())
        value = types[label] if label in types else int(label)
        call = re.search(r"return\s+(ReadEntityBody\w+)\s*\(", body)
        if call:
            state, parser = "parser", call.group(1)
            where = parsers.get(call.group(1), "?")
        elif re.search(r"return\s+true\s*;", body):
            state, parser, where = "no-op", "(returns true, reads nothing)", "-"
        else:
            state, parser, where = "other", body[:60], "-"
        rows.append((value, label, state, parser, where, "%s:%d" % (rel(PORT_SHARED / "entity" / "EntityAddCore.cpp", REPO), core.line(start))))
    return types, rows


# ---------------------------------------------------------------------------
# Rendering helpers
# ---------------------------------------------------------------------------

def pct(part, total):
    return "%.1f" % (100.0 * part / total) if total else "0.0"


def cell(text):
    return str(text).replace("|", "\\|").replace("\n", " ")


def table(headers, rows):
    lines = ["| " + " | ".join(headers) + " |", "|" + "|".join("---" for _ in headers) + "|"]
    for row in rows:
        lines.append("| " + " | ".join(cell(c) for c in row) + " |")
    return "\n".join(lines)


def names_list(names):
    return ", ".join("`%s`" % n for n in names) if names else "_none_"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--desktop", default=DEFAULT_DESKTOP,
                    help="desktop mta_neon_source checkout (default: %(default)s)")
    args = ap.parse_args()

    client_root = pathlib.Path(args.desktop).expanduser()
    client_logic = client_root / DESKTOP_CLIENT_REL
    if not client_logic.is_dir():
        sys.exit("desktop client logic not found: %s" % client_logic)
    if not PORT_SHARED.is_dir():
        sys.exit("port shared sources not found: %s" % PORT_SHARED)

    today = datetime.date.today().isoformat()
    summary = []

    # ---- A. Lua functions -------------------------------------------------
    lua_rows, lua_classes, lua_order, lua_unreached = desktop_lua_functions(client_root)
    helpers, fn_bodies = port_stub_helpers(PORT_SHARED)
    eff = port_lua_bindings(PORT_SHARED, helpers, fn_bodies)

    groups = OrderedDict()
    lua_unique = OrderedDict()
    dup_count = 0
    for group, name, file, line, handler in lua_rows:
        if name in lua_unique:
            dup_count += 1
            continue
        lua_unique[name] = (group, file, line, handler)
        groups.setdefault(group, []).append(name)

    lua_status = {n: classify_lua(n, eff, helpers) for n in lua_unique}
    lua_totals = {"implemented": 0, "stub": 0, "missing": 0}
    for st, _ in lua_status.values():
        lua_totals[st] += 1
    lua_total = len(lua_unique)
    port_only = [n for n in eff if n not in lua_unique]

    summary.append(("A1 Lua functions", lua_total, lua_totals["implemented"], lua_totals["stub"],
                    lua_totals["missing"],
                    "Port-only bindings not in desktop: %d. Duplicates across groups skipped: %d."
                    % (len(port_only), dup_count)))

    class_rows = []
    port_globals = set(eff)
    for name, kind, parent, file, line in lua_classes:
        class_rows.append((name, kind, parent, file + ":" + str(line),
                           "implemented" if name in port_globals else "missing"))
    cls_impl = sum(1 for r in class_rows if r[4] == "implemented")
    port_class_calls = sum(
        len(re.findall(r"\blua_(?:registerclass|registerstaticclass|classfunction|classvariable)\b", Src(p).clean))
        for p in walk(PORT_CPP, {".cpp", ".h"}, skip=("lua53", "opus")))
    summary.append(("A2 Lua OOP classes", len(class_rows), cls_impl, 0, len(class_rows) - cls_impl,
                    "Matched by global name only. Port class-registration calls found: %d."
                    % port_class_calls))

    # ---- B. Client events -------------------------------------------------
    desk_ev = desktop_events(client_root)
    fired = port_fired_events()
    ev_fired = [n for n in desk_ev if n in fired]
    ev_missing = [n for n in desk_ev if n not in fired]
    summary.append(("B Client events", len(desk_ev), len(ev_fired), 0, len(ev_missing),
                    "Port fires %d literal event names in total (%s). Server-triggered names from packet 81 are dispatched dynamically and are not counted."
                    % (len(fired), ", ".join(fired.keys()))))

    # ---- C. Packets -------------------------------------------------------
    d_enum = desktop_packet_enum(client_root)
    d_in = desktop_inbound(client_root)
    d_out = desktop_outbound(client_root)
    d_other = desktop_other_refs(client_root, set(d_in) | set(d_out))
    explicit, provisional = port_inbound()
    p_out = port_outbound()

    def enum_name(value):
        for k, v in d_enum.items():
            if v == value:
                return k[len("PACKET_ID_"):]
        return "(not in desktop enum)"

    in_rows = []
    in_counts = {"handled": 0, "provisional": 0, "missing": 0}
    for name, sites in d_in.items():
        value = d_enum.get("PACKET_ID_" + name)
        if value is None:
            status, port_detail = "unknown id", ""
        elif value in explicit:
            status = "handled"
            port_detail = "%s (%s)" % explicit[value]
        elif value in provisional:
            status = "provisional"
            port_detail = "registered as MTAPacketID::%s (%s) under id %d" % (
                provisional[value][0], provisional[value][1], value)
        else:
            status, port_detail = "missing", ""
        if status in in_counts:
            in_counts[status] += 1
        first = sites[0]
        in_rows.append((value, name, first[0] + ":" + str(first[1]), first[2], first[3], status, port_detail))
    in_rows.sort(key=lambda r: (r[0] is None, r[0] or 0))
    in_total = len(d_in)
    # Provisional registrations do not match the wire numbering, so they count as missing.
    in_missing = in_counts["missing"] + in_counts["provisional"]
    port_in_extra = [v for v in explicit if enum_name(v) not in d_in]
    summary.append(("C1 Inbound packets (UPE game ids)", in_total, in_counts["handled"], 0, in_missing,
                    "%d desktop ids reachable only via provisional registerHandler (ids 0-11, not wire ids) counted as missing. Port explicitly dispatches %d ids."
                    % (in_counts["provisional"], len(explicit))))

    out_rows = []
    out_impl = 0
    for name, (file, line) in d_out.items():
        value = d_enum.get("PACKET_ID_" + name)
        if value in p_out:
            out_impl += 1
            status = "sent"
            where = p_out[value][0]
        else:
            status = "not sent"
            where = ""
        out_rows.append((value, name, file + ":" + str(line), status, where))
    out_rows.sort(key=lambda r: (r[0] is None, r[0] or 0))
    out_extra = [(v, enum_name(v), w) for v, (w, _) in p_out.items() if enum_name(v) not in d_out]
    summary.append(("C2 Outbound packets (client sends)", len(d_out), out_impl, 0, len(d_out) - out_impl,
                    "Port sends %d game ids in total, of which %d have no desktop SendPacket site."
                    % (len(p_out), len(out_extra))))

    # ---- D. Element types -------------------------------------------------
    d_types, d_switch, d_has_default = desktop_entity_types(client_root)
    p_types, p_cases = port_entity_cases()
    p_by_value = {v: (label, state, parser, where, loc) for v, label, state, parser, where, loc in p_cases}
    type_rows = []
    type_counts = {"implemented": 0, "stub": 0, "missing": 0}
    d_switch_values = set()
    for label, reads, file, line in d_switch:
        value = d_types.get(label)
        d_switch_values.add(value)
        if value in p_by_value:
            _, state, parser, where, loc = p_by_value[value]
            # Implemented: the port's read/no-read behaviour matches the desktop case.
            # Stub: bound, but it reads when desktop does not, or skips a read desktop does.
            if state == "parser":
                status = "implemented" if reads else "stub"
            elif state == "no-op":
                status = "stub" if reads else "implemented"
            else:
                status = "missing"
        else:
            state, parser, where, loc = "absent", "(default: ENTITY_BODY_FAIL)", "-", "-"
            status = "missing"
        type_counts[status] += 1
        type_rows.append((value, label, "reads" if reads else "no read", state, parser, where,
                          loc, status, file + ":" + str(line)))
    d_type_total = len(d_switch)
    d_extra = [(v, n) for n, v in d_types.items() if v not in d_switch_values]
    summary.append(("D Element types (ENTITY_ADD)", d_type_total, type_counts["implemented"],
                    type_counts["stub"], type_counts["missing"],
                    "Desktop enum has %d entries; %d not in the desktop ENTITY_ADD switch. Desktop default branch present: %s."
                    % (len(d_types), len(d_extra), "yes" if d_has_default else "no")))

    # ---- Write document ---------------------------------------------------
    out = []
    out.append("# Neon Parity Matrix (P5)\n")
    out.append("Generated: %s\n" % today)
    out.append("Regenerate: `python3 tools/parity_matrix.py` "
               "(optional `--desktop <mta_neon_source checkout>`, defaults to `%s`).\n" % DEFAULT_DESKTOP)
    out.append("Desktop reference: `%s`. Port sources: `app/src/main/cpp/`. "
               "Every row is derived from source by `tools/parity_matrix.py`; do not edit this file by hand.\n" % client_root)
    out.append("Percent = implemented / desktop total. Stub and missing columns are defined in the method notes at the end.\n")

    out.append("## Summary\n")
    rows = []
    for name, total, impl, stub, missing, note in summary:
        rows.append((name, total, impl, stub, missing, pct(impl, total) + "%", note))
    out.append(table(["Section", "Desktop total", "Implemented", "Stub", "Missing", "%", "Notes"], rows))
    out.append("")

    # A
    out.append("## A. Client Lua functions\n")
    out.append("Desktop: `{\"name\", Fn}` tables and `lua_register` calls reached from `CLuaManager::LoadCFunctions` "
               "(Client luadefs and the Shared luadefs that `CLuaShared` loads). "
               "Port: `binds[]`, `SetGlobal`, `fromJSON`/`toJSON` and the `defaults[]` table in `app/src/main/cpp/shared/`.\n")
    out.append("Desktop scope: the load graph from `CLuaManager::LoadCFunctions` (%d loader functions reached: %s). "
               "Loader definitions never reached and therefore excluded: %s.\n"
               % (len(lua_order), ", ".join("`%s`" % k for k in lua_order),
                  ", ".join("`%s`" % k for k in lua_unreached) or "none"))
    out.append("### A1. Stub helpers the script treats as stubs\n")
    helper_rows = [(h, s, "%s:%d" % fn_bodies[h]) for h, s in helpers.items()]
    out.append(table(["C function", "Constant body", "Defined at"], helper_rows))
    out.append("")
    out.append("Plus the `DefaultValue` dispatcher (`LuaApiExtra.cpp`), which returns the kind in its `defaults[]` row (`kTrue`, `kFalse`, `kNil`, `kZero`, ...). A name bound there is a stub.\n")

    out.append("### A1 by desktop group\n")
    grp_rows = []
    for g, names in groups.items():
        c = {"implemented": 0, "stub": 0, "missing": 0}
        for n in names:
            c[lua_status[n][0]] += 1
        grp_rows.append((g, len(names), c["implemented"], c["stub"], c["missing"], pct(c["implemented"], len(names)) + "%"))
    out.append(table(["Desktop group", "Total", "Implemented", "Stub", "Missing", "%"], grp_rows))
    out.append("")

    out.append("### A1 detail per group\n")
    for g, names in groups.items():
        impl = [n for n in names if lua_status[n][0] == "implemented"]
        stub = [n for n in names if lua_status[n][0] == "stub"]
        miss = [n for n in names if lua_status[n][0] == "missing"]
        out.append("#### %s\n" % g)
        if impl:
            out.append("Implemented (%d): %s\n" % (len(impl), names_list(impl)))
        if stub:
            out.append("Stub (%d):\n" % len(stub))
            out.append(table(["Lua function", "Port binding", "Where"],
                             [(n, lua_status[n][1], eff[n]["where"]) for n in stub]))
            out.append("")
        if miss:
            out.append("Missing (%d): %s\n" % (len(miss), ", ".join("`%s`" % n for n in miss)))
    if port_only:
        out.append("#### Port-only bindings (not in desktop luadefs)\n")
        out.append(", ".join("`%s`" % n for n in port_only) + "\n")

    out.append("### A2. OOP classes (`lua_registerclass` / `lua_registerstaticclass`)\n")
    out.append(table(["Class", "Kind", "Parent", "Desktop location", "Port"],
                     [(n, k, p, loc, st) for n, k, p, loc, st in class_rows]))
    out.append("")

    # B
    out.append("## B. Client events\n")
    out.append("Desktop: literal `AddEvent(\"name\", ...)` calls under `%s`. "
               "Port: names used in `fireHandlers(...)` or in a `handlers.find` / `handlers[...]` dispatch site under `app/src/main/cpp/`.\n"
               % DESKTOP_CLIENT_REL)
    out.append(table(["Event", "Status", "Port dispatch site(s)", "Desktop registration"],
                     [(n, "fired" if n in fired else "not fired",
                       ", ".join(fired[n]) if n in fired else "-", "%s:%d" % desk_ev[n])
                      for n in desk_ev]))
    out.append("")

    # C
    out.append("## C. Network packets\n")
    out.append("Desktop ids come from `Shared/sdk/net/Packets.h` (`ePacketID`, %d ids). "
               "Anchors asserted: PLAYER_PURESYNC=%d, VEHICLE_INOUT=%d, PLAYER_BULLETSYNC=%d.\n"
               % (len(d_enum), ANCHORS["PACKET_ID_PLAYER_PURESYNC"], ANCHORS["PACKET_ID_VEHICLE_INOUT"],
                  ANCHORS["PACKET_ID_PLAYER_BULLETSYNC"]))
    out.append("### C1. Inbound (desktop `case PACKET_ID_*` and RPC dispatch vs port `gameId ==` dispatch)\n")
    out.append(table(["Id", "Packet", "Desktop case (file:line)", "Desktop owner", "Handler call", "Port status", "Port site"],
                     [(v if v is not None else "?", n, loc, owner, call, st, pd)
                      for v, n, loc, owner, call, st, pd in in_rows]))
    out.append("")
    out.append("Port inbound ids with no desktop inbound case: %s\n"
               % (", ".join("%d (%s)" % (v, enum_name(v)) for v in port_in_extra) or "none"))

    out.append("### C2. Outbound (desktop `SendPacket(PACKET_ID_*)` vs port first-write-of-builder)\n")
    out.append(table(["Id", "Packet", "Desktop send (file:line)", "Port status", "Port site"],
                     [(v if v is not None else "?", n, loc, st, w) for v, n, loc, st, w in out_rows]))
    out.append("")
    out.append("Port sends with no desktop SendPacket site: %s\n"
               % (", ".join("%d (%s, %s)" % (v, n, w) for v, n, w in out_extra) or "none"))
    out.append("Desktop references without a case or send (enum/other uses): %s\n"
               % (", ".join("`%s`" % n for n in d_other) or "none"))

    # D
    out.append("## D. Element types (ENTITY_ADD)\n")
    out.append("Desktop: top-level `case CClientGame::*` labels of the `ucEntityTypeID` switch in `CPacketHandler::Packet_EntityAdd`. "
               "Port: top-level `case` labels of the `common.type` switch in `ReadEntityBody`, matched by numeric value.\n")
    out.append(table(["Value", "Desktop type", "Desktop body", "Port case", "Port parser", "Port parser file", "Port site", "Status", "Desktop case"],
                     [(v, label, rd, state, parser, where, loc, st, dl)
                      for v, label, rd, state, parser, where, loc, st, dl in type_rows]))
    out.append("")
    out.append("Desktop enum entries not in the ENTITY_ADD switch: %s\n"
               % (", ".join("%d %s" % (v, n) for v, n in d_extra) or "none"))
    port_extra = [(v, label) for v, label, _, _, _, _ in p_cases if v not in d_switch_values]
    out.append("Port cases with no desktop ENTITY_ADD case: %s\n"
               % (", ".join("%d %s" % (v, l) for v, l in port_extra) or "none"))

    # Method and blind spots
    out.append("## Method and blind spots\n")
    out.append("""- **Implemented** means the Lua name is bound to a C function whose body is not a constant return. It does not mean the behaviour matches desktop. Argument handling and return values are not compared.
- **Stub** means the binding is a constant helper (table in A1, detected from the C body), or a `defaults[]` entry in `LuaApiExtra.cpp`. A `defaults[]` entry only applies when no core binding already defines the name, which the script follows.
- **Port binding order** follows `installDxBindings` (binds, then `InstallJsonBindings`, then `SetGlobal` calls, then defaults). This order is hard-coded in the script. If `InstallExtraBindings` changes order, update `port_lua_bindings`.
- **Lua name overlap** in other ports (Lua resource code that defines a function with the same name) is not counted. Only C-side bindings are.
- **Desktop luadefs** are read from `LoadFunctions` tables (entries of the form `{"name", Handler}`) and `lua_register` calls. Functions registered through other mechanisms are missed. Duplicates across groups are counted once.
- **Client events** are literal names only. `AddEvent` calls that pass a variable name are not counted.
- **Packets (inbound)** use the top-level `case` labels of each switch whose labels are `PACKET_ID_*`, plus `ucPacketID == PACKET_ID_*` checks. The port side uses `gameId == N` lines in `PacketHandler.cpp`. `registerHandler(MTAPacketID::*)` entries are provisional: their numeric ids (0-11) do not match the desktop enum (the port's own comment says so). They are counted as missing.
- **Packets (outbound)** on the port: the first `Write(static_cast<uint8_t>(N))` after each `BitStreamWriter` declaration. Payload bytes written later are skipped. A builder whose first write is not a literal is missed.
- **Element types**: a type is implemented when the port's read/no-read behaviour matches its desktop case: a parser where the desktop reads, or a no-op where it does not. It is a stub when those disagree (for example WORLD_MESH, which the desktop breaks on without reading but the port hands to a parser). A type the port fails on by default is missing. The desktop "reads" test looks for `bitStream.Read`/`Read(` in the case body (after fall-through grouping), which is approximate.
- **Enclosing function names** come from column-0 function headers. Code that does not follow that layout gets the wrong owner name.
""")

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("\n".join(out) + "\n", encoding="utf-8")

    # Console summary
    print("Wrote %s" % OUT)
    print()
    print("| Section | Desktop total | Implemented | Stub | Missing | % |")
    print("|---|---|---|---|---|---|")
    for name, total, impl, stub, missing, _ in summary:
        print("| %s | %d | %d | %d | %d | %s%% |" % (name, total, impl, stub, missing, pct(impl, total)))


if __name__ == "__main__":
    main()
