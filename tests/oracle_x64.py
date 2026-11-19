#!/usr/bin/env python3
"""Compares dbg/x64.c with llvm-objdump on every instruction of the given executables: the length,
the kind of control flow, the destination, whether the stack pointer is written, a repeat prefix. Two arguments stand for
bytes no compiler writes: random:<seed>:<n>, n pieces of 32,768 random bytes, and sweep, every opcode
of every map under each prefix with the ModRM bytes that name register 4 (the stack pointer, or ah,
xmm4, k4...), each followed by 15 one-byte instructions so that the next starts clean. The disassembler
crashes on some pieces, and those are left out and counted.
Usage: oracle_x64.py <x64q> <exe or random:seed:n or sweep>..."""
import random, re, subprocess, sys, tempfile

FLOW = {"call": 1, "lcall": 1, "jmp": 2, "ljmp": 2, "ret": 4, "retf": 4, "retfq": 4, "iretd": 4, "iretq": 4, "iret": 4, "iretw": 4, "retw": 4, "retfw": 4}
STACK = {"push", "pop", "pushfq", "popfq", "pushf", "popf", "leave", "enter"}
READS = {"cmp", "test", "bt", "ud1", "nop", "div", "idiv", "mul", "vmwrite", "verr", "verw", "ltr", "lldt", "lmsw", "wrfsbase", "wrgsbase",
         "umonitor", "tpause", "umwait", "ptwrite", "incsspd", "incsspq", "invept", "invvpid", "invpcid", "uwrmsr", "movdir64b", "enqcmd", "enqcmds"}   # their first operand is not written
SP = {"rsp", "esp", "sp", "spl"}
STRING = re.compile(r"(movs|cmps|stos|lods|scas|ins|outs)[bwdq]$")
PREFIXES = ("rep", "repe", "repne", "lock", "notrack", "data16", "bnd", "addr32", "xacquire", "xrelease", "cs", "ds", "es", "fs", "gs", "ss")
P = r"(?:(?:66|67|f0|f2|f3|2e|36|3e|26|64|65) )*"
LEFT_OUT = re.compile(P + r"(?:4. )?(?:d5|62 .[04-9a-f]|8f (?:[13579bdf].|.[89a-f]))|" + P + r"66 " + P + r"(?:4. )?(?:e8|e9|0f 8.)")
LINE = re.compile(r"^\s*([0-9a-f]+): ((?:[0-9a-f]{2} ?)+)\s*\t(.*)$")

def expected(text):
    words = [w for w in text.split("\t") if w.strip()]
    prefixes = []
    while words[0].strip() in PREFIXES:
        prefixes.append(words.pop(0).strip())
    name = words[0].strip()
    operands = [o.strip() for o in re.sub(r"\s*#.*| <.*", "", words[1]).split(",")] if len(words) > 1 else []
    flow = FLOW.get(name, 3 if name[0] == "j" or name.startswith("loop") else 0)
    known = flow in (1, 2, 3) and operands[0].startswith("0x")
    target = int(operands[0], 16) if known else 0
    sp = flow == 0 and (name in STACK or (bool(operands) and operands[0] in SP and name not in READS and not (name == "imul" and len(operands) == 1)) or (name in ("xchg", "xadd", "mulx") and operands[1] in SP))
    repeats = any(p.startswith("rep") for p in prefixes) and bool(STRING.match(name))
    return flow, int(known), target, int(sp), int(repeats)

def sweep():
    maps = [b"", b"\x0f", b"\x0f\x38", b"\x0f\x3a"]
    for pp in range(4):
        for third in (0x78, 0x58):   # no third register, register 4
            maps += [bytes([0xc5, 0x80 | third | pp])]
            maps += [bytes([0xc4, 0xe0 | m, w | third | pp]) for m in (1, 2, 3) for w in (0, 0x80)]
            maps += [bytes([0x62, 0xf0 | m, w | third | 4 | pp, 0x08]) for m in (1, 2, 3) for w in (0, 0x80)]
    out = bytearray()
    for m in maps:
        for prefix in (b"", b"\x66", b"\xf3", b"\xf2", b"\x40", b"\x41", b"\x44", b"\x48") if m[:1] in (b"", b"\x0f") else (b"",):
            for op in range(256):
                for modrm in (0xe4, 0xc4, 0xe0, 0x24, 0xcc, 0xd4, 0xdc, 0xec, 0xf4, 0xfc, 0x65):
                    out += prefix + m + bytes([op, modrm]) + random.randbytes(8) + b"\x90" * 15
    return out

def subjects(tmp):
    for arg in sys.argv[2:]:
        if arg != "sweep" and not arg.startswith("random:"):
            yield arg
            continue
        random.seed(int(arg.split(":")[1]) if arg != "sweep" else 0)
        data = sweep() if arg == "sweep" else random.randbytes(32768 * int(arg.split(":")[2]))
        for at in range(0, len(data), 32768):
            open(f"{tmp}/r.bin", "wb").write(data[at:at + 32768])
            subprocess.run(["llvm-objcopy", "-I", "binary", "-O", "elf64-x86-64", "--rename-section", ".data=.text,code", f"{tmp}/r.bin", f"{tmp}/r.o"], check=True)
            yield f"{tmp}/r.o"

total = bad = crashed = 0
tmp = tempfile.TemporaryDirectory()
for exe in subjects(tmp.name):
    run = subprocess.run(["llvm-objdump", "-d", "--x86-asm-syntax=intel", exe], capture_output=True, text=True, errors="replace")
    if run.returncode < 0 and exe.endswith("/r.o"):
        crashed += 1
        continue
    assert run.returncode == 0, exe
    out = run.stdout
    rows = []
    for line in out.splitlines():
        m = LINE.match(line)
        # a prefix alone on a line is data between functions, where the disassembler gave up: no instruction.
        # Left out: d5, and 62 with a map of 4 or more or the bit above the map set (APX and half floats, which this disassembler reads
        # and no processor here runs);
        # 8f with a map (XOP, which only AMD's processors of 2011 to 2016 run);
        # 66 before a near call or jump (this disassembler reads 2 bytes as AMD does, Intel reads 4).
        if m and "<unknown>" not in m.group(3) and "(bad)" not in m.group(3) and not all(w.strip() in PREFIXES for w in m.group(3).split("\t") if w.strip()) \
           and not LEFT_OUT.match(m.group(2)):
            rows.append((m.group(1), m.group(2).split(), m.group(3)))
    # the decoder gets 15 bytes from each address on, as a debugger reading memory would, not the instruction alone
    data = [b for _, bs, _ in rows for b in bs]
    feed, at = [], 0
    for addr, bs, _ in rows:
        feed.append(addr + " " + " ".join(data[at:at + 15]))
        at += len(bs)
    got = subprocess.run([sys.argv[1]], input="\n".join(feed) + "\n", capture_output=True, text=True, check=True).stdout.splitlines()
    assert len(got) == len(rows), exe
    for (addr, bs, text), g in zip(rows, got):
        f = g.split()
        mine = (int(f[1]), int(f[2]), int(f[3]), int(f[4], 16), int(f[5]), int(f[6]))
        want = (len(bs),) + expected(text)
        total += 1
        if mine != want:
            bad += 1
            if bad <= 20: print(f"{exe} {addr}: {' '.join(bs)} {text!r}: got {mine}, want {want}")
print(f"x64: {total} instructions, {bad} differ" + (f" ({crashed} pieces left out)" if crashed else ""))
sys.exit(1 if bad or not total else 0)
