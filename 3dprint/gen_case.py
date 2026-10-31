#!/usr/bin/env python3
# 3D printable case for the CH32V003 Mini Game Console (PCB v1.1).
#
# All dimensions come from the v1.1 Gerber files (board outline, drill and pad
# positions) and the datasheets of the parts. Coordinates in mm, as in the Gerber
# files: x to the right, y downwards from the top edge of the board (OLED side),
# z = 0 on the top side of the PCB (components side with OLED and buttons).
#
# The parts are built from signed distance functions and meshed with marching
# cubes (scikit-image). Output: binary STL files in print orientation.
#
# Usage: python3 gen_case.py [outdir] [voxel size]

import sys, os, struct
import numpy as np
from skimage import measure

OUT = sys.argv[1] if len(sys.argv) > 1 else 'stl'
VOX = float(sys.argv[2]) if len(sys.argv) > 2 else 0.15

# -----------------------------------------------------------------------------------
# Board and parts (from the Gerber files, v1.1)
# -----------------------------------------------------------------------------------
PCB_W, PCB_H, PCB_T, PCB_R = 35.56, 50.8, 1.6, 2.54
HOLES   = [(2.54, 2.54), (33.02, 2.54), (2.54, 48.26)]      # M2, 2.03 mm
BTN     = {'UP': (12.70, 34.29), 'DOWN': (12.70, 46.99), 'LEFT': (6.35, 40.64),
           'RIGHT': (19.05, 40.64), 'ACT': (31.115, 43.815)}
BUZZER  = (31.115, 34.29)                                    # MLT-5030, 5x5x3 mm
SWITCH_Y = 12.70                                             # power switch (left edge)
PROG    = [(34.29, 10.16), (34.29, 12.70), (34.29, 15.24)]   # SWIO header (right edge)
OLED_X0, OLED_X1, OLED_Y0, OLED_Y1 = 4.13, 31.43, 0.4, 28.2  # 0.96" module (27.3 x 27.8)
BATT_C  = (17.78, 35.56)                                     # CR2032 holder (bottom side)

# -----------------------------------------------------------------------------------
# Case parameters
# -----------------------------------------------------------------------------------
GAP     = 0.5           # PCB edge to inner wall
WALL    = 1.6
PLUNGER = 4.3           # top of the tact switch plungers (6x6 mm switches, 4 mm high)
CAP_BOT = PLUNGER + 0.15  # underside of the button caps at rest
TOP_IN  = CAP_BOT + 1.25  # inner ceiling of the top shell above the PCB (room for the cap collars)
TOP_T   = 1.6           # thickness of the front plate
TOP_Z   = TOP_IN + TOP_T
OLED_TOP = 5.0          # top of the OLED glass above the PCB (measured: about 5 mm)
BOT_IN  = 6.2           # free space below the PCB (battery holder: 5.5 mm)
BOT_T   = 1.2
BOT_Z   = -PCB_T - BOT_IN - BOT_T
JOINT_Z = -0.6          # overlap of the two shells (rabbet joint) from -PCB_T up to here
EDGE    = 0.8           # chamfer of the outer edges
WIN     = (5.28, 30.28, 5.3, 19.3)   # OLED window (x0, x1, y0, y1)
CAP_CL  = 0.3           # clearance of the button caps in their holes
CAP_TOP = TOP_Z + 1.4   # buttons stand out 1.4 mm

CX, CY = PCB_W / 2, PCB_H / 2

# -----------------------------------------------------------------------------------
# SDF helpers (negative inside)
# -----------------------------------------------------------------------------------
def rrect(x, y, x0, x1, y0, y1, r):
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    hx, hy = (x1 - x0) / 2 - r, (y1 - y0) / 2 - r
    qx, qy = np.abs(x - cx) - hx, np.abs(y - cy) - hy
    return np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - r

def outline(x, y, off):
    """PCB outline grown by off"""
    return rrect(x, y, -off, PCB_W + off, -off, PCB_H + off, PCB_R + off)

def circle(x, y, c, r):
    return np.hypot(x - c[0], y - c[1]) - r

def slab(d2, z, z0, z1):
    return np.maximum(d2, np.maximum(z0 - z, z - z1))

def union(*a):
    r = a[0]
    for b in a[1:]: r = np.minimum(r, b)
    return r

def cut(a, b):
    return np.maximum(a, -b)

