#!/usr/bin/env python3
# Checks of the case: collisions with the components and between the parts,
# mesh sanity (closed, manifold) and preview pictures.
import sys, glob, struct
import numpy as np
sys.argv = sys.argv[:1]
import gen_case as G
from PIL import Image

def pts(box, step=0.1):
    x0, x1, y0, y1, z0, z1 = box
    xs = np.arange(x0 + 0.02, x1, step); ys = np.arange(y0 + 0.02, y1, step); zs = np.arange(z0 + 0.02, z1, step)
    X, Y, Z = np.meshgrid(xs, ys, zs, indexing='ij')
    return X.ravel(), Y.ravel(), Z.ravel()

parts = {'top': G.top_shell, 'bottom': G.bottom_shell, 'dpad': G.dpad, 'fire': G.fire}
print('collisions with the components (points inside a part, 0.1 mm grid):')
bad = 0
for name, box in G.boxes():
    X, Y, Z = pts(box)
    for pn, f in parts.items():
        n = int((f(X, Y, Z) < -0.01).sum())
        if n:
            bad += 1; print('  %-18s x %-6s: %d points' % (name, pn, n))
print('  none' if not bad else '')

print('part against part / PCB:')
xs = np.arange(-2.7, 38.3, 0.1); ys = np.arange(-2.7, 53.5, 0.1); zs = np.arange(G.BOT_Z - 0.1, G.CAP_TOP + 0.1, 0.1)
X, Y = np.meshgrid(xs, ys, indexing='ij')
names = list(parts) + ['pcb']
cnt = {}; clr = {'dpad': 9, 'fire': 9}
for z in zs:
    Z = np.full_like(X, z)
    fs = {k: f(X, Y, Z) for k, f in parts.items()}
    fs['pcb'] = G.pcb(X, Y, Z)
    for i in range(len(names)):
        for j in range(i + 1, len(names)):
            k = (names[i], names[j])
            cnt[k] = cnt.get(k, 0) + int(((fs[names[i]] < -0.01) & (fs[names[j]] < -0.01)).sum())
    for c in clr:
        m = fs[c] < 0
        if m.any(): clr[c] = min(clr[c], float(fs['top'][m].min()))
for k, n in cnt.items(): print('  %-6s x %-6s: %d points' % (k[0], k[1], n))
for c in clr: print('  clearance %s to top shell: %.2f mm' % (c, clr[c]))

# mesh check
print('meshes:')
for fn in sorted(glob.glob('stl/*.stl')):
    d = open(fn, 'rb').read()
    n = struct.unpack('<I', d[80:84])[0]
    a = np.frombuffer(d[84:], dtype=[('n', '<f4', 3), ('v', '<f4', (3, 3)), ('a', '<u2')], count=n)
    v = a['v'].reshape(-1, 3)
    u, inv = np.unique(np.round(v, 4), axis=0, return_inverse=True)
    t = inv.reshape(-1, 3)
    e = np.sort(np.concatenate([t[:, [0, 1]], t[:, [1, 2]], t[:, [2, 0]]]), axis=1)
    _, cnt = np.unique(e, axis=0, return_counts=True)
    # directed edges: each must appear once (consistent orientation)
    de = np.concatenate([t[:, [0, 1]], t[:, [1, 2]], t[:, [2, 0]]])
    _, dc = np.unique(de, axis=0, return_counts=True)
    tri = a['v']
    vol = np.einsum('ij,ij->i', tri[:, 0], np.cross(tri[:, 1], tri[:, 2])).sum() / 6
    print('  %-40s %7d tri  edges !=2: %d  dup directed: %d  volume %.2f cm3  z %.2f..%.2f' %
          (fn, n, int((cnt != 2).sum()), int((dc != 1).sum()), vol / 1000, v[:, 2].min(), v[:, 2].max()))
