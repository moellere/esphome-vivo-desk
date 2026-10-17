"""Desk-bike controller enclosure (parametric; trimesh + manifold -> STL).

Built around two uploaded parts:
  * the Perma-Proto snap-in tray (166 x 60 x 8 mm) that holds the board -> drops into the base
  * the OLED/EC11 front panel's proven cutouts (window 34.5x19, bushing 7.5, keys 12x7.5)

Outputs:
  base.stl           box body: cavity for the tray, corner lid posts, keystone + USB-C + DevKitC-USB ports, vents
  lid.stl            flat lid carrying the module (window, bushing hole, key openings, 4 M3 posts)
  keycap.stl         key cap for the two tact switches (print 2)
  tray_notched.stl   the uploaded tray with 4 corner notches for the lid posts
  module_bezel.stl   Option A: stand-alone bezel plate for a cutout in a bought box lid
All mm, Z up, origin at the centre of the tray / board.
"""
import numpy as np, trimesh, sys, json, os
from trimesh.creation import box, cylinder

OUT = sys.argv[1] if len(sys.argv) > 1 else "."
TRAY = sys.argv[2] if len(sys.argv) > 2 else None

P = dict(
    tray_len=166.0, tray_wid=60.0, tray_h=8.0, tray_clear=0.5,
    board_top_above_tray=7.4,          # Perma-Proto top surface above the tray's underside
    wall=2.5, floor=2.5, inner_h=42.0, # floor top -> lid underside
    lid_thk=2.5, post_d=7.0, post_screw_d=2.7, lid_hole_d=3.4, head_d=6.5, head_t=1.5,
    # keystone jack cutout in the +X end wall (standard 14.5 x 16.0 opening, 1.6 mm panel)
    keystone_w=14.5, keystone_h=16.0, keystone_panel=1.6, keystone_relief=4.0, keystone_z=14.0, keystone_y=12.0,
    # USB-C breakout slot on the same end wall (centre y, bottom z above board top)
    usbc_w=10.0, usbc_h=4.5, usbc_y=-14.0, usbc_z_above_board=1.0,
    # DevKitC micro-USB slot on the -Y long wall (x along the board; bottom z above board top; assumes female headers)
    devkit_usb_x=-55.0, devkit_usb_w=13.0, devkit_usb_h=7.0, devkit_usb_z_above_board=9.0,
    vent_n=6, vent_w=2.0, vent_l=16.0,
    # module cutouts, in module coords: origin = centre of the 4-hole pattern, +x toward the encoder, +y up (toward BACK)
    mod_holes=[(-29.5, 14.6), (29.5, 14.6), (-29.5, -14.6), (29.5, -14.6)],
    mod_window=(-10.75, 2.0, 34.5, 19.0), window_bevel=1.0,
    mod_shaft=(20.9, -0.15), shaft_hole_d=7.6,
    mod_keys=[(20.95, 11.9), (20.95, -11.9)], key_w=12.0, key_h=7.5,
    mod_post_h=7.0, mod_post_d=6.5, mod_post_screw_d=2.7,
    mod_offset_x=38.0,                 # module centre along the lid; + = keystone end
    # keycap: stem fits the key opening with 0.25 mm/side clearance, flange under the lid, nub onto the switch
    cap_clear=0.25, cap_protrude=1.5, cap_flange=1.5, cap_flange_t=1.2, cap_nub_d=3.0, cap_nub_h=2.0,
    bezel_margin=4.0, bezel_thk=2.5, bezel_hole_d=3.4,
)
if os.path.exists(os.path.join(OUT, "params.json")): P.update(json.load(open(os.path.join(OUT, "params.json"))))

def cyl(d, h, x=0, y=0, z0=0, sections=48):
    c = cylinder(radius=d/2, height=h, sections=sections); c.apply_translation((x, y, z0 + h/2)); return c
def bx(sx, sy, sz, x=0, y=0, z0=0):
    b = box((sx, sy, sz)); b.apply_translation((x, y, z0 + sz/2)); return b
