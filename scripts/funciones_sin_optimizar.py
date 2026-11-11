"""Busca funciones del codigo traducido compiladas sin optimizar (C4883): prologo de /Od (los tres argumentos
guardados en su sitio de la pila: [rsp+18h],r8 ; [rsp+10h],rdx ; [rsp+8],rcx)."""
import glob
import re
import subprocess
import sys

dumpbin = sys.argv[1]
objs = sorted(glob.glob(sys.argv[2]))
res = []
for o in objs:
    p = subprocess.run([dumpbin, '/nologo', '/disasm', o], capture_output=True, text=True, errors='replace')
    lines = p.stdout.split('\n')
    for i, l in enumerate(lines):
        if l.startswith('?sub_') or l.startswith('sub_'):
            name = l.split('@')[0].lstrip('?')
            body = ' '.join(lines[i + 1:i + 8])
            if 'qword ptr [rsp+18h],r8' in body and 'qword ptr [rsp+10h],rdx' in body and 'qword ptr [rsp+8],rcx' in body:
                m = re.search(r'mov\s+eax,([0-9A-F]+)h\s', body)
                res.append((name, o.split('/')[-1].split('\\')[-1], m.group(1) if m else ''))
for r in res:
    print('%s %s pila=%s' % r)
print('total', len(res), file=sys.stderr)
