# Extract translatable strings from BLADE.DAT entries into JSON units.
import sys, os, json, struct, glob
sys.path.insert(0, os.path.dirname(__file__))
from datlib import exe_names, read_dat
ROOT=os.path.join(os.path.dirname(__file__),'..')
exe=open(os.path.join(ROOT,'orig','BLADEM.EXE'),'rb').read()
dat=open(os.path.join(ROOT,'orig','BLADE.DAT'),'rb').read()
names=exe_names(exe); files=read_dat(dat)
def dec(b):
    # keep control bytes visible: F0-F3 -> {C0}..{C3}; other high bytes -> {xHH}
    out=[]
    for x in b:
        if 0xf0<=x<=0xf3: out.append('{C%d}'%(x-0xf0))
        elif x>=0x80 or x<0x20: out.append('{x%02X}'%x)
        else: out.append(chr(x))
    return ''.join(out)
units={}
for i,(nm,b) in enumerate(zip(names,files)):
    ext=nm.split('.')[-1] if '.' in nm else nm
    head=b''
    if ext in ('LTX','DTX') or nm in ('ITEMNAME','MONNAMES'):
        body=b
    elif ext=='TLK':
        off=struct.unpack_from('<I',b,0)[0]; head=b[:6+off]; body=b[6+off:]
    else: continue
    parts=body.split(b'\0')
    trailing = parts[-1]==b''
    if trailing: parts=parts[:-1]
    units[nm]={'index':i,'head_len':len(head),'trailing_nul':trailing,
               'strings':[dec(p) for p in parts]}
# DSAGEN.DAT (character generator, named directory)
g=open(os.path.join(ROOT,'orig','DSAGEN.DAT'),'rb').read()
dirsize=struct.unpack_from('<I',g,12)[0]
ents=[]
for k in range(0,dirsize,16):
    nm=g[k:k+12].split(bytes(1))[0].decode('latin1')
    if not nm: break
    ents.append((nm,struct.unpack_from('<I',g,k+12)[0]))
for k,(nm,off) in enumerate(ents):
    if nm=='E_GENTXT':
        end=ents[k+1][1] if k+1<len(ents) else len(g)
        b=g[off:end]; parts=b.split(bytes(1)); trailing=parts[-1]==b''
        if trailing: parts=parts[:-1]
        units['E_GENTXT']={'archive':'DSAGEN.DAT','index':-1,'head_len':0,'trailing_nul':trailing,'strings':[dec(p) for p in parts]}
os.makedirs(os.path.join(ROOT,'text','en'),exist_ok=True)
tot=0
for nm,u in units.items():
    json.dump(u,open(os.path.join(ROOT,'text','en',nm+'.json'),'w',encoding='utf-8'),ensure_ascii=False,indent=0)
    tot+=len(u['strings'])
print(len(units),'files',tot,'strings')
