#!/usr/bin/env python3
"""Rewrites a Lua 5.1 chunk dumped on a 64-bit host (8-byte size_t) into the
32-bit layout luac.mtasa.com produces (4-byte size_t). Only the header's
size_t byte and the string lengths change; ints, instructions and numbers
are 4/4/8 bytes in both."""
import struct, sys

def convert(src):
    pos = 0
    out = bytearray()
    def take(n):
        nonlocal pos
        b = src[pos:pos + n]
        pos += n
        return b
    header = bytearray(take(12))
    assert header[:4] == b'\x1bLua' and header[4] == 0x51 and header[8] == 8, 'not a 64-bit Lua 5.1 chunk'
    header[8] = 4
    out += header
    def string():
        n = struct.unpack('<Q', take(8))[0]
        out.extend(struct.pack('<I', n))
        out.extend(take(n))
    def ints(count_fmt='<i'):
        n = struct.unpack('<i', take(4))[0]
        out.extend(struct.pack('<i', n))
        return n
    def function():
        string()                       # source
        out.extend(take(4 + 4))        # linedefined, lastlinedefined
        out.extend(take(4))            # nups, numparams, is_vararg, maxstacksize
        n = ints(); out.extend(take(4 * n))   # code
        n = ints()                     # constants
        for _ in range(n):
            t = take(1); out.extend(t)
            if t[0] == 1: out.extend(take(1))         # boolean
            elif t[0] == 3: out.extend(take(8))       # number
            elif t[0] == 4: string()                  # string
        n = ints()                     # nested functions
        for _ in range(n): function()
        n = ints(); out.extend(take(4 * n))   # lineinfo
        n = ints()                     # locals
        for _ in range(n):
            string(); out.extend(take(8))
        n = ints()                     # upvalue names
        for _ in range(n): string()
    function()
    assert pos == len(src), 'trailing bytes'
    return bytes(out)

data = open(sys.argv[1], 'rb').read()
open(sys.argv[2], 'wb').write(convert(data))
