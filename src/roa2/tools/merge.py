# Merge translated chunks (text/chunks/cNN.ja.txt) + text/fixes.json into text/ja/<FILE>.json and validate.
import json, glob, os, re, sys, collections
ROOT = os.path.join(os.path.dirname(__file__), '..')
EN = os.path.join(ROOT, 'text', 'en')
JA = os.path.join(ROOT, 'text', 'ja')

FMT = re.compile(r'%%|%[-+ #0-9.]*l?[a-zA-Z]')
CTL = re.compile(r'\{C[0-5]\}|\{x(?!0D)[0-9A-Fa-f]{2}\}|~|\^|@')
NAMEFILES = ('ITEMS.LTX', 'MONNAMES.LTX')

def fmts(s):
    return ['%s' if f == '%.0s' else f for f in FMT.findall(s)]

def check(en, ja, nm):
    errs = []
    if fmts(en) != fmts(ja):
        errs.append('format %s != %s' % (FMT.findall(en), FMT.findall(ja)))
    ce, cj = CTL.findall(en), CTL.findall(ja)
    # @ and ^ may move/merge a little; colours, raw bytes and tabs must match exactly
    strict = lambda l: [x for x in l if x not in ('@', '^')]
    if strict(ce) != strict(cj):
        errs.append('controls %s != %s' % (strict(ce), strict(cj)))
    if en.strip() and not ja.strip():
        errs.append('empty')
    if nm in NAMEFILES and en.count('.') == 2 and not re.fullmatch(r'[^.]*\.\.', ja):
        errs.append('name needs form X..')
    if '<' in ja or '>' in ja:
        if ja.count('<') + ja.count('>') > 0 and en.count('<') + en.count('>') > 0:
            errs.append('angle quote left')
    if '"' in ja:
        errs.append('double quote char')
    if re.search(r'[～~]', re.sub(r'~', '', ja)) :
        errs.append('wave dash')
    return errs

def main():
    os.makedirs(JA, exist_ok=True)
    tr = collections.defaultdict(dict)
    line_re = re.compile(r'^\[(\d+)\] ?(.*)$')
    for f in sorted(glob.glob(os.path.join(ROOT, 'text', 'chunks', 'c*.ja.txt'))):
        cur = None
        for line in open(f, encoding='utf-8-sig'):
            line = line.rstrip('\n').rstrip('\r')
            if line.startswith('### FILE:'):
                cur = line.split(':', 1)[1].strip()
                continue
            m = line_re.match(line)
            if m and cur:
                tr[cur][int(m.group(1))] = m.group(2)
    fx = os.path.join(ROOT, 'text', 'fixes.json')
    if os.path.exists(fx):
        for k, v in json.load(open(fx, encoding='utf-8')).items():
            m = re.match(r'^(.*)\[(\d+)\]$', k)
            tr[m.group(1)][int(m.group(2))] = v
    total = bad = missing = 0
    report = []
    for f in sorted(glob.glob(os.path.join(EN, '*.json'))):
        en = json.load(open(f, encoding='utf-8'))
        nm = en['name']
        out = []
        for i, s in enumerate(en['strings']):
            total += 1
            j = tr.get(nm, {}).get(i)
            if j is None:
                if s.strip() and nm in tr:
                    missing += 1
                    report.append('%s[%d] MISSING' % (nm, i))
                out.append(s)
                continue
            e = check(s, j, nm)
            if e:
                bad += 1
                report.append('%s[%d] %s | EN: %s | JA: %s' % (nm, i, '; '.join(e), s, j))
            out.append(j)
        if nm in tr:
            json.dump({'strings': out}, open(os.path.join(JA, nm + '.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
    open(os.path.join(ROOT, 'text', 'merge_report.txt'), 'w', encoding='utf-8').write('\n'.join(report))
    print('strings %d, missing %d, with issues %d' % (total, missing, bad))

if __name__ == '__main__':
    main()
