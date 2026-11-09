#!/usr/bin/env python3
"""Read-only audit: simulate dyld load + bind closure over a sysroot.
Usage: symaudit.py SYSROOT ROOT_BIN [ROOT_BIN...]
Reports libraries that cannot be found and undefined symbols no named library exports.
"""
import os, re, subprocess, sys, collections

root = sys.argv[1]
def run(*a):
    return subprocess.run(a, capture_output=True, text=True).stdout

class Img: pass
imgs = {}

def load(install):
    if install in imgs: return imgs[install]
    p = root + install
    i = Img(); i.path = p; i.install = install; i.missing = not os.path.exists(p)
    i.deps = []; i.reexports = []; i.exports = set(); i.undef = []
    imgs[install] = i
    if i.missing: return i
    l = run('otool', '-l', p).splitlines()
    k = 0
    while k < len(l):
        m = re.match(r'\s+cmd (LC_\w+)', l[k])
        if m and m.group(1) in ('LC_LOAD_DYLIB','LC_REEXPORT_DYLIB','LC_LOAD_WEAK_DYLIB','LC_LOAD_UPWARD_DYLIB'):
            n = re.match(r'\s+name (\S+)', l[k+2])
            if n:
                i.deps.append((n.group(1), m.group(1)))
                if m.group(1) == 'LC_REEXPORT_DYLIB': i.reexports.append(n.group(1))
        k += 1
    for line in run('nm', '-m', p).splitlines():
        m = re.match(r'\s*[0-9a-f]*\s*\(undefined\)\s*(weak )?(?:\[?\w*\]?\s*)?external (\S+)(?: \(from (\S+)\))?', line.strip())
        if '(undefined)' in line and m:
            i.undef.append((m.group(2), m.group(3), bool(m.group(1)) or 'weak' in line))
            continue
        m2 = re.search(r'\(__\w+,__\w+\).*external (?:\[\w+\] )?(\S+)|\(indirect\).*external (\S+)|\(absolute\).*external (\S+)', line)
        if m2 and 'non-external' not in line:
            i.exports.add(m2.group(1) or m2.group(2) or m2.group(3))
    for d, _ in i.deps:
        load(d)
    return i

for b in sys.argv[2:]:
    load(b)

def exported(inst, sym, seen=None):
    seen = seen or set()
    if inst in seen or inst not in imgs: return False
    seen.add(inst)
    i = imgs[inst]
    return sym in i.exports or any(exported(r, sym, seen) for r in i.reexports)

def short(s): return os.path.basename(s) if s else s

print('images:', len(imgs))
print('MISSING LIBS:')
for k, i in sorted(imgs.items()):
    if i.missing:
        who = [short(x.install) for x in imgs.values() if any(d == k for d, _ in x.deps)]
        print(' ', k, '<-', ','.join(who)[:200])

bad = collections.defaultdict(list)
weakbad = 0
for k, i in sorted(imgs.items()):
    if i.missing: continue
    ordmap = {short(d): d for d, _ in i.deps}
    # basename without version suffix mapping used by nm -m (e.g. libSystem)
    def resolve(frm):
        for b, d in ordmap.items():
            stem = b.split('.')[0]
            if frm == stem or frm == stem.replace('lib','',1) or frm == b: return d
        return None
    for sym, frm, weak in i.undef:
        if sym == 'dyld_stub_binder' : continue
        if frm is None or frm in ('dynamically', 'flat'):
            # flat lookup: any image
            ok = any(sym in x.exports for x in imgs.values())
        elif frm == 'executable':
            ok = True
        else:
            d = resolve(frm)
            ok = exported(d, sym) if d else False
            if not ok:
                # dyld does not fall back, but flag separately when found elsewhere
                pass
        if not ok:
            if weak: weakbad += 1
            else:
                where = [short(x.install) for x in imgs.values() if sym in x.exports]
                bad[short(k)].append((sym, frm, where[:2]))

print('UNRESOLVED (non-weak):', sum(len(v) for v in bad.values()), ' weak-unresolved:', weakbad)
for k, v in sorted(bad.items(), key=lambda t: -len(t[1])):
    print(f'  {k}: {len(v)}')
    for sym, frm, where in sorted(v)[:40]:
        print(f'     {sym}  (from {frm})' + (f'  [exists in {where}]' if where else ''))
