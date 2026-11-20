# Snapshot the game's data segment from a running DOSBox (this project's build only).
import ctypes, ctypes.wintypes as wt, sys, struct
PID = int(sys.argv[1])
k = ctypes.WinDLL('kernel32', use_last_error=True)
h = k.OpenProcess(0x0010 | 0x0400, False, PID)
def rd(addr, n):
    buf = ctypes.create_string_buffer(n); got = ctypes.c_size_t()
    if not k.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, n, ctypes.byref(got)):
        raise OSError(ctypes.get_last_error())
    return buf.raw
membase = struct.unpack('<I', rd(0x74b6b0, 4))[0]          # tlb.read[0] == MemBase
dsphys = struct.unpack('<I', rd(0x74b10c, 4))[0]
csphys = struct.unpack('<I', rd(0x74b104, 4))[0]
data = rd(membase + dsphys, 0x10000)
out = sys.argv[2]
open(out, 'wb').write(data)
print('membase %x ds %x cs %x -> %s' % (membase, dsphys, csphys, out))
