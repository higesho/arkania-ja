import json, glob, os, re
ROOT=os.path.join(os.path.dirname(__file__),'..')
G=json.load(open(os.path.join(ROOT,'text','glossary.json'),encoding='utf-8'))
for f in sorted(glob.glob(os.path.join(ROOT,'text','chunks','c*.en.txt'))):
    t=open(f,encoding='utf-8').read().upper()
    files=set(re.findall(r'### FILE: (\S+)',t))
    sub=[]
    for e in G:
        en=e['en'].upper().strip()
        if not en: continue
        pat=r'(?<![A-Z])'+re.escape(en)+r'(?![A-Z])'
        if re.search(pat,t) or any(fl.startswith(en) for fl in files):
            sub.append({k:e[k] for k in ('en','ja','note') if e.get(k)})
    out=f.replace('.en.txt','.gloss.json')
    json.dump(sub,open(out,'w',encoding='utf-8'),ensure_ascii=False,indent=0)
    print(os.path.basename(out),len(sub),os.path.getsize(out))
