import struct, os
GAME=os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..')
def exe_names(exe):
    d=exe
    hdr=struct.unpack_from('<H',d,8)[0]*16
    DS=hdr+0x146b*16
    i=d.find(b'\xff\xffTEMP\%s')
    j=i; ptrs=[]
    while True:
        off,seg=struct.unpack_from('<HH',d,j-4)
        if seg!=0x146b: break
        ptrs.insert(0,off); j-=4
    names=[]
    for p in ptrs[1:]:
        e=d.index(b'\0',DS+p); names.append(d[DS+p:e].decode('latin1'))
    return names
def read_dat(b):
    n=struct.unpack_from('<I',b,0)[0]//4
    offs=struct.unpack_from('<%dI'%n,b,0)
    out=[]
    for i in range(n):
        s=offs[i]
        e=offs[i+1] if i+1<n and offs[i+1] else len(b)
        if s==0: out.append(b''); continue
        out.append(b[s:e])
    return out
