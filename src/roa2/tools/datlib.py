import struct
def read_star_dat(b):
    """STAR.DAT: u32 n, n * {char name[14], u32 off, u16 flag}, data (offsets relative to data start)"""
    n = struct.unpack_from('<I', b, 0)[0]
    base = 2 + n * 20
    ents = []
    for i in range(n):
        o = 4 + i * 20
        nm = b[o:o+14].split(b'\0')[0].decode('latin1')
        off, fl = struct.unpack_from('<IH', b, o + 14)
        ents.append([nm, off, fl])
    out = []
    for i, (nm, off, fl) in enumerate(ents):
        nxt = len(b) - base
        for j in range(i + 1, n):
            if ents[j][1] >= off:
                nxt = ents[j][1]; break
        out.append((nm, off, fl, b[base + off: base + nxt]))
    return out
