# Merge translated chunk files (text/chunks/cNN.ja.txt) into text/ja/<FILE>.json and validate.
import json, glob, os, re, sys, collections
ROOT = os.path.join(os.path.dirname(__file__), '..')
EN = os.path.join(ROOT, 'text', 'en')
JA = os.path.join(ROOT, 'text', 'ja')
os.makedirs(JA, exist_ok=True)

FMT = re.compile(r'%%|%[-+ #0-9.]*l?[a-z]')
CTL = re.compile(r'\{C[0-3]\}|\{x(?!0D)[0-9A-Fa-f]{2}\}|~')

def fmts(s):
    return ['%s' if f == '%.0s' else f for f in FMT.findall(s)]

def check(en, ja, nm):
    errs = []
    if fmts(en) != fmts(ja):
        errs.append('format %s != %s' % (FMT.findall(en), FMT.findall(ja)))
    if CTL.findall(en) != CTL.findall(ja):
        errs.append('controls %s != %s' % (CTL.findall(en), CTL.findall(ja)))
    if en.strip() and not ja.strip():
        errs.append('empty')
    if nm in ('ITEMNAME', 'MONNAMES') and en.count('.') == 2 and ja.count('.') != 2:
        errs.append('name needs exactly two dots')
    if re.search(r'[a-z]', FMT.sub('', re.sub(r'\{x[0-9A-Fa-f]{2}\}', '', ja))) and not re.search(r'[a-z]', FMT.sub('', en)):
        errs.append('lowercase latin')
    if ja.count('<') + ja.count('>') > en.count('<') + en.count('>') or ('<' in ja and '<' in en and en.count('<') > 0 and ja.count('<') == en.count('<') and not re.fullmatch(r"[%d'<>]*", en)):
        errs.append('angle quote left')
    return errs

def main():
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
        nm = os.path.basename(f)[:-5]
        en = json.load(open(f, encoding='utf-8'))
        out = []
        for i, s in enumerate(en['strings']):
            total += 1
            j = tr.get(nm, {}).get(i)
            if j is None:
                missing += 1
                report.append('%s[%d] MISSING' % (nm, i))
                out.append(s)
                continue
            e = check(s, j, nm)
            if e:
                bad += 1
                report.append('%s[%d] %s | EN: %s | JA: %s' % (nm, i, '; '.join(e), s, j))
            out.append(j)
        json.dump({'strings': out}, open(os.path.join(JA, nm + '.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
    open(os.path.join(ROOT, 'text', 'merge_report.txt'), 'w', encoding='utf-8').write('\n'.join(report))
    print('strings %d, missing %d, with issues %d' % (total, missing, bad))

if __name__ == '__main__':
    main()
