"""Desensamblador mínimo de la VU (instrucción inferior + bits de la superior). Uso: vu_dis.py code.bin desde hasta"""
import struct, sys

def sx(v, bits):
    return v - (1 << bits) if v & (1 << (bits - 1)) else v

DEST = lambda i: ''.join(c for b, c in zip((24, 23, 22, 21), 'xyzw') if i >> b & 1)

def lower(i, pc):
    if i in (0, 0x8000033C):
        return 'nop'
    op = i >> 25
    it, is_, id_ = (i >> 16) & 31, (i >> 11) & 31, (i >> 6) & 31
    imm11 = sx(i & 0x7FF, 11)
    imm15 = ((i >> 10) & 0x7800) | (i & 0x7FF)
    tgt = lambda: '0x%04x' % ((pc + 8 + imm11 * 8) & 0x3FFF)
    names = {0x00: 'lq', 0x01: 'sq', 0x04: 'ilw', 0x05: 'isw', 0x08: 'iaddiu', 0x09: 'isubiu',
             0x10: 'fceq', 0x11: 'fcset', 0x12: 'fcand', 0x13: 'fcor', 0x14: 'fseq', 0x15: 'fsset',
             0x16: 'fsand', 0x17: 'fsor', 0x18: 'fmeq', 0x1A: 'fmand', 0x1B: 'fmor', 0x1C: 'fcget',
             0x20: 'b', 0x21: 'bal', 0x24: 'jr', 0x25: 'jalr', 0x28: 'ibeq', 0x29: 'ibne',
             0x2C: 'ibltz', 0x2D: 'ibgtz', 0x2E: 'iblez', 0x2F: 'ibgez'}
    if op == 0x40:
        f = i & 0x3F
        if f < 0x3C:
            n = {0x30: 'iadd', 0x31: 'isub', 0x32: 'iaddi', 0x34: 'iand', 0x35: 'ior'}.get(f, 'special1_%02x' % f)
            if f == 0x32:
                return '%s vi%d, vi%d, %d' % (n, it, is_, sx(id_, 5))
            return '%s vi%d, vi%d, vi%d' % (n, id_, is_, it)
        f2 = ((i >> 6) & 0x1F) << 2 | (i & 3)
        n = {0x30: 'move', 0x31: 'mr32', 0x34: 'lqi', 0x35: 'sqi', 0x36: 'lqd', 0x37: 'sqd', 0x38: 'div',
             0x39: 'sqrt', 0x3A: 'rsqrt', 0x3B: 'waitq', 0x3C: 'mtir', 0x3D: 'mfir', 0x3E: 'ilwr', 0x3F: 'iswr',
             0x40: 'rnext', 0x41: 'rget', 0x42: 'rinit', 0x43: 'rxor', 0x64: 'mfp', 0x68: 'xtop', 0x69: 'xitop',
             0x6C: 'xgkick', 0x70: 'esadd', 0x7B: 'waitp'}.get(f2, 'special2_%02x' % f2)
        if n == 'mtir':
            return 'mtir vi%d, vf%d%s' % (it, is_, 'xyzw'[(i >> 21) & 3])
        if n in ('xtop', 'xitop'):
            return '%s vi%d' % (n, it)
        if n == 'xgkick':
            return 'xgkick vi%d' % is_
        if n in ('lqi', 'lqd'):
            return '%s.%s vf%d, (vi%d)' % (n, DEST(i), it, is_)
        if n in ('sqi', 'sqd'):
            return '%s.%s vf%d, (vi%d)' % (n, DEST(i), is_, it)
        if n in ('ilwr', 'iswr'):
            return '%s.%s vi%d, (vi%d)' % (n, DEST(i), it, is_)
        if n == 'mfir':
            return 'mfir.%s vf%d, vi%d' % (DEST(i), it, is_)
        return '%s it=%d is=%d' % (n, it, is_)
    n = names.get(op, 'op_%02x' % op)
    if op in (0x08, 0x09):
        return '%s vi%d, vi%d, 0x%x' % (n, it, is_, imm15)
    if op in (0x04, 0x05):
        return '%s.%s vi%d, %d(vi%d)' % (n, DEST(i), it, imm11, is_)
    if op == 0x00:
        return 'lq.%s vf%d, %d(vi%d)' % (DEST(i), it, imm11, is_)
    if op == 0x01:
        return 'sq.%s vf%d, %d(vi%d)' % (DEST(i), is_, imm11, it)
    if op == 0x20:
        return 'b ' + tgt()
    if op == 0x21:
        return 'bal vi%d, %s' % (it, tgt())
    if op in (0x24, 0x25):
        return '%s vi%d' % (n, is_) if op == 0x24 else 'jalr vi%d, vi%d' % (it, is_)
    if op in (0x28, 0x29):
        return '%s vi%d, vi%d, %s' % (n, it, is_, tgt())
    if 0x2C <= op <= 0x2F:
        return '%s vi%d, %s' % (n, is_, tgt())
    if op == 0x1C:
        return 'fcget vi%d' % it
    return '%s raw=%08x' % (n, i)

