#!/usr/bin/env python3
# Preview pictures of the case by sphere tracing the signed distance functions.
import sys
import numpy as np
from PIL import Image
sys.argv = sys.argv[:1]
import gen_case as G

def box(x, y, z, b):
    x0, x1, y0, y1, z0, z1 = b
    q = np.stack([np.abs(x - (x0 + x1) / 2) - (x1 - x0) / 2, np.abs(y - (y0 + y1) / 2) - (y1 - y0) / 2,
                  np.abs(z - (z0 + z1) / 2) - (z1 - z0) / 2])
    return np.linalg.norm(np.maximum(q, 0), axis=0) + np.minimum(q.max(axis=0), 0)

COMP = [b for n, b in G.boxes() if 'plug' not in n and 'pins' not in n]

def scene(parts):
    def f(x, y, z):
        d = np.full(x.shape, 1e3); col = np.zeros(x.shape, np.int8)
        for i, (fn, dz, c) in enumerate(parts):
            di = fn(x, y, z - dz)
            m = di < d; d = np.where(m, di, d); col = np.where(m, c, col)
        return d, col
    return f

def comps(x, y, z):
    d = G.pcb(x, y, z)
    for b in COMP: d = np.minimum(d, box(x, y, z, b))
    return d

COLORS = {0: (235, 235, 235), 1: (60, 130, 220), 2: (40, 40, 40), 3: (230, 80, 60), 4: (70, 160, 90)}

def render(f, eye_dir, center, size, res=640, up=(0, 0, 1)):
    d = np.array(eye_dir, float); d /= np.linalg.norm(d)                 # viewing direction (towards scene)
    upv = np.array(up, float); r = np.cross(upv, d); r /= np.linalg.norm(r); u = np.cross(d, r)
    s = np.linspace(-size / 2, size / 2, res)
    A, B = np.meshgrid(s, -s)
    o = np.array(center)[:, None, None] - d[:, None, None] * 80 + r[:, None, None] * A + u[:, None, None] * B
    t = np.zeros(A.shape); hit = np.zeros(A.shape, bool); col = np.zeros(A.shape, np.int8)
    for _ in range(260):
        p = o + d[:, None, None] * t
        dist, c = f(p[0], p[1], p[2])
        nh = (dist < 0.02) & ~hit
        col = np.where(nh, c, col); hit |= nh
        t = np.where(hit, t, t + np.maximum(dist * 0.8, 0.02))
        if (t > 200).all(): break
    p = o + d[:, None, None] * t
    e = 0.05
    n = np.stack([f(p[0] + e, p[1], p[2])[0] - f(p[0] - e, p[1], p[2])[0],
                  f(p[0], p[1] + e, p[2])[0] - f(p[0], p[1] - e, p[2])[0],
                  f(p[0], p[1], p[2] + e)[0] - f(p[0], p[1], p[2] - e)[0]])
    n /= np.maximum(np.linalg.norm(n, axis=0), 1e-9)
    light = -d + np.array([0.3, -0.5, 0.6]); light /= np.linalg.norm(light)
    sh = np.clip((n * light[:, None, None]).sum(axis=0), 0, 1) * 0.75 + 0.25
    img = np.full(A.shape + (3,), 255, np.uint8)
    for k, c in COLORS.items():
        m = hit & (col == k)
        img[m] = (np.array(c)[None, :] * sh[m][:, None]).clip(0, 255)
    return Image.fromarray(img)

if __name__ == '__main__':
    # board coordinates have y downwards: look from "below" in y to see it right way round
    exploded = scene([(G.top_shell, 16, 1), (G.dpad, 26, 2), (G.fire, 26, 3), (comps, 0, 4), (G.bottom_shell, -14, 1)])
    assembled = scene([(G.top_shell, 0, 1), (G.dpad, 0, 2), (G.fire, 0, 3), (comps, 0, 4), (G.bottom_shell, 0, 1)])
    c = (G.CX, G.CY, 0)
    ims = [render(exploded, (-0.55, -0.65, -0.55), (G.CX, G.CY, 4), 75, 560, up=(0, 0, 1)),
           render(assembled, (0.0001, 0.0001, -1), c, 62, 560, up=(0, -1, 0)),
           render(assembled, (0.0001, 0.0001, 1), c, 62, 560, up=(0, -1, 0)),
           render(assembled, (0.75, -0.45, -0.5), (G.CX, G.CY, 0), 62, 560, up=(0, 0, 1)),
           render(assembled, (-0.75, -0.45, -0.5), (G.CX, G.CY, 0), 62, 560, up=(0, 0, 1))]
    W = sum(i.width for i in ims); sheet = Image.new('RGB', (W, ims[0].height), 'white'); x = 0
    for i in ims: sheet.paste(i, (x, 0)); x += i.width
    sheet.save('preview.png')
    for k, i in enumerate(ims): i.save('view%d.png' % k)
