# Split English strings into translation chunks (line based text files).
import json, glob, os
ROOT=os.path.join(os.path.dirname(__file__),'..')
LIMIT=16000
files=sorted(glob.glob(os.path.join(ROOT,'text','en','*.json')), key=lambda f: json.load(open(f,encoding='utf-8'))['index'])
chunks=[]; cur=[]; size=0
for f in files:
    nm=os.path.basename(f)[:-5]
    ss=json.load(open(f,encoding='utf-8'))['strings']
    for i,s in enumerate(ss):
        l=len(s)+8
        if size+l>LIMIT and cur and (size>LIMIT*0.6 or i==0):
            chunks.append(cur); cur=[]; size=0
        cur.append((nm,i,s)); size+=l
if cur: chunks.append(cur)
od=os.path.join(ROOT,'text','chunks'); os.makedirs(od,exist_ok=True)
for k,c in enumerate(chunks):
    with open(os.path.join(od,'c%02d.en.txt'%k),'w',encoding='utf-8') as fp:
        last=None
        for nm,i,s in c:
            if nm!=last: fp.write('### FILE: %s\n'%nm); last=nm
            fp.write('[%d] %s\n'%(i,s))
print(len(chunks),'chunks', [sum(len(s) for _,_,s in c) for c in chunks])
