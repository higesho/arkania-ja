# Build build/patch.bin (embedded into winmm.dll) from text/<dir>/*.json translations.
#   python -X utf8 tools/make_patch.py [ja]
import sys, os, json, struct, re, zlib, collections, glob
sys.path.insert(0, os.path.dirname(__file__))
from datlib import read_star_dat

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
BDF = os.path.join(ROOT, 'font', 'misaki_gothic_2nd.bdf')
CB_BASE = 0x78
LOG = int(os.environ.get('RJP_LOG', '0'))

LEADS = list(range(0x80, 0xa0)) + list(range(0xf6, 0xff))
WIDES = list(range(0xa0, 0xf0))

UMLAUT = {0x8e: b'Ae', 0x84: b'ae', 0x99: b'Oe', 0x94: b'oe', 0x9a: b'Ue', 0x81: b'ue', 0xe1: b'ss'}
NO_START = set('、。，．・：；？！ー…‥」』）］｝〕〉》】ぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮヵヶ々〜～!?.,)]:;')
NO_END = set('「『（［｛〔〈《【')
TLK_LINE = 78          # max bytes inside one quoted line of a text dialogue script

def load_bdf(path):
    glyphs = {}
    cur = None
    lines = open(path, encoding='latin1').read().split('\n')
    i = 0
    while i < len(lines):
        l = lines[i]
        if l.startswith('ENCODING'):
            cur = int(l.split()[1])
        elif l.startswith('BBX'):
            w, h, xo, yo = map(int, l.split()[1:])
        elif l == 'BITMAP':
            rows = [0] * 8
            top = 7 - yo - h
            for r in range(h):
                v = int(lines[i + 1 + r], 16) >> xo
                if 0 <= top + r < 8:
                    rows[top + r] = v & 0xff
            glyphs[cur] = bytes(rows)
            i += h
        i += 1
    return glyphs

TOKEN = re.compile(r'\{C([0-5])\}|\{x([0-9A-Fa-f]{2})\}|(%%|%[-+ #0-9.]*l?[a-zA-Z])|(.)', re.S)
FW = {chr(0xff01 + i): chr(0x21 + i) for i in range(94)}
for k in '！？（）％：、。「」・ー＿':
    FW.pop(k, None)

def tokens(s):
    for m in TOKEN.finditer(s):
        if m.group(1) is not None:
            yield ('b', 0xf0 + int(m.group(1)))
        elif m.group(2) is not None:
            yield ('b', int(m.group(2), 16))
        elif m.group(3) is not None:
            for ch in m.group(3):
                yield ('b', ord(ch))
        else:
            ch = m.group(4)
            ch = FW.get(ch, ch)
            if ch == '　':
                ch = ' '
            if ord(ch) < 0x80:
                yield ('b', ord(ch))
            else:
                yield ('j', ch)

class Encoder:
    def __init__(self, code):
        self.code = code
    def units(self, s):
        """list of encoded characters (bytes objects), never splitting a Japanese char"""
        toks = list(tokens(s))
        if any(t == 'j' for t, v in toks):
            toks = [(t, v) for t, v in toks if not (t == 'b' and v == 0x0d)]
            toks = [tv for i, tv in enumerate(toks)
                    if not (tv == ('b', 0x20) and 0 < i < len(toks) - 1
                            and toks[i - 1][0] == 'j' and toks[i + 1][0] == 'j')]
        out = []
        for t, v in toks:
            if t == 'b' and v in UMLAUT and any(tt == 'j' for tt, vv in toks):
                out.append(UMLAUT[v])
            elif t == 'b':
                out.append(bytes([v]))
            elif v in self.code:
                out.append(self.code[v])
            else:
                out.append(b'?')
        return out
    def encode(self, s):
        return b''.join(self.units(s))

def wrap_tlk(units, isjp):
    """split encoded units into quoted lines of <= TLK_LINE bytes; prefer splits between Japanese chars"""
    lines, cur = [], []
    def L(x): return sum(len(u) for u in x)
    i = 0
    while i < len(units):
        cur.append(units[i]); i += 1
        if L(cur) >= TLK_LINE - 2 and i < len(units):
            # find a split point: between two JP units, else after a space
            k = len(cur)
            best = None
            for j in range(k - 1, max(0, k - 30), -1):
                a, b = cur[j - 1], cur[j]
                if (a[0] >= 0x80 and b[0] >= 0x80):
                    best = j; break
                if a == b' ':
                    best = j; break
            if best is None:
                best = k
            rest = cur[best:]
            line = cur[:best]
            if line and line[-1] == b' ' and not isjp:
                line = line[:-1]
            lines.append(b''.join(line)); cur = rest
    if cur or not lines:
        lines.append(b''.join(cur))
    return lines

