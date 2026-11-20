# Build build/patch.bin (embedded into winmm.dll) from translations.
import sys, os, json, struct, re, zlib, collections, glob
sys.path.insert(0, os.path.dirname(__file__))
from datlib import exe_names, read_dat

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
BDF = os.path.join(ROOT, 'font', 'misaki_gothic_2nd.bdf')
LINE_H = 7
CB_BASE = 0x78
LOG = 0

LEADS = list(range(0x80, 0xa0))      # 32
WIDES = list(range(0xa0, 0xf0))      # 80

UMLAUT = {0x8e: b'AE', 0x84: b'AE', 0x99: b'OE', 0x94: b'OE', 0x9a: b'UE', 0x81: b'UE', 0xe1: b'SS'}
NO_START = set('、。，．・：；？！ー…‥」』）］｝〕〉》】ぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮヵヶ々〜～!?.,)]:;')
NO_END = set('「『（［｛〔〈《【')

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

TOKEN = re.compile(r'\{C([0-3])\}|\{x([0-9A-Fa-f]{2})\}|(%%|%[-+ #0-9.]*l?[a-z])|(.)', re.S)

FW = {chr(0xff01 + i): chr(0x21 + i) for i in range(94)}   # full-width ASCII -> ASCII
for k in '！？（）％：、。「」・ー＿':   # keep these Japanese punctuation as Japanese glyphs
    FW.pop(k, None)

def tokens(s):
    """yield ('b', int) raw byte or ('j', ch) Japanese char"""
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
            o = ord(ch)
            if o < 0x80:
                if 'a' <= ch <= 'z':
                    ch = ch.upper()
                yield ('b', ord(ch))
            else:
                yield ('j', ch)