def frustum(w0, h0, w1, h1, z0, z1, x=0, y=0):
    """rectangular frustum (for the bevelled window): w0 x h0 at z0 -> w1 x h1 at z1"""
    v = np.array([[-w0/2,-h0/2,z0],[w0/2,-h0/2,z0],[w0/2,h0/2,z0],[-w0/2,h0/2,z0],
                  [-w1/2,-h1/2,z1],[w1/2,-h1/2,z1],[w1/2,h1/2,z1],[-w1/2,h1/2,z1]]) + [x,y,0]
    f = [[0,2,1],[0,3,2],[4,5,6],[4,6,7],[0,1,5],[0,5,4],[1,2,6],[1,6,5],[2,3,7],[2,7,6],[3,0,4],[3,4,7]]
    m = trimesh.Trimesh(v, f); m.fix_normals(); return m
def union(ms): return trimesh.boolean.union(ms, engine="manifold")
def diff(a, ms): return trimesh.boolean.difference([a] + ms, engine="manifold")

in_x = P["tray_len"] + 2*P["tray_clear"]; in_y = P["tray_wid"] + 2*P["tray_clear"]
out_x = in_x + 2*P["wall"]; out_y = in_y + 2*P["wall"]
z_floor = P["floor"]; out_z = z_floor + P["inner_h"]
z_board_top = z_floor + P["board_top_above_tray"]
posts_xy = [(sx*(in_x/2 - P["post_d"]/2), sy*(in_y/2 - P["post_d"]/2)) for sx in (-1,1) for sy in (-1,1)]

# ---------------- base
base = diff(bx(out_x, out_y, out_z), [bx(in_x, in_y, P["inner_h"]+1, z0=z_floor)])
base = union([base] + [cyl(P["post_d"], P["inner_h"], px, py, z_floor) for (px,py) in posts_xy])
cuts = [cyl(P["post_screw_d"], 14, px, py, out_z-14+0.01) for (px,py) in posts_xy]
kx = out_x/2; ky = P["keystone_y"]; kz0 = z_floor + P["keystone_z"]
cuts.append(bx(P["wall"]*3, P["keystone_w"], P["keystone_h"], kx, ky, kz0))
cuts.append(bx(P["wall"]-P["keystone_panel"]+0.01, P["keystone_w"]+2*P["keystone_relief"], P["keystone_h"]+2*P["keystone_relief"],
               in_x/2 + (P["wall"]-P["keystone_panel"])/2, ky, kz0-P["keystone_relief"]))
cuts.append(bx(P["wall"]*3, P["usbc_w"], P["usbc_h"], kx, P["usbc_y"], z_board_top + P["usbc_z_above_board"]))
cuts.append(bx(P["devkit_usb_w"], P["wall"]*3, P["devkit_usb_h"], P["devkit_usb_x"], -out_y/2, z_board_top + P["devkit_usb_z_above_board"]))
for i in range(P["vent_n"]):
    vx = -out_x/2 + 28 + i*(out_x-56)/(P["vent_n"]-1)
    for sy in (-1,1): cuts.append(bx(P["vent_w"], P["wall"]*3, P["vent_l"], vx, sy*out_y/2, out_z - P["vent_l"] - 6))
base = diff(base, cuts)