def main():
    tdir = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith('-') else 'ja'
    exe = open(os.path.join(ROOT, 'orig', 'STAR.EXE'), 'rb').read()
    dat = open(os.path.join(ROOT, 'orig', 'STAR.DAT'), 'rb').read()
    entries = read_star_dat(dat)

    trans = {}
    for f in sorted(glob.glob(os.path.join(ROOT, 'text', tdir, '*.json'))):
        nm = os.path.basename(f)[:-5]
        ja = json.load(open(f, encoding='utf-8'))
        # an ASCII ':' right after a Japanese char touches it and reads as '!': use the wide colon
        ja['strings'] = [re.sub(r'([぀-鿿！-ﾟ]):(?: (?=[^ ]))?', r'\1：', x) for x in ja['strings']]
        en = json.load(open(os.path.join(ROOT, 'text', 'en', nm + '.json'), encoding='utf-8'))
        if len(ja['strings']) != len(en['strings']):
            raise SystemExit('%s: count mismatch %d != %d' % (nm, len(ja['strings']), len(en['strings'])))
        trans[nm] = (en, ja)
    # LOW47CD.TLK is the CD twin of LOW47.TLK
    if 'LOW47.TLK' in trans and 'LOW47CD.TLK' not in trans:
        en47 = json.load(open(os.path.join(ROOT, 'text', 'en', 'LOW47CD.TLK.json'), encoding='utf-8'))
        trans['LOW47CD.TLK'] = (en47, trans['LOW47.TLK'][1])

    freq = collections.Counter()
    for nm, (en, ja) in trans.items():
        for s in ja['strings']:
            for t, v in tokens(s):
                if t == 'j':
                    freq[v] += 1
    bdf = load_bdf(BDF)
    missing = [c for c in freq if ord(c) not in bdf]
    if missing:
        print('WARNING: no glyph for', ''.join(sorted(missing)))
    chars = [c for c, _ in freq.most_common() if ord(c) in bdf]
    cap = len(WIDES) + len(LEADS) * len(WIDES)
    if len(chars) > cap:
        raise SystemExit('too many distinct characters: %d > %d' % (len(chars), cap))
    code = {}
    for i, c in enumerate(chars):
        if i < len(WIDES):
            code[c] = bytes([WIDES[i]])
        else:
            j = i - len(WIDES)
            code[c] = bytes([LEADS[j // len(WIDES)], WIDES[j % len(WIDES)]])
    print('distinct chars %d (capacity %d)' % (len(chars), cap))
    enc = Encoder(code)

    # ---- STAR.DAT splices
    reps = []
    report = []
    for nm, (en, ja) in sorted(trans.items(), key=lambda x: x[1][0]['index']):
        idx = en['index']
        name, off, fl, orig = entries[idx]
        assert name == nm, (name, nm)
        kind = en['kind']
        spl = []
        if kind == 'ltx':
            body = b'\0'.join(enc.encode(s) for s in ja['strings'])
            if en['trailing_nul']:
                body += b'\0'
            spl.append((0, len(orig), body))
            new_len = len(body)
        elif kind == 'tlkb':
            p = en['head_len']
            body = b'\0'.join(enc.encode(s) for s in ja['strings']) + b'\0'
            cnt = struct.unpack_from('<H', orig, p - 2)[0]
            if len(body) > 0xffff:
                raise SystemExit(nm + ' too long')
            spl.append((p - 4, len(orig), struct.pack('<HH', len(body), cnt) + body))
            new_len = p + len(body)
        elif kind in ('tlkt', 'top'):
            for k, ((a, b), s) in enumerate(zip(en['spans'], ja['strings'])):
                u = enc.units(s)
                isjp = any(x[0] >= 0x80 for x in u)
                if kind == 'top' or (en['kinds'][k] == 'NAME'):
                    q = b'"' + b''.join(u) + b'"'
                else:
                    q = b'\r\n\t\t\t'.join(b'"' + l + b'"' for l in wrap_tlk(u, isjp))
                if b'"' in b''.join(u):
                    raise SystemExit('quote char in %s[%d]' % (nm, k))
                spl.append((a, b, q))
            new_len = len(orig) + sum(len(q) - (b - a) for a, b, q in spl)
        elif kind == 'fixed':
            # fixed-size, NUL padded name fields (INF building names, NPC hero names)
            for (a, b), s in zip(en['spans'], ja['strings']):
                q = enc.encode(s)
                if len(q) >= b - a:
                    raise SystemExit('%s: "%s" too long (%d >= %d)' % (nm, s, len(q), b - a))
                spl.append((a, b, q + b'\0' * (b - a - len(q))))
            new_len = len(orig)
        else:
            continue
        reps.append((idx, spl))
        report.append((nm, len(orig), new_len))
    for nm, a, b in sorted(report, key=lambda r: r[2] / max(r[1], 1), reverse=True)[:12]:
        print('  %-14s %6d -> %6d  (%.0f%%)' % (nm, a, b, 100.0 * b / max(a, 1)))

    # ---- STAR.EXE patches (file offsets; text module segment at file 0xf780)
    def cb(n):
        return bytes([0xfe, 0x38, CB_BASE + n, 0x00])
    SEG = 0xf780
    PROT = 0xf1e0   # resident segment 0ee0 (copy protection)
    exe_patches = [
        (SEG + 0x103, cb(0) + b'\xcb'),                                      # get_width
        (SEG + 0x2b5, cb(1) + b'\xcb'),                                      # print_char
        (SEG + 0x794, cb(2) + b'\xe9' + struct.pack('<h', 0x9ae - (0x794 + 7))),  # print_string body
        (SEG + 0xa5b, cb(3) + b'\xcb'),                                      # wrap / count lines
        (SEG + 0x717, cb(4) + b'\xcb'),                                      # string width
        # copy protection question 0ee0:0467: push bp; mov bp,sp; callback formats the
        # question (an extra %s gets the decoded answer); jmp past the original sprintf
        (PROT + 0x467, b'\x55\x8b\xec' + cb(5) + b'\xe9' + struct.pack('<h', 0x4a3 - (0x467 + 10))),
    ]
    expect = {SEG + 0x103: b'\x55\x8b\xec\x83\xec\x04', SEG + 0x2b5: b'\x55\x8b\xec\x83\xec\x02',
              SEG + 0x794: b'\xc4\x5e\x06\x26\x83\x7f\x31', SEG + 0xa5b: b'\x55\x8b\xec\x83\xec\x10',
              SEG + 0x717: b'\x55\x8b\xec\x83\xec\x02',
              PROT + 0x467: b'\x55\x8b\xec\xc4\x5e\x06\x26\x8a\x47\x26',
              PROT + 0x49b: b'\x9a\xff\x40\x00\x00\x83\xc4\x0e'}
    for o, b in expect.items():
        if exe[o:o + len(b)] != b:
            raise SystemExit('STAR.EXE does not match at %x' % o)

    sects = []
    def sect_t(ch, payload):
        sects.append(struct.pack('<II', ord(ch), len(payload)) + payload)
    cls = bytearray(256)
    for b in LEADS:
        cls[b] = 1
    for b in WIDES:
        cls[b] = 2
    sect_t('E', bytes(cls))
    sect_t('P', struct.pack('<II', CB_BASE, LOG))
    codes = []; bitmaps = b''; flags = b''
    for c in chars:
        e = code[c]
        codes.append(e[0] if len(e) == 1 else (e[0] << 8) | e[1])
        bitmaps += bdf[ord(c)]
        flags += bytes([(1 if c in NO_START else 0) | (2 if c in NO_END else 0)])
    sect_t('F', struct.pack('<I', len(codes)) + struct.pack('<%dH' % len(codes), *codes) + bitmaps + flags)
    xp = struct.pack('<II', len(exe), zlib.crc32(exe) & 0xffffffff)
    for off, b in sorted(exe_patches):
        xp += struct.pack('<II', off, len(b)) + b
    sect_t('X', b'STAR.EXE'.ljust(16, b'\0') + xp)
    dp = struct.pack('<III', len(dat), zlib.crc32(dat) & 0xffffffff, len(reps))
    for idx, spl in reps:
        dp += struct.pack('<II', idx, len(spl))
        for a, b, q in sorted(spl):
            dp += struct.pack('<III', a, b, len(q)) + q
    sect_t('S', b'STAR.DAT'.ljust(16, b'\0') + dp)

    blob = struct.pack('<4sII', b'RJP2', 1, len(sects)) + b''.join(sects)
    os.makedirs(os.path.join(ROOT, 'build'), exist_ok=True)
    open(os.path.join(ROOT, 'build', 'patch.bin'), 'wb').write(blob)
    json.dump({c: code[c].hex() for c in chars}, open(os.path.join(ROOT, 'build', 'charmap.json'), 'w', encoding='utf-8'), ensure_ascii=False)
    print('patch.bin %d bytes, %d entries replaced' % (len(blob), len(reps)))

if __name__ == '__main__':
    main()