def main():
    exe = open(os.path.join(ROOT, 'orig', 'BLADEM.EXE'), 'rb').read()
    dat = open(os.path.join(ROOT, 'orig', 'BLADE.DAT'), 'rb').read()
    names = exe_names(exe)
    files = read_dat(dat)
    name2idx = {n: i for i, n in enumerate(names) if n}

    trans = {}
    for f in sorted(glob.glob(os.path.join(ROOT, 'text', sys.argv[1] if len(sys.argv) > 1 else 'ja', '*.json'))):
        nm = os.path.basename(f)[:-5]
        ja = json.load(open(f, encoding='utf-8'))
        en = json.load(open(os.path.join(ROOT, 'text', 'en', nm + '.json'), encoding='utf-8'))
        if len(ja['strings']) != len(en['strings']):
            raise SystemExit('%s: count mismatch %d != %d' % (nm, len(ja['strings']), len(en['strings'])))
        trans[nm] = (en, ja)

    # character frequency
    freq = collections.Counter()
    for nm, (en, ja) in trans.items():
        for s in ja['strings']:
            for t, v in tokens(s):
                if t == 'j':
                    freq[v] += 1
    EXE_STRINGS = json.load(open(os.path.join(ROOT, 'text', 'exe_strings.json'), encoding='utf-8'))
    for e in EXE_STRINGS:
        for t, v in tokens(e['ja']):
            if t == 'j':
                freq[v] += 1
    freq['の'] += 1
    TLK_TITLES = json.load(open(os.path.join(ROOT, 'text', 'tlk_titles.json'), encoding='utf-8'))
    for v in TLK_TITLES.values():
        for ch in v:
            freq[ch] += 1
    bdf = load_bdf(BDF)
    missing = [c for c in freq if ord(c) not in bdf]
    if missing:
        print('WARNING: no glyph for', ''.join(sorted(missing)))
    chars = [c for c, _ in freq.most_common() if ord(c) in bdf]
    if len(chars) > len(WIDES) + len(LEADS) * len(WIDES):
        raise SystemExit('too many distinct characters: %d' % len(chars))
    code = {}
    for i, c in enumerate(chars):
        if i < len(WIDES):
            code[c] = bytes([WIDES[i]])
        else:
            j = i - len(WIDES)
            code[c] = bytes([LEADS[j // len(WIDES)], WIDES[j % len(WIDES)]])
    print('distinct chars %d (single-byte %d)' % (len(chars), min(len(chars), len(WIDES))))

    def encode(s):
        out = bytearray()
        toks = list(tokens(s))
        if any(t == 'j' for t, v in toks):
            # hard CRs of the English layout are meaningless for Japanese wrapping
            toks = [(t, v) for t, v in toks if not (t == 'b' and v == 0x0d)]
            # spaces between Japanese characters would show up as 6px gaps
            toks = [tv for i, tv in enumerate(toks)
                    if not (tv == ('b', 0x20) and 0 < i < len(toks) - 1
                            and toks[i - 1][0] == 'j' and toks[i + 1][0] == 'j')]
        for t, v in toks:
            if t == 'b' and v in UMLAUT:
                out += UMLAUT[v]
            elif t == 'b':
                out.append(v)
            elif v in code:
                out += code[v]
            else:
                out += b'?'
        return bytes(out)

    # DAT replacements
    reps = []
    named = []
    report = []
    gdat = open(os.path.join(ROOT, 'orig', 'DSAGEN.DAT'), 'rb').read()
    for nm, (en, ja) in trans.items():
        if en.get('archive') == 'DSAGEN.DAT':
            body = b'\0'.join(encode(s) for s in ja['strings'])
            if en['trailing_nul']:
                body += b'\0'
            named.append((nm, body))
            continue
        idx = en['index']
        orig = files[idx]
        keep = en['head_len']          # leading bytes taken from the user's own BLADE.DAT at runtime
        head = orig[:keep]
        hpatch = []
        if nm.endswith('.TLK'):
            # dialog partner titles: struct of 0x26 bytes, title at +6 (30 bytes)
            npart = struct.unpack_from('<H', head, 4)[0]
            for k in range(npart):
                o = 6 + k * 0x26 + 6
                t = bytes(head[o:o + 30]).split(b'\0')[0].decode('latin1')
                if t in TLK_TITLES:
                    jt = encode(TLK_TITLES[t])
                    if len(jt) > 29:
                        raise SystemExit('title too long: ' + t)
                    hpatch.append((o, jt + b'\0' * (30 - len(jt))))
        body = b'\0'.join(encode(s) for s in ja['strings'])
        if en['trailing_nul']:
            body += b'\0'
        new = head + body
        reps.append((idx, keep, hpatch, body))
        report.append((nm, len(orig), len(new)))
    for nm, a, b in sorted(report, key=lambda r: r[2] / max(r[1], 1), reverse=True)[:15]:
        print('  %-14s %6d -> %6d  (%.0f%%)' % (nm, a, b, 100.0 * b / max(a, 1)))
    # memory buffer limits of the English BLADEM.EXE
    over = []
    for nm, a, b in report:
        ext = nm.rsplit('.', 1)[-1]
        head = trans[nm][0]['head_len']
        if nm == 'TEXT.LTX':
            lim = 25500
        elif nm == 'ITEMNAME':
            lim = 3880
        elif nm == 'MONNAMES':
            lim = 950
        elif ext == 'DTX':
            lim = 12000
        elif ext == 'TLK':
            lim = head + 22000
        else:
            lim = max(a, 10000)
        if b > lim:
            over.append('%s %d > %d' % (nm, b, lim))
    if over:
        print('SIZE LIMIT EXCEEDED:', '; '.join(over))
        if '--force' not in sys.argv:
            raise SystemExit(1)

    # EXE patches (file offsets in BLADEM.EXE, English 3.x)
    def cb(n):
        return bytes([0xfe, 0x38, CB_BASE + n, 0x00])
    exe_patches = [
        (0x6af9a, cb(4) + b'\xcb'),                                   # GUI_count_lines
        (0x6b206, cb(5) + b'\xe9' + struct.pack('<h', 0x6b33f - (0x6b206 + 7))),  # GUI_print_string body
        (0x6b348, cb(6) + b'\xcb'),                                   # GUI_print_char
        (0x6b383, cb(7) + b'\xcb'),                                   # GUI_lookup_char_width
    ]
    gen_patches = [
        (0x5efd, cb(0) + b'\xcb'),
        (0x608b, cb(1) + b'\xe9' + struct.pack('<h', 0x6191 - (0x608b + 7))),
        (0x6199, cb(2) + b'\xcb'),
        (0x61c8, cb(3) + b'\xcb'),
    ]
    # strings inside the executables (patched in place, never longer than the original)
    gexe0 = open(os.path.join(ROOT, 'orig', 'GEN.EXE'), 'rb').read()
    DSF = 0x2200 + 0x146b * 16
    for e in EXE_STRINGS:
        img = exe if e['exe'] == 'BLADEM.EXE' else gexe0
        target = exe_patches if e['exe'] == 'BLADEM.EXE' else gen_patches
        en_b = e['en'].encode('latin1')
        pos = [m.start() for m in re.finditer(rb'(?<![\x20-\x7e])' + re.escape(en_b) + rb'\x00', img)]
        if e.get('ctx') == 'grammar':
            pos = [p for p in pos if DSF + 0xa9e0 <= p < DSF + 0xaa50]
        if not pos or (len(pos) > 1 and not e.get('all')):
            raise SystemExit('exe string %r: %d matches' % (e['en'], len(pos)))
        enc = encode(e['ja'])
        if len(enc) > len(en_b):
            raise SystemExit('exe string too long: %r -> %r (%d > %d)' % (e['en'], e['ja'], len(enc), len(en_b)))
        for p in pos:
            target.append((p, enc + b'\0' * (len(en_b) - len(enc))))
    # possessive "'S" -> "の" in GUI_name_plural
    no = code.get('の')
    if no and len(no) == 1:
        exe_patches.append((0x6ae20, no))
        exe_patches.append((0x6ae24, b'\xeb\x12\x90'))

    sects = []
    def sect(t, payload):
        sects.append(struct.pack('<4sI', t.encode()[::-1][::-1].ljust(4, b'\0'), len(payload)) + payload)
    # type is stored as u32 little endian of the char
    def sect_t(ch, payload):
        sects.append(struct.pack('<II', ord(ch), len(payload)) + payload)

    cls = bytearray(256)
    for b in LEADS:
        cls[b] = 1
    for b in WIDES:
        cls[b] = 2
    sect_t('E', bytes(cls))
    sect_t('P', struct.pack('<III', LINE_H, CB_BASE, LOG))
    codes = []
    bitmaps = b''
    flags = b''
    for c in chars:
        e = code[c]
        codes.append(e[0] if len(e) == 1 else (e[0] << 8) | e[1])
        bitmaps += bdf[ord(c)]
        flags += bytes([(1 if c in NO_START else 0) | (2 if c in NO_END else 0)])
    sect_t('F', struct.pack('<I', len(codes)) + struct.pack('<%dH' % len(codes), *codes) + bitmaps + flags)
    xp = struct.pack('<II', len(exe), zlib.crc32(exe) & 0xffffffff)
    for off, b in sorted(exe_patches):
        xp += struct.pack('<II', off, len(b)) + b
    sect_t('X', b'BLADEM.EXE'.ljust(16, b'\0') + xp)
    gexe = open(os.path.join(ROOT, 'orig', 'GEN.EXE'), 'rb').read()
    xp = struct.pack('<II', len(gexe), zlib.crc32(gexe) & 0xffffffff)
    for off, b in sorted(gen_patches):
        xp += struct.pack('<II', off, len(b)) + b
    sect_t('X', b'GEN.EXE'.ljust(16, b'\0') + xp)
    if named:
        np_ = struct.pack('<III', len(gdat), zlib.crc32(gdat) & 0xffffffff, len(named))
        for nm, b in named:
            np_ += nm.encode().ljust(16, b'\0') + struct.pack('<I', len(b)) + b
        sect_t('N', b'DSAGEN.DAT'.ljust(16, b'\0') + np_)
    dp = struct.pack('<III', len(dat), zlib.crc32(dat) & 0xffffffff, len(reps))
    for idx, keep, hpatch, body in reps:
        dp += struct.pack('<III', idx, keep, len(hpatch))
        for o, b in hpatch:
            dp += struct.pack('<II', o, len(b)) + b
        dp += struct.pack('<I', len(body)) + body
    sect_t('D', b'BLADE.DAT'.ljust(16, b'\0') + dp)

    blob = struct.pack('<4sII', b'RJP1', 1, len(sects)) + b''.join(sects)
    os.makedirs(os.path.join(ROOT, 'build'), exist_ok=True)
    open(os.path.join(ROOT, 'build', 'patch.bin'), 'wb').write(blob)
    json.dump({c: code[c].hex() for c in chars}, open(os.path.join(ROOT, 'build', 'charmap.json'), 'w', encoding='utf-8'), ensure_ascii=False)
    print('patch.bin %d bytes, %d files replaced' % (len(blob), len(reps)))

if __name__ == '__main__':
    main()