# ---------------- lid with module
def modXY(mx, my): return (P["mod_offset_x"] + mx, my)
lid = bx(out_x, out_y, P["lid_thk"], z0=out_z)
lid = union([lid] + [cyl(P["mod_post_d"], P["mod_post_h"], *modXY(mx,my), out_z - P["mod_post_h"]) for (mx,my) in P["mod_holes"]])
lc = [cyl(P["mod_post_screw_d"], P["mod_post_h"]+0.5, *modXY(mx,my), out_z - P["mod_post_h"] - 0.3) for (mx,my) in P["mod_holes"]]
wx, wy, ww, wh = P["mod_window"]; b = P["window_bevel"]
lc.append(frustum(ww, wh, ww, wh, out_z-1, out_z + P["lid_thk"] - b, *modXY(wx, wy)))
lc.append(frustum(ww, wh, ww+2*b, wh+2*b, out_z + P["lid_thk"] - b - 0.01, out_z + P["lid_thk"] + 0.5, *modXY(wx, wy)))
lc.append(cyl(P["shaft_hole_d"], P["lid_thk"]*3, *modXY(*P["mod_shaft"]), out_z - P["lid_thk"]))
for (mx,my) in P["mod_keys"]: lc.append(bx(P["key_w"], P["key_h"], P["lid_thk"]*3, *modXY(mx,my), out_z - P["lid_thk"]))
for (px,py) in posts_xy:
    lc.append(cyl(P["lid_hole_d"], P["lid_thk"]*3, px, py, out_z - P["lid_thk"]))
    lc.append(cyl(P["head_d"], P["head_t"]+0.01, px, py, out_z + P["lid_thk"] - P["head_t"]))
lid = diff(lid, lc)

# ---------------- keycap (stem through the 12x7.5 opening, flange under the lid, nub onto the switch)
sw = P["key_w"] - 2*P["cap_clear"]; sh = P["key_h"] - 2*P["cap_clear"]
cap = union([bx(sw, sh, P["cap_flange_t"] + P["lid_thk"] + P["cap_protrude"]),
             bx(sw + 2*P["cap_flange"], sh + 2*P["cap_flange"], P["cap_flange_t"]),
             cyl(P["cap_nub_d"], P["cap_nub_h"], z0=-P["cap_nub_h"])])

# ---------------- tray with corner notches for the lid posts
tray = None
if TRAY and os.path.exists(TRAY):
    tray = trimesh.load(TRAY)
    n = P["post_d"]/2 + 1.0
    tray = diff(tray, [bx(2*n, 2*n, 30, sx*in_x/2, sy*in_y/2, -5) for sx in (-1,1) for sy in (-1,1)])

# ---------------- bezel (option A)
bz_x = 59.0 + 2*P["mod_post_d"] + 2*P["bezel_margin"]; bz_y = 29.2 + 2*P["mod_post_d"] + 2*P["bezel_margin"]
bezel = union([bx(bz_x, bz_y, P["bezel_thk"], z0=0)] + [cyl(P["mod_post_d"], P["mod_post_h"], mx, my, -P["mod_post_h"]) for (mx,my) in P["mod_holes"]])
bc = [cyl(P["mod_post_screw_d"], P["mod_post_h"]+0.5, mx, my, -P["mod_post_h"]-0.3) for (mx,my) in P["mod_holes"]]
bc.append(frustum(ww, wh, ww, wh, -1, P["bezel_thk"]-b, wx, wy)); bc.append(frustum(ww, wh, ww+2*b, wh+2*b, P["bezel_thk"]-b-0.01, P["bezel_thk"]+0.5, wx, wy))
bc.append(cyl(P["shaft_hole_d"], 10, *P["mod_shaft"], -5))
for (mx,my) in P["mod_keys"]: bc.append(bx(P["key_w"], P["key_h"], 10, mx, my, -5))
for sx in (-1,1):
    for sy in (-1,1): bc.append(cyl(P["bezel_hole_d"], 10, sx*(bz_x/2-3), sy*(bz_y/2-3), -5))
bezel = diff(bezel, bc)

parts = [("base", base), ("lid", lid), ("keycap", cap), ("module_bezel", bezel)] + ([("tray_notched", tray)] if tray is not None else [])
for name, mesh in parts:
    assert mesh.is_watertight, name
    mesh.export(os.path.join(OUT, f"{name}.stl"))
    print(f"{name}: {np.round(mesh.bounds[1]-mesh.bounds[0],1)} mm, {len(mesh.faces)} faces")
print(f"outer {out_x} x {out_y} x {out_z+P['lid_thk']} | board top z={z_board_top:.1f} | lid underside z={out_z}")
print(f"bezel plate {bz_x:.1f} x {bz_y:.1f}; cutout in a bought lid for the bezel's posts/module: 66 x 36 (module + 0.5/side)")