# -----------------------------------------------------------------------------------
# Top shell
# -----------------------------------------------------------------------------------
def cross2d(x, y, grow):
    a = 3.4 + grow
    (lx, ly), (rx, _), (ux, uy), (_, dy) = BTN['LEFT'], BTN['RIGHT'], BTN['UP'], BTN['DOWN']
    h = rrect(x, y, lx - 3.0 - grow, rx + 3.0 + grow, ly - a, ly + a, 1.2 + grow)
    v = rrect(x, y, ux - a, ux + a, uy - 3.0 - grow, dy + 3.0 + grow, 1.2 + grow)
    return np.minimum(h, v)

def fire2d(x, y, grow):
    return circle(x, y, BTN['ACT'], 3.4 + grow)

def cap_hole(d2fun, x, y, z):
    # vertical hole with CAP_CL clearance in the outer 1 mm of the plate, widening by 45
    # degrees towards the inside (room for the sloped collar of the cap)
    grow = CAP_CL + np.clip(CAP_BOT + 1.85 - z, 0, 0.8)
    return slab(d2fun(x, y, 0) - grow, z, TOP_IN - 1, TOP_Z + 1)

def top_shell(x, y, z):
    o_out = GAP + WALL
    # outer body with chamfered top edge
    body = slab(outline(x, y, o_out - np.clip(z - (TOP_Z - EDGE), 0, EDGE)), z, -PCB_T, TOP_Z)
    # cavity; below JOINT_Z only the outer half of the wall belongs to the top shell
    cav = union(slab(outline(x, y, GAP), z, -PCB_T - 1, TOP_IN),
                slab(outline(x, y, GAP + WALL / 2 + 0.08), z, -PCB_T - 1, JOINT_Z))
    s = cut(body, cav)
    # corner pillars around the screw holes (with a flat side next to the OLED module)
    p1 = np.maximum(rrect(x, y, -GAP - 1, 4.0, -GAP - 1, 4.4, 1.0), -z)
    p2 = np.maximum(rrect(x, y, 31.56, PCB_W + GAP + 1, -GAP - 1, 4.4, 1.0), -z)
    p3 = np.maximum(circle(x, y, HOLES[2], 2.3), -z)
    p3 = union(p3, np.maximum(rrect(x, y, -GAP - 1, 2.54, 46.0, PCB_H + GAP + 1, 0.5), -z))
    pil = slab(union(p1, p2, p3), z, 0, TOP_IN + 0.5)
    s = union(s, cut(pil, -outline(x, y, GAP + 0.01)))      # stay inside the walls
    for h in HOLES:                                          # M2 screws (self tapping)
        s = cut(s, slab(circle(x, y, h, 0.8), z, -5, TOP_IN - 0.8))
    # pocket above the OLED module, its glass is higher than the ceiling
    if OLED_TOP + 0.2 > TOP_IN:
        s = cut(s, slab(rrect(x, y, OLED_X0 - 0.3, OLED_X1 + 0.3, OLED_Y0 - 0.3, OLED_Y1 + 0.3, 0.8), z, TOP_IN - 0.5, OLED_TOP + 0.2))
    # OLED window with a 45 degree chamfer on the outside
    g = np.clip(z - (TOP_Z - 1.2), 0, 2)
    s = cut(s, slab(rrect(x, y, WIN[0] - g, WIN[1] + g, WIN[2] - g, WIN[3] + g, 1.0 + g), z, TOP_IN - 1, TOP_Z + 1))
    # button holes
    s = cut(s, cap_hole(cross2d, x, y, z))
    s = cut(s, cap_hole(fire2d, x, y, z))
    # buzzer sound holes
    bx, by = BUZZER
    for dx, dy in ((0, 0), (1.7, 0), (-1.7, 0), (0, 1.7), (0, -1.7)):
        s = cut(s, slab(circle(x, y, (bx + dx, by + dy), 0.6), z, TOP_IN - 1, TOP_Z + 1))
    # programming header: notch open from the top and the side (over the full height of
    # the wall, so nothing has to be bridged when printing)
    s = cut(s, slab(rrect(x, y, 32.3, PCB_W + 10, 7.9, 17.5, 0.8), z, 0.0, TOP_Z + 1))
    s = cut(s, slab(rrect(x, y, PCB_W + 0.1, PCB_W + 10, 7.9, 17.5, 0.8), z, -PCB_T - 1, TOP_Z + 1))
    # power switch: notch in the left wall
    s = cut(s, slab(rrect(x, y, -10, 0.3, SWITCH_Y - 4.0, SWITCH_Y + 4.0, 0.8), z, -PCB_T - 1, 3.5))
    return s

