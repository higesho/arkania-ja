# Extract translatable strings from STAR.DAT into text/en/<NAME>.json
#  kinds: ltx (NUL separated), tlkt (text dialog script), tlkb (binary dialog), top (TOPICS.TOP), inf (building names)
import sys, os, json, struct, re
sys.path.insert(0, os.path.dirname(__file__))
from datlib import read_star_dat
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))

def dec(b):
    out = []
    for x in b:
        if 0xf0 <= x <= 0xf5: out.append('{C%d}' % (x - 0xf0))
        elif x >= 0x80 or x < 0x20: out.append('{x%02X}' % x)
        else: out.append(chr(x))
    return ''.join(out)

QUOTE = re.compile(r'"([^"\r\n]*)"')

def tlk_text_units(s):
    """returns list of (kind, start, end, text) where [start,end) covers the quoted group in s"""
    lines = s.split('\n')
    pos = 0
    groups = []
    cur = None
    for ln in lines:
        body = ln.split('#')[0] if '"' not in ln.split('#')[0] else ln
        m = QUOTE.search(ln)
        code = ln[:m.start()] if m else ln.split('#')[0]
        if m and (code.strip() == '') and cur is not None:
            # continuation line
            cur[2] = pos + m.end()
            cur[3].append(m.group(1))
        elif m:
            if cur: groups.append(cur)
            kind = 'NAME' if code.strip().startswith('NAME') else 'MSG'
            cur = [kind, pos + m.start(), pos + m.end(), [m.group(1)]]
        else:
            if cur and ln.strip() and not ln.strip().startswith('#'):
                groups.append(cur); cur = None
        pos += len(ln) + 1
    if cur: groups.append(cur)
    return [(k, a, b, ' '.join(p)) for k, a, b, p in groups]

def find_tlkb_text(x):
    for p in range(4, len(x)):
        tl, cnt = struct.unpack_from('<HH', x, p - 4)
        if tl == len(x) - p and cnt and x[p:].count(b'\0') + 1 == cnt:
            return p
    raise ValueError('no text block')

def main():
    fs = read_star_dat(open(os.path.join(ROOT, 'orig', 'STAR.DAT'), 'rb').read())
    out = os.path.join(ROOT, 'text', 'en')
    os.makedirs(out, exist_ok=True)
    tot = 0; nch = 0; nfiles = 0
    seen = set()
    for idx, (nm, off, fl, x) in enumerate(fs):
        if not x or nm in seen: continue
        ext = nm.rsplit('.', 1)[-1]
        u = None
        if ext == 'LTX':
            parts = x.split(b'\0')
            trailing = parts[-1] == b''
            if trailing: parts = parts[:-1]
            u = {'kind': 'ltx', 'trailing_nul': trailing, 'strings': [dec(p) for p in parts]}
        elif ext == 'TLK':
            if b'\0' in x[:300]:
                p = find_tlkb_text(x)
                parts = x[p:].split(b'\0')[:-1]
                u = {'kind': 'tlkb', 'head_len': p, 'strings': [dec(q) for q in parts]}
            else:
                s = x.decode('latin1')
                g = tlk_text_units(s)
                u = {'kind': 'tlkt', 'spans': [[a, b] for k, a, b, t in g], 'kinds': [k for k, a, b, t in g],
                     'strings': [dec(t.encode('latin1')) for k, a, b, t in g]}
        elif ext == 'TOP':
            s = x.decode('latin1')
            ms = list(QUOTE.finditer(s))
            u = {'kind': 'top', 'spans': [[m.start(), m.end()] for m in ms], 'strings': [m.group(1) for m in ms]}
        elif ext == 'INF' and len(x) % 256 == 0:
            spans, ss = [], []
            for r in range(0, len(x), 256):
                rec = x[r:r + 256]
                if rec[:4] == b'\xff' * 4:
                    continue
                nmb = rec[4:].split(b'\0')[0]
                if nmb:
                    spans.append([r + 4, r + 256]); ss.append(dec(nmb))
            u = {'kind': 'fixed', 'spans': spans, 'strings': ss}
        elif ext == 'NPC' and x.startswith(b'DSA VERSION'):
            spans, ss = [], []
            for a in (0x12, 0x22):
                nmb = x[a:a + 16].split(b'\0')[0]
                if nmb:
                    spans.append([a, a + 16]); ss.append(dec(nmb))
            u = {'kind': 'fixed', 'spans': spans, 'strings': ss}
        if u is None or not u['strings']:
            continue
        seen.add(nm)
        u['name'] = nm
        u['index'] = idx
        json.dump(u, open(os.path.join(out, nm + '.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
        tot += len(u['strings']); nch += sum(len(t) for t in u['strings']); nfiles += 1
    print(nfiles, 'files', tot, 'strings', nch, 'chars')

if __name__ == '__main__':
    main()
