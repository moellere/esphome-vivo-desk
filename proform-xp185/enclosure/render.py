import sys, numpy as np, trimesh, matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection
def render(path, out, views, title):
    m = trimesh.load(path)
    fig = plt.figure(figsize=(5*len(views), 5))
    for i,(el,az,name) in enumerate(views):
        ax = fig.add_subplot(1, len(views), i+1, projection="3d")
        tri = m.vertices[m.faces]
        n = m.face_normals; light = np.array([0.4, 0.3, 0.85]); shade = 0.45 + 0.55*np.clip(n@light, 0, 1)
        cols = np.stack([shade*0.55, shade*0.65, shade*0.85, np.ones_like(shade)], 1)
        pc = Poly3DCollection(tri, facecolors=cols, edgecolors="none"); ax.add_collection3d(pc)
        lo, hi = m.bounds; c = (lo+hi)/2; r = (hi-lo).max()/2
        ax.set_xlim(c[0]-r, c[0]+r); ax.set_ylim(c[1]-r, c[1]+r); ax.set_zlim(c[2]-r, c[2]+r)
        ax.view_init(elev=el, azim=az); ax.set_axis_off(); ax.set_title(name, fontsize=10)
    fig.suptitle(title); fig.tight_layout(); fig.savefig(out, dpi=110); plt.close(fig)
render(sys.argv[1], sys.argv[2], eval(sys.argv[3]), sys.argv[4])