# -----------------------------------------------------------------------------------
# Bottom shell
# -----------------------------------------------------------------------------------
BATT_WIN = (5.6, 30.4, 22.8, 47.4)

def bottom_shell(x, y, z):
    o_out = GAP + WALL
    body = slab(outline(x, y, o_out - np.clip((BOT_Z + EDGE) - z, 0, EDGE)), z, BOT_Z, -PCB_T)
    # inner half of the wall goes up to JOINT_Z (rabbet joint with the top shell)
    lip = slab(cut(outline(x, y, GAP + WALL / 2 - 0.08), outline(x, y, GAP)), z, -PCB_T - 0.1, JOINT_Z)
    s = union(body, lip)
    s = cut(s, slab(outline(x, y, GAP), z, BOT_Z + BOT_T, 0))
    # posts under the PCB: screw posts and one support in the fourth corner
    posts = union(*[circle(x, y, h, 2.5) for h in HOLES], circle(x, y, (33.0, 48.3), 1.8))
    s = union(s, cut(slab(posts, z, BOT_Z + 0.5, -PCB_T), -outline(x, y, GAP + 0.01)))
    # battery window
    s = cut(s, slab(rrect(x, y, *BATT_WIN, 2.0), z, BOT_Z - 1, BOT_Z + BOT_T + 0.5))
    # screw holes with 90 degree countersinks (M2 flat head)
    for h in HOLES:
        s = cut(s, slab(circle(x, y, h, 1.15), z, BOT_Z - 1, 0))
        cs = circle(x, y, h, 2.1 - np.clip(z - BOT_Z, 0, 2))   # cone from 4.2 mm down to the hole
        s = cut(s, slab(cs, z, BOT_Z - 1, BOT_Z + 1.2))
    return s

# -----------------------------------------------------------------------------------
# Button caps
# -----------------------------------------------------------------------------------
def cap(d2fun, x, y, z, nub=None):
    # collar: flat 0.85 mm, then a 45 degree slope up to the body
    grow = np.clip(CAP_BOT + 1.85 - z, 0, 1.0)
    body = slab(d2fun(x, y, 0) - grow, z, CAP_BOT, CAP_TOP)
    # rounded top edge
    top = slab(d2fun(x, y, 0) + np.clip(z - (CAP_TOP - 0.5), 0, 0.5), z, CAP_BOT, CAP_TOP)
    s = np.maximum(body, np.where(z > CAP_TOP - 1, top, -1e3))
    if nub is not None:                                      # pivot of the D-pad
        s = union(s, slab(circle(x, y, nub, 1.4), z, 0.4, CAP_BOT + 0.5))
    return s

def dpad(x, y, z):
    return cap(cross2d, x, y, z, nub=(BTN['UP'][0], BTN['LEFT'][1]))

def fire(x, y, z):
    return cap(fire2d, x, y, z)

# -----------------------------------------------------------------------------------
# Components (for the collision check)
# -----------------------------------------------------------------------------------
def boxes():
    b = []
    for k, (x, y) in BTN.items():
        b.append(('switch ' + k, (x - 3, x + 3, y - 3, y + 3, 0, PLUNGER - 0.8)))
        b.append(('plunger ' + k, (x - 1.75, x + 1.75, y - 1.75, y + 1.75, 0, PLUNGER)))
        if k == 'ACT': b.append(('legs ' + k, (x - 2.6, x + 2.6, y - 4.75, y + 4.75, 0, 0.5)))
        else:          b.append(('legs ' + k, (x - 4.75, x + 4.75, y - 2.6, y + 2.6, 0, 0.5)))
    b.append(('OLED module', (OLED_X0, OLED_X1, OLED_Y0, OLED_Y1, 2.4, OLED_TOP)))
    b.append(('OLED header', (12.6, 23.0, 0.6, 3.2, 0, 2.5)))
    b.append(('buzzer', (BUZZER[0] - 2.5, BUZZER[0] + 2.5, BUZZER[1] - 2.5, BUZZER[1] + 2.5, 0, 3.0)))
    b.append(('power switch', (0, 4.2, SWITCH_Y - 3.7, SWITCH_Y + 3.7, 0, 1.6)))
    b.append(('switch knob', (-1.5, 0, SWITCH_Y - 2.3, SWITCH_Y + 2.3, 0.2, 1.4)))
    b.append(('SWIO header', (33.02, 35.56, 8.89, 16.51, 0, 2.5)))
    b.append(('SWIO pins + plug', (33.0, 35.6, 8.8, 16.6, -3.2, 16)))
    b.append(('battery holder', (17.78 - 15.65, 17.78 + 15.65, 27.56, 43.56, -PCB_T - 5.5, -PCB_T)))
    b.append(('IC', (14.2, 21.4, 10.1, 15.3, -PCB_T - 1.8, -PCB_T)))
    b.append(('pin tails', (13.0, 22.6, 1.0, 2.8, -PCB_T - 3.2, -PCB_T)))
    return b

