# Simulate the DLL's line wrapping on every translated string and report problems.
import sys, os, json, glob, re, collections
sys.path.insert(0, os.path.dirname(__file__))
import make_patch as mp

ROOT = mp.ROOT
TAB = {' ': 6, 'I': 4, 'L': 5, ',': 3, '.': 2, '(': 4, ')': 4, '!': 3, '?': 5, '1': 3, "'": 3, ':': 3,
       '&': 5, '%': 5, '<': 5, '>': 5}
def w_ascii(c):
    if c in TAB: return TAB[c]
    if c.isalnum() or c in '-#/_': return 6
    if c in '~': return 0
    return 5

def chars(s):
    """list of (text, width, is_jp) units; %s etc replaced by an 8-letter hero name"""
    out = []
    for t, v in mp.tokens(s):
        if t == 'j':
            out.append((v, 8, True))
        else:
            ch = chr(v)
            if 0xf0 <= v <= 0xf3: out.append(('', 0, False)); continue
            if v == 0x0d: continue
            out.append((ch, w_ascii(ch), False))
    # expand %s / %d placeholders
    res = []
    i = 0
    txt = ''.join(c[0] for c in out)
    for c in out:
        res.append(c)
    s2 = []
    j = 0
    while j < len(res):
        if res[j][0] == '%' and j + 1 < len(res):
            k = j + 1
            while k < len(res) and res[k][0] in '-+ #0123456789.l': k += 1
            if k < len(res) and res[k][0] in 'sdSu':
                rep = 'ARBOSHXX' if res[k][0] in 'sS' else '123'
                s2 += [(ch, w_ascii(ch), False) for ch in rep]
                j = k + 1
                continue
        s2.append(res[j]); j += 1
    return s2

def nostart(u):
    t, w, jp = u
    return (t in mp.NO_START) if jp else (t != '' and t in ",.!?)]:;'%>")
def noend(u):
    t, w, jp = u
    return (t in mp.NO_END) if jp else (t != '' and t in "([<")

GLUE = '--glue' in sys.argv
def can_break(p, u):
    if not GLUE: return True
    # keep ASCII words/numbers attached to the neighbouring Japanese character
    if (not p[2] and p[0].isalnum()) or (not u[2] and u[0].isalnum()): return False
    return True
mp.can_break = can_break

def wrap(units, maxlen):
    lines = []; cur = []; width = 0
    last_ws = last_jp = None
    i = 0; start = 0
    forced = 0
    while i < len(units):
        u = units[i]
        if u[0] == '@':
            lines.append(units[start:i]); start = i + 1; i += 1; width = 0; last_ws = last_jp = None; continue
        if i > start:
            p = units[i - 1]
            if (u[2] or p[2]) and not nostart(u) and not noend(p) and mp.can_break(p, u):
                last_jp = i
        width += u[1]
        if width >= maxlen:
            if last_ws is not None and (last_jp is None or last_ws + 1 >= last_jp):
                lines.append(units[start:last_ws]); start = last_ws + 1
            elif last_jp is not None:
                lines.append(units[start:last_jp]); start = last_jp
            elif u[2] and i > start:
                lines.append(units[start:i]); start = i; forced += 1
            else:
                lines.append(units[start:i]); start = i + 1; forced += 1
            i = start; width = 0; last_ws = last_jp = None
            continue
        if u[0] == ' ' and i > start: last_ws = i
        i += 1
    lines.append(units[start:])
    return lines, forced

def main():
    maxlens = [int(x) for x in ([a for a in sys.argv[1:] if a.isdigit()] or ['160', '255', '300'])]
    probs = collections.Counter(); samples = collections.defaultdict(list)
    total = 0
    for f in sorted(glob.glob(os.path.join(ROOT, 'text', 'ja', '*.json'))):
        nm = os.path.basename(f)[:-5]
        for idx, s in enumerate(json.load(open(f, encoding='utf-8'))['strings']):
            u = chars(s)
            if not any(x[2] for x in u): continue
            for ml in maxlens:
                lines, forced = wrap(u, ml)
                total += 1
                for k, ln in enumerate(lines):
                    txt = ''.join(x[0] for x in ln)
                    if k > 0 and ln and nostart(ln[0]):
                        probs['line starts with no-start char'] += 1; samples['start'].append((nm, idx, ml, txt[:20]))
                    if ln and noend(ln[-1]) and k < len(lines) - 1:
                        probs['line ends with opening bracket'] += 1; samples['end'].append((nm, idx, ml, txt[-20:]))
                    if sum(x[1] for x in ln) > ml:
                        probs['line wider than box'] += 1; samples['wide'].append((nm, idx, ml, txt))
                if forced:
                    probs['forced break (no break point)'] += 1; samples['forced'].append((nm, idx, ml))
    print('checked', total, 'wrap runs')
    for k, v in probs.items(): print(' ', k, v)
    for k, v in samples.items():
        print('--', k)
        for x in v[:8]: print('   ', x)

if __name__ == '__main__':
    main()