BC = 'xyzw'
UP1 = {0x1c: 'mulq', 0x1d: 'maxi', 0x1e: 'muli', 0x1f: 'minii', 0x20: 'addq', 0x21: 'maddq', 0x22: 'addi',
       0x23: 'maddi', 0x24: 'subq', 0x25: 'msubq', 0x26: 'subi', 0x27: 'msubi', 0x28: 'add', 0x29: 'madd',
       0x2a: 'mul', 0x2b: 'max', 0x2c: 'sub', 0x2d: 'msub', 0x2e: 'opmsub', 0x2f: 'mini'}
UP2 = {0x10: 'itof0', 0x11: 'itof4', 0x12: 'itof12', 0x13: 'itof15', 0x14: 'ftoi0', 0x15: 'ftoi4', 0x16: 'ftoi12',
       0x17: 'ftoi15', 0x1c: 'mulaq', 0x1d: 'abs', 0x1e: 'mulai', 0x1f: 'clip', 0x20: 'addaq', 0x21: 'maddaq',
       0x22: 'addai', 0x23: 'maddai', 0x24: 'subaq', 0x25: 'msubaq', 0x26: 'subai', 0x27: 'msubai', 0x28: 'adda',
       0x29: 'madda', 0x2a: 'mula', 0x2c: 'suba', 0x2d: 'msuba', 0x2e: 'opmula', 0x2f: 'nop'}

def upper(u):
    ft, fs, fd, f = (u >> 16) & 31, (u >> 11) & 31, (u >> 6) & 31, u & 0x3F
    d = DEST(u)
    if f < 0x3C:
        if f < 0x1C:
            n = ['add', 'sub', 'madd', 'msub', 'max', 'mini', 'mul'][f >> 2] + BC[f & 3]
            return '%s.%s vf%d, vf%d, vf%d%s' % (n, d, fd, fs, ft, BC[f & 3])
        return '%s.%s vf%d, vf%d, vf%d' % (UP1.get(f, 'up_%02x' % f), d, fd, fs, ft)
    f2 = ((u >> 6) & 0x1F) << 2 | (u & 3)
    if f2 == 0x2F:
        return 'nop'
    if f2 < 0x10:
        n = ['adda', 'suba', 'madda', 'msuba'][f2 >> 2] + BC[f2 & 3]
        return '%s.%s ACC, vf%d, vf%d%s' % (n, d, fs, ft, BC[f2 & 3])
    if 0x18 <= f2 <= 0x1B:
        return 'mula%s.%s ACC, vf%d, vf%d%s' % (BC[f2 & 3], d, fs, ft, BC[f2 & 3])
    n = UP2.get(f2, 'up2_%02x' % f2)
    if n.startswith(('itof', 'ftoi', 'abs')):
        return '%s.%s vf%d, vf%d' % (n, d, ft, fs)
    return '%s.%s vf%d, vf%d' % (n, d, fs, ft)

if __name__ == '__main__':
    code = open(sys.argv[1], 'rb').read()
    a, b = int(sys.argv[2], 0), int(sys.argv[3], 0)
    for pc in range(a, b, 8):
        lo, up = struct.unpack_from('<II', code, pc)
        flags = ''.join(c for bit, c in ((31, 'I'), (30, 'E'), (29, 'M'), (28, 'D'), (27, 'T')) if up >> bit & 1)
        low = '(imm %08x = %g)' % (lo, struct.unpack('<f', struct.pack('<I', lo))[0]) if up >> 31 & 1 else lower(lo, pc)
        print('%04x  %-5s %-34s | %s' % (pc, flags, upper(up), low))