def pcb(x, y, z):
    s = slab(outline(x, y, 0), z, -PCB_T, 0)
    for h in HOLES: s = cut(s, slab(circle(x, y, h, 1.0), z, -5, 5))
    return s

# -----------------------------------------------------------------------------------
# Meshing and STL output
# -----------------------------------------------------------------------------------
def grid(bounds, vox):
    (x0, x1), (y0, y1), (z0, z1) = bounds
    xs = np.arange(x0, x1 + vox, vox); ys = np.arange(y0, y1 + vox, vox); zs = np.arange(z0, z1 + vox, vox)
    return xs, ys, zs

def sample(f, bounds, vox):
    xs, ys, zs = grid(bounds, vox)
    vol = np.empty((len(xs), len(ys), len(zs)), np.float32)
    X, Y = np.meshgrid(xs, ys, indexing='ij')
    for k, z in enumerate(zs):                     # slice by slice to keep memory low
        vol[:, :, k] = f(X, Y, np.full_like(X, z))
    return xs, ys, zs, vol

def mesh(f, bounds, vox):
    xs, ys, zs, vol = sample(f, bounds, vox)
    # a tiny level offset avoids zero area triangles where the SDF is exactly 0 on a grid point
    v, faces, _, _ = measure.marching_cubes(vol, 1e-4, spacing=(vox, vox, vox))
    v += np.array([xs[0], ys[0], zs[0]])
    # make the normals point outwards (positive volume)
    a, b, c = v[faces[:, 0]], v[faces[:, 1]], v[faces[:, 2]]
    vol6 = np.einsum('ij,ij->i', a, np.cross(b, c)).sum()
    if vol6 < 0: faces = faces[:, ::-1]
    return v, faces, abs(vol6) / 6

def to_print(v, faces, flip):
    """board coordinates (y down) -> STL coordinates (y up), optionally turned upside down
    (rotated 180 degrees about the x axis); the faces are oriented outwards again"""
    w = v.copy()
    w[:, 1] = -w[:, 1]
    if flip: w[:, 1] = -w[:, 1]; w[:, 2] = -w[:, 2]
    a, b, c = w[faces[:, 0]], w[faces[:, 1]], w[faces[:, 2]]
    if np.einsum('ij,ij->i', a, np.cross(b, c)).sum() < 0: faces = faces[:, ::-1]
    return w, faces

def write_stl(fn, v, faces, name):
    tri = v[faces].astype(np.float32)
    n = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)
    data = np.zeros(len(faces), dtype=[('n', '<f4', 3), ('v', '<f4', (3, 3)), ('a', '<u2')])
    data['n'] = n; data['v'] = tri
    with open(fn, 'wb') as fh:
        fh.write(name.encode()[:80].ljust(80, b' '))
        fh.write(struct.pack('<I', len(faces)))
        fh.write(data.tobytes())

PARTS = [
    # name, sdf, bounds (x, y, z), print upside down
    ('GameConsole_case_top',    top_shell,    ((-2.6, 38.2), (-2.6, 53.4), (-1.8, TOP_Z + 0.2)), True),
    ('GameConsole_case_bottom', bottom_shell, ((-2.6, 38.2), (-2.6, 53.4), (BOT_Z - 0.2, 0.1)), False),
    ('GameConsole_button_dpad', dpad,         ((1.0, 24.5), (29.0, 51.8), (0.0, CAP_TOP + 0.2)), True),
    ('GameConsole_button_fire', fire,         ((26.4, 35.9), (38.9, 48.8), (CAP_BOT - 0.2, CAP_TOP + 0.2)), True),
]

if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    for name, f, b, flip in PARTS:
        v, faces, volume = mesh(f, b, VOX)
        w, faces = to_print(v, faces, flip)
        w -= w.min(axis=0) * np.array([0, 0, 1])     # lay it on the bed (z = 0)
        write_stl(os.path.join(OUT, name + '.stl'), w, faces, name)
        ext = w.max(axis=0) - w.min(axis=0)
        print('%-26s %7d triangles  %.1f x %.1f x %.1f mm  %.1f cm3' % (name, len(faces), *ext, volume / 1000))
