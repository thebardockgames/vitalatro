#!/usr/bin/env python3
"""compare_vpk.py <web.vpk> <native.vpk> <reference.vpk | reference assets dir + eboot dir>

1. The web VPK must equal the native CLI's VPK byte for byte (same code, same base).
2. Against the locally built VPK (or build/assets_vita + build/vita), per file:
   identical / expected differences (our sce_sys images; data/*.lua whose key order is
   random in the current pipeline: compared as Lua values with tools/asset_builder/test/
   same_data.lua) / unexpected.
"""
import os
import subprocess
import sys
import tempfile
import zipfile

web, native = sys.argv[1], sys.argv[2]
ref = sys.argv[3]
HERE = os.path.dirname(os.path.abspath(__file__))

wb = open(web, 'rb').read()
nb = open(native, 'rb').read()
print('web VPK == native VPK (whole file):', wb == nb, len(wb), len(nb))

zw = zipfile.ZipFile(web)
assert zw.testzip() is None
files = {n: zw.read(n) for n in zw.namelist()}

if ref.endswith('.vpk'):
    zr = zipfile.ZipFile(ref)
    reff = {n: zr.read(n) for n in zr.namelist()}
else:  # assets dir; eboot.bin and param.sfo from VITA_BUILD
    reff = {}
    for root, _, names in os.walk(ref):
        for n in names:
            p = os.path.join(root, n)
            reff[os.path.relpath(p, ref).replace(os.sep, '/')] = open(p, 'rb').read()
    vb = os.environ.get('VITA_BUILD', '/mnt/d/Proyectos/Vitalatro/build/vita')
    reff['eboot.bin'] = open(os.path.join(vb, 'eboot.bin'), 'rb').read()
    reff['sce_sys/param.sfo'] = open(os.path.join(vb, 'vitalatro.vpk_param.sfo'), 'rb').read()
    reff['sce_sys/livearea/contents/template.xml'] = open(os.path.join(HERE, '../../../sce_sys/livearea/contents/template.xml'), 'rb').read()

same, expected, unexpected = [], [], []
ours = {'sce_sys/icon0.png', 'sce_sys/livearea/contents/bg.png', 'sce_sys/livearea/contents/startup.png'}
data_diff = []
for n in sorted(set(files) | set(reff)):
    a, b = files.get(n), reff.get(n)
    if a == b:
        same.append(n)
    elif n in ours and a is not None:
        expected.append(n + ' (our own image)')
    elif n.startswith('data/') and a is not None and b is not None:
        data_diff.append(n)
    else:
        unexpected.append(n + (' (only in web)' if b is None else ' (only in reference)' if a is None else ' (differs)'))

if data_diff:
    with tempfile.TemporaryDirectory() as t:
        for side, src in (('w', files), ('r', reff)):
            os.makedirs(os.path.join(t, side))
            for n in data_diff:
                open(os.path.join(t, side, os.path.basename(n)), 'wb').write(src[n])
        r = subprocess.run(['luajit', os.path.join(HERE, 'same_data.lua'), os.path.join(t, 'w'), os.path.join(t, 'r')] +
                           [os.path.basename(n) for n in data_diff], capture_output=True, text=True)
        for line in r.stdout.splitlines():
            name = 'data/' + line.split()[1].split('.lua')[0] + '.lua'
            if line.startswith('same'):
                expected.append(name + ' (key order only)')
            else:
                expected.append(name + ' (' + line[5:] + ')' if 'pools_default' in line else name)
                if 'pools_default' not in line:
                    unexpected.append(line)

print(f'identical: {len(same)} files')
print(f'expected differences: {len(expected)}')
for e in expected:
    print('   ', e)
print(f'unexpected: {len(unexpected)}')
for u in unexpected:
    print('   ', u)
sys.exit(0 if wb == nb else 1)
