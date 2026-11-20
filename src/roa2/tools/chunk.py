# Split English strings into translation chunks (text/chunks/cNN.en.txt) + glossary subsets.
import json, glob, os, re
ROOT = os.path.join(os.path.dirname(__file__), '..')
LIMIT = 13000
EXCLUDE = {'PRINTER.LTX', 'DEBUG.LTX', 'RCHEATXT.LTX', 'PHEXADEP.LTX', 'CREDITS.LTX', 'SOUND.LTX', 'LOW47CD.TLK'}

def order_key(f):
    u = json.load(open(f, encoding='utf-8'))
    nm = u['name']
    # keep related files together: by kind, then by name stem
    stem = re.sub(r'[0-9_]+[A-Z]?\..*$', '', nm.split('.')[0]) or nm
    return ({'ltx': 0, 'top': 0, 'fixed': 0, 'tlkb': 1, 'tlkt': 2}[u['kind']], stem, nm)

def main():
    global SPEAK
    sp = os.path.join(ROOT, 'text', 'tlk_speakers.json')
    SPEAK = json.load(open(sp, encoding='utf-8')) if os.path.exists(sp) else {}
    files = sorted(glob.glob(os.path.join(ROOT, 'text', 'en', '*.json')), key=order_key)
    chunks = []; cur = []; size = 0
    for f in files:
        u = json.load(open(f, encoding='utf-8'))
        nm = u['name']
        if nm in EXCLUDE:
            continue
        for i, s in enumerate(u['strings']):
            if s.strip() == '' :
                continue          # empty / blank strings are copied by merge.py
            l = len(s) + 8
            if size + l > LIMIT and cur and (size > LIMIT * 0.6 or i == 0):
                chunks.append(cur); cur = []; size = 0
            cur.append((nm, i, s)); size += l
    if cur:
        chunks.append(cur)
    od = os.path.join(ROOT, 'text', 'chunks'); os.makedirs(od, exist_ok=True)
    for k, c in enumerate(chunks):
        with open(os.path.join(od, 'c%02d.en.txt' % k), 'w', encoding='utf-8') as fp:
            last = None
            for nm, i, s in c:
                if nm != last:
                    fp.write('### FILE: %s\n' % nm); last = nm
                    if nm in SPEAK:
                        fp.write('### SPEAKER: %s\n' % SPEAK[nm])
                fp.write('[%d] %s\n' % (i, s))
    print(len(chunks), 'chunks', [sum(len(s) for _, _, s in c) for c in chunks])
    # glossary subsets
    gp = os.path.join(ROOT, 'text', 'glossary.json')
    if os.path.exists(gp):
        G = json.load(open(gp, encoding='utf-8'))
        for k in range(len(chunks)):
            f = os.path.join(od, 'c%02d.en.txt' % k)
            t = open(f, encoding='utf-8').read().upper()
            sub = []
            for e in G:
                en = e['en'].upper().strip()
                if not en:
                    continue
                if re.search(r'(?<![A-Z])' + re.escape(en) + r'(?![A-Z])', t):
                    sub.append({x: e[x] for x in ('en', 'ja', 'note') if e.get(x)})
            json.dump(sub, open(f.replace('.en.txt', '.gloss.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=0)

if __name__ == '__main__':
    main()
