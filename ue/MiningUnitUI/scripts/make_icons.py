"""Draw T_IconUp/Down/Trash: 64x64 white glyph on transparent, 8x supersample -> LANCZOS. Out: scripts\\icons\\*.png"""
import math, os
from PIL import Image, ImageDraw, ImageOps
K, OUT = 8, os.path.join(os.path.dirname(os.path.abspath(__file__)), "icons")

def canvas():
    return Image.new("L", (64 * K, 64 * K), 0)

def rtri(d, pts, r):
    A, B, C = pts
    a, b, c = math.dist(B, C), math.dist(A, C), math.dist(A, B)
    s = (a + b + c) / 2
    area = abs((B[0]-A[0])*(C[1]-A[1]) - (C[0]-A[0])*(B[1]-A[1])) / 2
    inr = area / s
    ix = (a*A[0] + b*B[0] + c*C[0]) / (a + b + c)
    iy = (a*A[1] + b*B[1] + c*C[1]) / (a + b + c)
    f = (inr - r) / inr
    q = [(ix + (x - ix) * f, iy + (y - iy) * f) for x, y in pts]
    d.polygon([(x * K, y * K) for x, y in q], fill=255)
    for x, y in q:
        d.ellipse([(x - r) * K, (y - r) * K, (x + r) * K, (y + r) * K], fill=255)

def rect(d, x0, y0, x1, y1, fill, r=1.5):
    d.rounded_rectangle([x0 * K, y0 * K, x1 * K, y1 * K], radius=r * K, fill=fill)

def save(img, name):
    img = img.resize((64, 64), Image.LANCZOS)
    w = Image.new("L", (64, 64), 255)
    Image.merge("RGBA", (w, w, w, img)).save(os.path.join(OUT, name + ".png"))

os.makedirs(OUT, exist_ok=True)
up = canvas()
rtri(ImageDraw.Draw(up), [(32, 14), (60, 50), (4, 50)], 4)
save(up, "T_IconUp")
save(ImageOps.flip(up), "T_IconDown")
t = canvas(); d = ImageDraw.Draw(t)
rect(d, 22, 3, 42, 14, 255, 3)
rect(d, 27, 7, 37, 14, 0, 1)
rect(d, 6, 12, 58, 21, 255, 3)
d.polygon([(12*K, 25*K), (52*K, 25*K), (48*K, 58*K), (16*K, 58*K)], fill=255)
for cx in (24, 32, 40):
    rect(d, cx - 2, 31, cx + 2, 52, 0, 2)
save(t, "T_IconTrash")
def ell(d, cx, cy, r, fill):
    d.ellipse([(cx - r) * K, (cy - r) * K, (cx + r) * K, (cy + r) * K], fill=fill)

def line(d, a, b, w):
    d.line([(a[0] * K, a[1] * K), (b[0] * K, b[1] * K)], fill=255, width=int(w * K))
    ell(d, a[0], a[1], w / 2, 255); ell(d, b[0], b[1], w / 2, 255)

def poly(d, pts):
    d.polygon([(x * K, y * K) for x, y in pts], fill=255)

# steering wheel
t = canvas(); d = ImageDraw.Draw(t)
ell(d, 32, 32, 24, 255); ell(d, 32, 32, 16, 0)
rect(d, 8, 28, 56, 36, 255, 2); rect(d, 28, 32, 36, 56, 255, 2)
ell(d, 32, 32, 8, 255)
save(t, "T_IconStIdle")
# pickaxe: arched head + diagonal handle
t = canvas(); d = ImageDraw.Draw(t)
d.pieslice([(36 - 27) * K, (36 - 27) * K, (36 + 27) * K, (36 + 27) * K], 195, 345, fill=255)
ell(d, 36, 36, 19, 0)
d.rectangle([0, 36 * K, 64 * K, 64 * K], fill=0)
line(d, (12, 58), (38, 24), 6)
save(t, "T_IconStMining")
# docking: arrow down above U bracket
t = canvas(); d = ImageDraw.Draw(t)
rect(d, 27, 6, 37, 26, 255, 2); poly(d, [(32, 40), (50, 22), (14, 22)])
rect(d, 8, 50, 56, 58, 255, 2); rect(d, 8, 40, 14, 58, 255, 2); rect(d, 50, 40, 56, 58, 255, 2)
save(t, "T_IconStDocking")
save(ImageOps.flip(t), "T_IconStUndocking")
# droplet
t = canvas(); d = ImageDraw.Draw(t)
ell(d, 32, 40, 17, 255)
poly(d, [(32, 5), (47, 36), (17, 36)])
ell(d, 32, 42, 6, 0)
save(t, "T_IconStRefueling")
# crate + arrow out
t = canvas(); d = ImageDraw.Draw(t)
rect(d, 4, 18, 34, 48, 255, 3); rect(d, 11, 25, 27, 41, 0, 1)
poly(d, [(60, 33), (40, 17), (40, 49)])
save(t, "T_IconStUnloading")
# ship chevron
t = canvas(); d = ImageDraw.Draw(t)
d.polygon([(x * K, y * K) for x, y in ((32, 6), (54, 52), (32, 42), (10, 52))], fill=255)
save(t, "T_IconShip")
# pin
t = canvas(); d = ImageDraw.Draw(t)
ell(d, 32, 24, 15, 255)
d.polygon([(18 * K, 32 * K), (46 * K, 32 * K), (32 * K, 58 * K)], fill=255)
ell(d, 32, 24, 6, 0)
d.ellipse([22 * K, 56 * K, 42 * K, 60 * K], fill=255)
save(t, "T_IconPin")
print("wrote", OUT)
