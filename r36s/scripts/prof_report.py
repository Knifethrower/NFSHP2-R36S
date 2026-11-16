#!/usr/bin/env python3
"""Tables from NFSHP2_PROFILE's output (r36s/platform/r36s_perf.c).

    python prof_report.py BINARY DIR [--top 40] [--tid N|all]

BINARY is the unstripped build (r36s/build/armhf/nfshp2.armhf); DIR holds
prof_samples.txt ("<pc hex> <tid>" per CPU sample, a thousand a second of
CPU time), prof_maps.txt (/proc/self/maps at exit) and prof_threads.txt
("<tid> <name>"). Prints each thread's share of the samples, then the
chosen thread's (the busiest by default) functions: the game's by name,
a library's as [library].
"""
import argparse
import bisect
import collections
import os
import struct
import sys


def elf_functions(path):
    """(start, size, name) of the FUNC symbols of a 32- or 64-bit little-endian ELF."""
    with open(path, 'rb') as f:
        d = f.read()
    if d[:4] != b'\x7fELF' or d[5] != 1:
        sys.exit(f'{path}: not a little-endian ELF')
    is64 = d[4] == 2
    if is64:
        shoff = struct.unpack_from('<Q', d, 0x28)[0]
        shentsize, shnum, shstrndx = struct.unpack_from('<HHH', d, 0x3A)
        sec_fmt, sym_fmt, sym_size = '<IIQQQQIIQQ', '<IBBHQQ', 24
    else:
        shoff = struct.unpack_from('<I', d, 0x20)[0]
        shentsize, shnum, shstrndx = struct.unpack_from('<HHH', d, 0x2E)
        sec_fmt, sym_fmt, sym_size = '<IIIIIIIIII', '<IIIBBH', 16
    secs = [struct.unpack_from(sec_fmt, d, shoff + i * shentsize) for i in range(shnum)]
    names_off = secs[shstrndx][4]

    def sname(s):
        o = names_off + s[0]
        return d[o:d.index(b'\0', o)].decode('ascii', 'replace')

    by_name = {sname(s): s for s in secs}
    tab = by_name.get('.symtab')
    if tab is None:
        sys.exit(f'{path}: no symbol table (use the unstripped build)')
    strtab = secs[tab[6]]
    out = []
    for i in range(tab[5] // sym_size):
        fields = struct.unpack_from(sym_fmt, d, tab[4] + i * sym_size)
        if is64:
            st_name, st_info, _, _, value, size = fields
        else:
            st_name, value, size, st_info, _, _ = fields
        if (st_info & 0xF) != 2 or not size or not value:
            continue
        o = strtab[4] + st_name
        name = d[o:d.index(b'\0', o)].decode('ascii', 'replace')
        out.append((value & ~1, size, name))   # Thumb bit off
    out.sort()
    return out


def read_maps(path):
    rows = []
    if not os.path.exists(path):
        return rows
    with open(path) as f:
        for line in f:
            p = line.split()
            if len(p) < 6:
                continue
            lo, hi = (int(x, 16) for x in p[0].split('-'))
            rows.append((lo, hi, os.path.basename(p[5])))
    rows.sort()
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('binary')
    ap.add_argument('dir')
    ap.add_argument('--top', type=int, default=40)
    ap.add_argument('--tid', default='top')
    a = ap.parse_args()

    funcs = elf_functions(a.binary)
    starts = [f[0] for f in funcs]
    maps = read_maps(os.path.join(a.dir, 'prof_maps.txt'))
    mstarts = [m[0] for m in maps]
    names = {}
    tpath = os.path.join(a.dir, 'prof_threads.txt')
    if os.path.exists(tpath):
        with open(tpath) as f:
            for line in f:
                p = line.split(None, 1)
                if len(p) == 2:
                    names[int(p[0])] = p[1].strip()

    samples = []
    with open(os.path.join(a.dir, 'prof_samples.txt')) as f:
        for line in f:
            p = line.split()
            if len(p) == 2:
                samples.append((int(p[0], 16), int(p[1])))
    if not samples:
        sys.exit('no samples')

    def label(pc):
        i = bisect.bisect_right(starts, pc) - 1
        if i >= 0 and pc < funcs[i][0] + funcs[i][1]:
            return funcs[i][2]
        j = bisect.bisect_right(mstarts, pc) - 1
        if j >= 0 and pc < maps[j][1]:
            return f'[{maps[j][2]}]'
        return '[?]'

    tids = collections.Counter(t for _, t in samples)
    total = len(samples)
    print(f'{total} samples ({total / 1000.0:.1f} s of CPU)\n')
    print('| thread | samples | share |\n| --- | ---: | ---: |')
    for t, n in tids.most_common():
        print(f'| {t} {names.get(t, "")} | {n} | {100.0 * n / total:.1f}% |')

    chosen = tids.most_common(1)[0][0] if a.tid == 'top' else None if a.tid == 'all' else int(a.tid)
    pcs = [pc for pc, t in samples if chosen is None or t == chosen]
    c = collections.Counter(label(pc) for pc in pcs)
    libs = collections.Counter()
    for k, n in c.items():
        libs['the game' if not k.startswith('[') else k] += n
    who = 'all threads' if chosen is None else f'thread {chosen} {names.get(chosen, "")}'
    print(f'\n{who}: {len(pcs)} samples\n')
    print('| where | share |\n| --- | ---: |')
    for k, n in libs.most_common():
        print(f'| {k} | {100.0 * n / len(pcs):.1f}% |')
    print(f'\n| function | share |\n| --- | ---: |')
    for k, n in c.most_common(a.top):
        print(f'| {k} | {100.0 * n / len(pcs):.1f}% |')


if __name__ == '__main__':
    main()
